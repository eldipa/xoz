#include "xoz/repo/repository.h"

#include <cstring>
#include <filesystem>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"
#include "xoz/mem/endianness.h"

struct Repository::preload_repo_ctx_t Repository::dummy = {false, {0}};

Repository::Repository(const char* fpath):
        fpath(fpath), fblkarr(fpath, std::bind_front(Repository::preload_repo, dummy)), closed(true) {
    bootstrap_repository();
    assert(not closed);
    assert(fblkarr.begin_blk_nr() >= 1);
}

Repository::Repository(FileBlockArray&& fblkarr, const struct default_parameters_t& defaults, bool is_a_new_repository):
        fpath(fblkarr.get_file_path()), fblkarr(std::move(fblkarr)), closed(true) {
    if (is_a_new_repository) {
        // The given file block array has a valid and open file but it is not initialized as
        // a repository yet. We do that here.
        init_new_repository(defaults);
    }

    bootstrap_repository();
    assert(not closed);
    assert(fblkarr.begin_blk_nr() >= 1);
}

Repository::~Repository() { close(); }

Repository Repository::create(const char* fpath, bool fail_if_exists, const struct default_parameters_t& defaults) {
    // We pass defaults to the FileBlockArray::create via preload_repo function
    // so the array is created with the correct dimensions.
    // However, no header is written there so resulting file is not a valid repository yet
    struct preload_repo_ctx_t ctx = {false, defaults};
    FileBlockArray fblkarr =
            FileBlockArray::create(fpath, std::bind_front(Repository::preload_repo, std::ref(ctx)), fail_if_exists);

    // We delegate the initialization of the new repository to the Repository constructor
    // that it should call init_new_repository iff ctx.was_file_created
    return Repository(std::move(fblkarr), defaults, ctx.was_file_created);
}

Repository Repository::create_mem_based(const struct default_parameters_t& defaults) {
    FileBlockArray fblkarr = FileBlockArray::create_mem_based(defaults.blk_sz, 1 /* begin_blk_nr */);

    // Memory based file block arrays (and therefore Repository too) are always created
    // empty and require an initialization (so is_a_new_repository is always true)
    return Repository(std::move(fblkarr), defaults, true);
}

void Repository::bootstrap_repository() {
    // During the construction of Repository, in particular of FileBlockArray fblkarr,
    // the block array was initialized so we can read/write extents/header/trailer but we cannot
    // allocate yet.
    assert(not fblkarr.is_closed());
    read_and_check_header_and_trailer();

    // Let's load the root descriptor set.
    // So far we have the root segment loaded in root_sg *but*
    // if the particular setting is set, assume that the recently loaded root_sg is
    // not the root segment but a single extent that points a block(s) with the real
    // root segment.
    // This additional indirection allow us to encode large root segments outside the header.
    if (root_sg.inline_data_sz() == 4 and root_sg.ext_cnt() == 1) {
        external_root_sg_loc.add_extent(root_sg.exts()[0]);

        IOSegment io2(fblkarr, external_root_sg_loc);
        root_sg = Segment::load_struct_from(io2);

        // In the inline data we have the checksum of the root segment.
        // We don't have such checksum when the root segment is written in the header
        // because there is a checksum for the entire header.
        // But when the root segment is outside the header, we need this extra protection.
        IOSpan io3(root_sg.inline_data());

        [[maybe_unused]] uint32_t root_sg_chksum = io3.read_u32_from_le();

        // TODO check assert(checksum(io2) == root_sg_chksum);

    } else if (root_sg.inline_data_sz() != 0) {
        throw InconsistentXOZ(*this, "the repository header contains a root segment with an unexpected format.");
    }

    // Discard any checksum and the end-of-segment by removing the inline data
    root_sg.remove_inline_data();
    assert(root_sg.has_end_of_segment() == false);

    // Load the root set.
    // NOTE: if a single descriptor tries to allocate blocks from the array
    // it will crash because the allocator is not initialized yet and we cannot do it
    // because we need to scan the sets to find which extents/segments are already
    // allocated.
    root_dset = std::make_shared<DescriptorSet>(this->root_sg, fblkarr, fblkarr, idmgr);
    root_dset->load_set();

    // Scan which extents/segments are allocated so we can initialize the allocator.
    auto allocated = scan_descriptor_sets();

    // With this, we can do alloc/dealloc and the Repository is fully operational.
    fblkarr.allocator().initialize_from_allocated(allocated);

    closed = false;
}

std::ostream& Repository::print_stats(std::ostream& out) const {
    // TODO make the Repository print stats much alignes with the resto fo the object
    out << "XOZ Repository\n"
           "File: '"
        << fpath
        << "' "
           "[start pos: "
        // TODO << phy_repo_start_pos << ", end pos: " << phy_repo_end_pos
        << "]\n"
           "File status: ";

    if (closed) {
        out << "closed\n";
    } else {
        // out << "open [fail: " << fp.fail() << ", bad: " << fp.bad() << ", eof: " << fp.eof() << ", good: " <<
        // fp.good()
        //     << "]\n";  TODO
    }

    // TODO better stats!!
    auto blk_total_cnt = fblkarr.blk_cnt() + fblkarr.begin_blk_nr();
    out << "\nRepository size: " << (blk_total_cnt << fblkarr.blk_sz_order()) << " bytes, " << blk_total_cnt
        << " blocks\n"
        << "\nBlock size: " << fblkarr.blk_sz() << " bytes (order: " << (uint32_t)fblkarr.blk_sz_order() << ")\n"
        << "\nTrailer size: " << fblkarr.trailer_sz() << " bytes\n";

    return out;
}

const std::stringstream& Repository::expose_mem_fp() const { return fblkarr.expose_mem_fp(); }

std::list<Segment> Repository::scan_descriptor_sets() {
    // TODO this should be recursive to scan *all*, not just the root.

    std::list<Segment> allocated;

    allocated.push_back(root_dset->segment());
    for (auto it = root_dset->begin(); it != root_dset->end(); ++it) {
        auto& dsc(*it);
        if (dsc->does_own_edata()) {
            allocated.push_back(dsc->edata_segment_ref());
        }
    }

    return allocated;
}

struct Repository::stats_t Repository::stats() const {
    struct stats_t st;

    auto blkarr_st = fblkarr.stats();
    memcpy(&st.blkarr_st, &blkarr_st, sizeof(st.blkarr_st));

    return st;
}

namespace {

// In-disk repository's header
struct repo_header_t {
    // It should be "XOZ" followed by a NUL
    uint8_t magic[4];

    // Size of the whole repository, including the header
    // but not the trailer, in bytes. It is a multiple
    // of the block total count
    uint64_t repo_sz;

    // The size in bytes of the trailer
    //
    // TODO it must be smaller than the block size
    //
    // TODO this could be much smaller than 64 bits
    // Like 16 bits should be enough
    uint64_t trailer_sz;

    // Count of blocks in the repo.
    // It should be equal to repo_sz/blk_sz
    uint32_t blk_total_cnt;

    uint32_t unused;  // TODO

    // Log base 2 of the block size in bytes
    // Order of 10 means block size of 1KB,
    // order of 11 means block size of 2KB, and so on
    uint8_t blk_sz_order;

    // For more future metadata
    uint8_t reserved[7];

    // Feature flags. If the xoz library does not recognize one of those bits
    // it may or may not proceed reading. In specific:
    //
    // - if the unknown bit is in feature_flags_compat, it should be safe for
    //   the library to read and write the xoz file
    // - if the unknown bit is in feature_flags_incompat, the library must
    //   not read further and do not write anything.
    // - if the unknown bit is in feature_flags_ro_compat, the library can
    //   read the file bit it cannot write/update it.
    uint32_t feature_flags_compat;
    uint32_t feature_flags_incompat;
    uint32_t feature_flags_ro_compat;

    // Segment that points to the blocks that hold the root or main descriptor set
    // See read_and_check_header_and_trailer for the complete interpretation of this.
    uint8_t root_sg[12];

    uint32_t hdr_checksum;
} __attribute__((packed));

// In-disk repository's trailer
struct repo_trailer_t {
    // It should be "EOF" followed by a NUL
    uint8_t magic[4];
} __attribute__((packed));

static_assert(sizeof(struct repo_header_t) == 64);
}  // namespace

void Repository::preload_repo(struct Repository::preload_repo_ctx_t& ctx, std::istream& is,
                              struct FileBlockArray::blkarr_cfg_t& cfg, bool on_create) {
    if (on_create) {
        cfg.blk_sz = ctx.defaults.blk_sz;
        cfg.begin_blk_nr = 1;  // TODO

        ctx.was_file_created = true;
        return;
    }

    struct repo_header_t hdr;
    is.read((char*)&hdr, sizeof(hdr));

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw std::runtime_error("magic string 'XOZ' not found in the header.");
    }

    // TODO
    // check checksum of (char*)(&hdr) against hdr.hdr_checksum

    if (hdr.feature_flags_incompat) {
        // TODO eventually we want to fail iff we don't understand one of those flags only
        throw std::runtime_error("the repository has incompatible features.");
    }

    uint8_t blk_sz_order = u8_from_le(hdr.blk_sz_order);

    if (blk_sz_order < 6 or blk_sz_order > 16) {
        throw std::runtime_error(
                (F() << "block size order " << blk_sz_order << " is out of range [6 to 16] (block sizes of 64 to 64K).")
                        .str());
    }

    cfg.blk_sz = (1 << hdr.blk_sz_order);
    cfg.begin_blk_nr = 1;  // TODO it should be 1 or 2

    return;
}

void Repository::read_and_check_header_and_trailer() {
    struct repo_header_t hdr;

    if (fblkarr.header_sz() < sizeof(hdr)) {
        throw InconsistentXOZ(*this, F() << "mismatch between the minimum size of the header (" << sizeof(hdr)
                                         << " bytes) and the real header read from the file (" << fblkarr.header_sz()
                                         << " bytes).");
    }

    fblkarr.read_header((char*)&hdr, sizeof(hdr));

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'XOZ' not found in the header.");
    }

    // TODO
    // check checksum of (char*)(&hdr) against hdr.hdr_checksum

    if (hdr.feature_flags_incompat) {
        throw InconsistentXOZ(*this, "the repository has incompatible features.");
    }

    if (hdr.feature_flags_ro_compat) {
        // TODO implement read-only mode
        throw InconsistentXOZ(
                *this,
                "the repository has read-only compatible features and the repository was not open in read-only mode.");
    }

    uint8_t blk_sz_order = u8_from_le(hdr.blk_sz_order);
    uint32_t blk_sz = (1 << hdr.blk_sz_order);

    if (blk_sz_order < 6 or blk_sz_order > 16) {
        throw InconsistentXOZ(*this, F() << "block size order " << blk_sz_order
                                         << " is out of range [6 to 16] (block sizes of 64 to 64K).");
    }

    auto blk_total_cnt = u32_from_le(hdr.blk_total_cnt);
    if (blk_total_cnt == 0) {
        throw InconsistentXOZ(*this, "the repository has a declared block total count of zero.");
    }

    // Calculate the repository size based on the block count.
    repo_sz = blk_total_cnt << blk_sz_order;

    // Read the declared repository size from the header and
    // check that it matches with what we calculated
    uint64_t repo_sz_read = u64_from_le(hdr.repo_sz);
    if (repo_sz != repo_sz_read) {
        throw InconsistentXOZ(*this, F() << "the repository declared a size of " << repo_sz_read
                                         << " bytes but it is expected to have " << repo_sz
                                         << " bytes based on the declared block total count " << blk_total_cnt
                                         << " and block size " << blk_sz << ".");
    }


    /*
     * TODO rewrite these checks
    if (repo_sz > fp_end) {
        throw InconsistentXOZ(*this,
                              F() << "the repository has a declared size (" << repo_sz
                                  << ") starting at "
                                  //<< phy_repo_start_pos << " offset this gives an expected end of "
                                  // TODO<< phy_repo_end_pos << " which goes beyond the physical file end at " << fp_end
                                  << ".");
    }

    if (fp_end > phy_repo_end_pos) {
        // More real bytes than the ones in the repo
        // Perhaps an incomplete shrink/truncate?
    }
    assert(fp_end >= phy_repo_end_pos);
    */


    // load root set's segment (tentative, see _init_repository)
    static_assert(sizeof(hdr.root_sg) >= 12);
    IOSpan io(hdr.root_sg, sizeof(hdr.root_sg));
    root_sg = Segment::load_struct_from(io);

    // the root_sg is located in the header so we mark this with an empty extent
    external_root_sg_loc = Segment::EmptySegment();

    if (!(root_sg.inline_data_sz() == 4 and root_sg.ext_cnt() == 1) and root_sg.inline_data_sz() != 0) {
        throw InconsistentXOZ(*this, "the repository header contains a root segment with an unexpected format.");
    }


    // TODO this *may* still be useful: assert(phy_repo_end_pos > 0);

    uint64_t trailer_sz = u64_from_le(hdr.trailer_sz);  // TODO uint64_t ??
    if (trailer_sz < sizeof(struct repo_trailer_t)) {
        throw InconsistentXOZ(*this, F() << "the declared trailer size (" << trailer_sz
                                         << ") is too small, required at least " << sizeof(struct repo_trailer_t)
                                         << " bytes.");
    }

    if (trailer_sz != fblkarr.trailer_sz()) {
        throw InconsistentXOZ(*this, F() << "mismatch between the declared trailer size (" << trailer_sz
                                         << " bytes) and the real trailer read from the file (" << fblkarr.trailer_sz()
                                         << " bytes).");
    }

    struct repo_trailer_t eof;
    fblkarr.read_trailer((char*)&eof, sizeof(eof));

    if (strncmp((char*)&eof.magic, "EOF", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'EOF' not found in the trailer.");
    }
}

void Repository::write_header(const std::vector<uint8_t>& root_sg_bytes) {
    // Note: currently the trailer size is fixed but we may decide
    // to make it variable later.
    //
    // The header will store the trailer size so we may decide
    // here to change it because at the moment of calling close()
    // we should have all the info needed.
    uint64_t trailer_sz = sizeof(struct repo_trailer_t);

    // note: we declare that the repository has the same block count
    // than the file block array *plus* its begin blk number to count
    // for the array's header (where the repo's header will be written into)
    //
    // one comment on this: the file block array *may* have more blocks than
    // the blk_cnt() says because it may be keeping some unused blocks for
    // future allocations (this is the fblkarr.capacity()).
    //
    // the call to fblkarr.close() *should* release those blocks and resize
    // the file to the correct size.
    // the caveat is that it feels fragile to store something without being
    // 100% sure that it is true -- TODO store fblkarr.capacity() ? may be
    // store more details of fblkarr?
    uint32_t blk_total_cnt = fblkarr.blk_cnt() + fblkarr.begin_blk_nr();

    struct repo_header_t hdr = {
            .magic = {'X', 'O', 'Z', 0},
            .repo_sz = u64_to_le(blk_total_cnt << fblkarr.blk_sz_order()),
            .trailer_sz = u64_to_le(trailer_sz),
            .blk_total_cnt = u32_to_le(blk_total_cnt),
            .unused = 0,  // TODO
            .blk_sz_order = u8_to_le(fblkarr.blk_sz_order()),
            .reserved = {0, 0, 0, 0, 0, 0, 0},
            .feature_flags_compat = u32_to_le(0),
            .feature_flags_incompat = u32_to_le(0),
            .feature_flags_ro_compat = u32_to_le(0),
            .root_sg = {0},
            .hdr_checksum = u32_to_le(0),
    };

    assert(sizeof(hdr.root_sg) == root_sg_bytes.size());
    memcpy(hdr.root_sg, root_sg_bytes.data(), sizeof(hdr.root_sg));

    fblkarr.write_header((const char*)&hdr, sizeof(hdr));
}

void Repository::write_trailer() {
    struct repo_trailer_t eof = {.magic = {'E', 'O', 'F', 0}};
    fblkarr.write_trailer((const char*)&eof, sizeof(eof));
}

std::vector<uint8_t> Repository::_encode_empty_root_segment() {
    const auto hdr_capacity = sizeof(((struct repo_header_t*)nullptr)->root_sg);
    std::vector<uint8_t> root_sg_bytes(hdr_capacity);

    Segment root_sg_empty = Segment::EmptySegment();
    root_sg_empty.add_end_of_segment();

    IOSpan io(root_sg_bytes.data(), (uint32_t)root_sg_bytes.size());
    root_sg_empty.write_struct_into(io);

    assert(root_sg_bytes.size() == hdr_capacity);
    return root_sg_bytes;
}

// TODO: most of this code could be handled by a (future) special descriptor type to hold
// descriptor sets
std::vector<uint8_t> Repository::update_and_encode_root_segment_and_loc() {
    const auto hdr_capacity = sizeof(((struct repo_header_t*)nullptr)->root_sg);
    std::vector<uint8_t> root_sg_bytes(hdr_capacity);

    root_dset->write_set();
    assert(root_sg.has_end_of_segment() == false);
    assert(external_root_sg_loc.has_end_of_segment() == false);

    auto root_sg_sz = root_sg.calc_struct_footprint_size();
    if (root_sg_sz == hdr_capacity or root_sg_sz + Segment::EndOfSegmentSize <= hdr_capacity) {

        // If it does not fit perfectly we need to mark somehow the end of the segment
        // otherwise the reader will overflow and read garbage after the segment.
        if (root_sg_sz != hdr_capacity) {
            root_sg.add_end_of_segment();
            root_sg_sz = root_sg.calc_struct_footprint_size();
        }

        assert(root_sg_sz <= hdr_capacity);

        // the root segment is small enough to be stored in the header;
        // release any allocated space
        if (not external_root_sg_loc.is_empty_space()) {
            fblkarr.allocator().dealloc(external_root_sg_loc);
            external_root_sg_loc = Segment::EmptySegment();
        }

        IOSpan io(root_sg_bytes);
        root_sg.write_struct_into(io);

        // remove the end of segment
        root_sg.remove_inline_data();
    } else {
        // Either the root_sg is too large to fit in the header or it is small
        // enough but we cannot put there because with the addition of the end-of-segment
        // it will not fit.

        // root segment is too large to fit in the header, try to use
        // the space allocated in external_root_sg_loc first, allocate more
        // if required
        auto external_capacity = external_root_sg_loc.calc_data_space_size(fblkarr.blk_sz_order());
        if ((external_capacity >> 2) > root_sg_sz) {
            // the space needed is less than 25% of the current capacity: dealloc + (re)alloc
            // to shrink and save some space
            fblkarr.allocator().dealloc(external_root_sg_loc);
            external_root_sg_loc = fblkarr.allocator().alloc(root_sg_sz);
        } else if (external_capacity >= root_sg_sz) {
            // the current capacity can hold the root segm: do nothing
        } else {
            fblkarr.allocator().dealloc(external_root_sg_loc);
            external_root_sg_loc = fblkarr.allocator().alloc(root_sg_sz);
        }

        // Write the root segment in an external location
        IOSegment io(fblkarr, external_root_sg_loc);
        root_sg.write_struct_into(io);

        uint32_t root_sg_chksum = 0;  // TODO
        external_root_sg_loc.reserve_inline_data(sizeof(root_sg_chksum));
        IOSpan io2(external_root_sg_loc.inline_data());
        io2.write_u32_to_le(root_sg_chksum);

        IOSpan io3(root_sg_bytes);
        external_root_sg_loc.write_struct_into(io3);

        // We don't really want to store the checksum, it was put here just for
        // the write_struct_into call above. Remove the inline then.
        external_root_sg_loc.remove_inline_data();
    }

    assert(root_sg_bytes.size() == hdr_capacity);
    assert(root_sg.has_end_of_segment() == false);
    assert(external_root_sg_loc.has_end_of_segment() == false);
    return root_sg_bytes;
}

void Repository::init_new_repository(const struct default_parameters_t& defaults) {
    fblkarr.fail_if_bad_blk_sz(defaults.blk_sz, 0, REPOSITORY_MIN_BLK_SZ);

    const auto root_sg_bytes = _encode_empty_root_segment();
    write_header(root_sg_bytes);
    write_trailer();
}

void Repository::close() {
    if (closed)
        return;

    const auto root_sg_bytes = update_and_encode_root_segment_and_loc();

    write_header(root_sg_bytes);
    write_trailer();

    fblkarr.close();
    closed = true;
}
