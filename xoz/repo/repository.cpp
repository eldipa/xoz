#include "xoz/repo/repository.h"

#include <cstring>
#include <filesystem>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"
#include "xoz/mem/endianness.h"
#include "xoz/mem/inet_checksum.h"

struct Repository::preload_repo_ctx_t Repository::dummy = {false, {0}};

Repository::Repository(const DescriptorMapping& dmap, const char* fpath):
        fpath(fpath),
        fblkarr(std::make_unique<FileBlockArray>(fpath, std::bind_front(Repository::preload_repo, dummy))),
        closed(true),
        closing(false),
        rctx(dmap),
        trampoline_segm(fblkarr->blk_sz_order()) {
    bootstrap_repository();
    assert(not closed);
    assert(this->fblkarr->begin_blk_nr() >= 1);
}

Repository::Repository(const DescriptorMapping& dmap, std::unique_ptr<FileBlockArray>&& fblkarr_,
                       const struct default_parameters_t& defaults, bool is_a_new_repository):
        fpath(fblkarr_->get_file_path()),
        fblkarr(std::move(fblkarr_)),
        closed(true),
        closing(false),
        rctx(dmap),
        trampoline_segm(fblkarr->blk_sz_order()) {
    if (is_a_new_repository) {
        // The given file block array has a valid and open file but it is not initialized as
        // a repository yet. We do that here.
        init_new_repository(defaults);
    }

    bootstrap_repository();
    assert(not closed);
    assert(this->fblkarr->begin_blk_nr() >= 1);
}

Repository::~Repository() { close(); }

Repository Repository::create(const DescriptorMapping& dmap, const char* fpath, bool fail_if_exists,
                              const struct default_parameters_t& defaults) {
    // Check that the default block size is large enough and valid.
    // The same check will happen in FileBlockArray::create but we do it here because
    // the minimum block size (MIN_BLK_SZ) is an extra requirement of us
    // not of FileBlockArray.
    FileBlockArray::fail_if_bad_blk_sz(defaults.blk_sz, 0, MIN_BLK_SZ);

    // We pass defaults to the FileBlockArray::create via preload_repo function
    // so the array is created with the correct dimensions.
    // However, no header is written there so resulting file is not a valid repository yet
    struct preload_repo_ctx_t ctx = {false, defaults};
    auto fblkarr_ptr =
            FileBlockArray::create(fpath, std::bind_front(Repository::preload_repo, std::ref(ctx)), fail_if_exists);

    // We delegate the initialization of the new repository to the Repository constructor
    // that it should call init_new_repository iff ctx.was_file_created
    return Repository(dmap, std::move(fblkarr_ptr), defaults, ctx.was_file_created);
}

Repository Repository::create_mem_based(const DescriptorMapping& dmap, const struct default_parameters_t& defaults) {
    // Check that the default block size is large enough and valid.
    // The same check will happen in FileBlockArray::create but we do it here because
    // the minimum block size (MIN_BLK_SZ) is an extra requirement of us
    // not of FileBlockArray.
    FileBlockArray::fail_if_bad_blk_sz(defaults.blk_sz, 0, MIN_BLK_SZ);

    auto fblkarr_ptr = FileBlockArray::create_mem_based(defaults.blk_sz, 1 /* begin_blk_nr */);

    // Memory based file block arrays (and therefore Repository too) are always created
    // empty and require an initialization (so is_a_new_repository is always true)
    return Repository(dmap, std::move(fblkarr_ptr), defaults, true);
}

void Repository::bootstrap_repository() {
    // During the construction of Repository, in particular of FileBlockArray fblkarr,
    // the block array was initialized so we can read/write extents/header/trailer but we cannot
    // allocate yet (we cannot use fblkarr->allocator() yet).
    assert(not fblkarr->is_closed());
    read_and_check_header_and_trailer();

    // Scan which extents/segments are allocated so we can initialize the allocator.
    auto allocated = scan_descriptor_sets();

    // Add the trampoline segment, if any
    if (trampoline_segm.length()) {
        allocated.push_back(trampoline_segm);
    }

    // With this, we can do alloc/dealloc and the Repository is fully operational.
    fblkarr->allocator().initialize_from_allocated(allocated);

    closed = false;
}

const std::stringstream& Repository::expose_mem_fp() const { return fblkarr->expose_mem_fp(); }

std::list<Segment> Repository::scan_descriptor_sets() {
    // TODO this should be recursive to scan *all*, not just the root.

    std::list<Segment> allocated;

    allocated.push_back(root_holder->set()->segment());
    for (auto it = root_holder->set()->begin(); it != root_holder->set()->end(); ++it) {
        auto& dsc(*it);
        if (dsc->does_own_edata()) {
            allocated.push_back(dsc->edata_segment_ref());
        }
    }

    return allocated;
}

struct Repository::stats_t Repository::stats() const {
    struct stats_t st;

    auto fblkarr_st = fblkarr->stats();
    memcpy(&st.fblkarr_stats, &fblkarr_st, sizeof(st.fblkarr_stats));

    auto allocator_st = fblkarr->allocator().stats();
    memcpy(&st.allocator_stats, &allocator_st, sizeof(st.allocator_stats));

    st.capacity_repo_sz = (fblkarr->capacity() + fblkarr->begin_blk_nr()) << fblkarr->blk_sz_order();
    st.in_use_repo_sz = (fblkarr->blk_cnt() + fblkarr->begin_blk_nr()) << fblkarr->blk_sz_order();

    st.capacity_repo_sz += fblkarr->trailer_sz();
    st.in_use_repo_sz += fblkarr->trailer_sz();

    st.capacity_repo_sz_kb = double(st.capacity_repo_sz) / double(1024.0);
    st.in_use_repo_sz_kb = double(st.in_use_repo_sz) / double(1024.0);

    st.in_use_repo_sz_rel = (st.capacity_repo_sz == 0) ? 0 : (double(st.in_use_repo_sz) / double(st.capacity_repo_sz));

    st.header_sz = fblkarr->header_sz();
    st.trailer_sz = fblkarr->trailer_sz();
    return st;
}

void PrintTo(const Repository& repo, std::ostream* out) {
    struct Repository::stats_t st = repo.stats();
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << "File:              " << std::setfill(' ') << std::setw(12) << repo.fblkarr->get_file_path() << "\n";

    auto& fp = repo.fblkarr->phy_file_stream();
    (*out) << "Status:            ";
    if (repo.fblkarr->is_closed()) {
        (*out) << std::setfill(' ') << std::setw(12) << "closed\n\n";
    } else {
        (*out) << "        open "
               << "[fail: " << fp.fail() << ", bad: " << fp.bad() << ", eof: " << fp.eof() << ", good: " << fp.good()
               << "]\n\n";
    }

    (*out) << "-- Repository -----------------\n"
           << "Capacity:          " << std::setfill(' ') << std::setw(12) << st.capacity_repo_sz_kb << " kb\n"
           << "In use:            " << std::setfill(' ') << std::setw(12) << st.in_use_repo_sz_kb << " kb ("
           << std::setfill(' ') << std::setw(5) << std::fixed << std::setprecision(2) << (st.in_use_repo_sz_rel * 100)
           << "%)\n"
           << " - Header:         " << std::setfill(' ') << std::setw(12) << st.header_sz << " bytes\n"
           << " - Trailer:        " << std::setfill(' ') << std::setw(12) << st.trailer_sz << " bytes\n"
           << "\n";

    (*out) << "-- Block Array ----------------\n"
           << repo.fblkarr << "\n"
           << "\n"
           << "-- Allocator ------------------\n"
           << repo.fblkarr->allocator() << "\n";

    out->flags(ioflags);
}

std::ostream& operator<<(std::ostream& out, const Repository& repo) {
    PrintTo(repo, &out);
    return out;
}


void Repository::preload_repo(struct Repository::preload_repo_ctx_t& ctx, std::istream& is,
                              struct FileBlockArray::blkarr_cfg_t& cfg, bool on_create) {
    if (on_create) {
        cfg.blk_sz = ctx.defaults.blk_sz;
        cfg.begin_blk_nr = 1;

        ctx.was_file_created = true;
        return;
    }

    struct repo_header_t hdr;
    is.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    check_header_magic(hdr);
    compute_and_check_header_checksum(hdr);

    uint8_t blk_sz_order = u8_from_le(hdr.blk_sz_order);
    check_blk_sz_order(blk_sz_order);

    cfg.blk_sz = (1 << blk_sz_order);
    cfg.begin_blk_nr = 1;

    return;
}

void Repository::read_and_check_header_and_trailer() {
    struct repo_header_t hdr;

    if (fblkarr->header_sz() < sizeof(hdr)) {
        throw InconsistentXOZ(*this, F() << "mismatch between the minimum size of the header (" << sizeof(hdr)
                                         << " bytes) and the real header read from the file (" << fblkarr->header_sz()
                                         << " bytes).");
    }

    fblkarr->read_header(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    check_header_magic(hdr);
    compute_and_check_header_checksum(hdr);

    feature_flags_compat = u32_from_le(hdr.feature_flags_compat);
    feature_flags_incompat = u32_from_le(hdr.feature_flags_incompat);
    feature_flags_ro_compat = u32_from_le(hdr.feature_flags_ro_compat);

    if (feature_flags_incompat) {
        throw InconsistentXOZ(*this, "the repository has incompatible features.");
    }

    if (feature_flags_ro_compat) {
        // TODO implement read-only mode
        throw InconsistentXOZ(
                *this,
                "the repository has read-only compatible features and the repository was not open in read-only mode.");
    }

    uint8_t blk_sz_order = u8_from_le(hdr.blk_sz_order);
    check_blk_sz_order(blk_sz_order);

    uint32_t blk_sz = (1 << blk_sz_order);


    auto blk_total_cnt = u32_from_le(hdr.blk_total_cnt);
    if (blk_total_cnt == 0) {
        throw InconsistentXOZ(*this, "the repository has a declared block total count of zero.");
    }

    // Calculate the repository size based on the block count.
    uint64_t repo_sz = blk_total_cnt << blk_sz_order;

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


    load_root_holder(hdr);

    // TODO this *may* still be useful: assert(phy_repo_end_pos > 0);

    uint16_t trailer_sz = u16_from_le(hdr.trailer_sz);
    if (uint64_t(trailer_sz) < sizeof(struct repo_trailer_t)) {
        throw InconsistentXOZ(*this, F() << "the declared trailer size (" << trailer_sz
                                         << " bytes) is too small, required at least " << sizeof(struct repo_trailer_t)
                                         << " bytes.");
    }

    if (uint64_t(trailer_sz) != fblkarr->trailer_sz()) {
        throw InconsistentXOZ(*this, F() << "mismatch between the declared trailer size (" << trailer_sz
                                         << " bytes) and the real trailer read from the file (" << fblkarr->trailer_sz()
                                         << " bytes).");
    }

    struct repo_trailer_t eof;
    fblkarr->read_trailer(reinterpret_cast<char*>(&eof), sizeof(eof));

    if (strncmp(reinterpret_cast<const char*>(&eof.magic), "EOF", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'EOF' not found in the trailer.");
    }
}

void Repository::write_header() {

    // Write the root holder in the buffer. This *may* trigger an (de)allocation
    // in the fblkarr if the use of a trampoline is required or not.
    //
    // Caller *MUST* call root_holder->update_header() before calling write_header()
    // so we can be sure that all the descriptor sets (including the root) are up to date
    // and the holder has the latest updated sizes.
    uint8_t rootbuf[HEADER_ROOT_SET_SZ] = {0};
    uint8_t flags = 0;
    write_root_holder(rootbuf, HEADER_ROOT_SET_SZ, flags);

    // Despite that close() should be doing a release() of any free block,
    // the write_root_holder() may had deallocated stuff making free
    // new blocks.
    // So this is the last chance to release them (only on closing)
    if (closing) {
        fblkarr->allocator().release();
    }

    // Note: currently the trailer size is fixed but we may decide
    // to make it variable later.
    //
    // The header will store the trailer size so we may decide
    // here to change it because at the moment of calling close()
    // we should have all the info needed.
    uint16_t trailer_sz = assert_u16(sizeof(struct repo_trailer_t));

    // note: we declare that the repository has the same block count
    // than the file block array *plus* its begin blk number to count
    // for the array's header (where the repo's header will be written into)
    //
    // one comment on this: the file block array *may* have more blocks than
    // the blk_cnt() says because it may be keeping some unused blocks for
    // future allocations (this is the fblkarr->capacity()).
    //
    // the call to fblkarr->close() *should* release those blocks and resize
    // the file to the correct size.
    // the caveat is that it feels fragile to store something without being
    // 100% sure that it is true -- TODO store fblkarr->capacity() ? may be
    // store more details of fblkarr?
    uint32_t blk_total_cnt = fblkarr->blk_cnt() + fblkarr->begin_blk_nr();

    struct repo_header_t hdr = {.magic = {'X', 'O', 'Z', 0},
                                .app_name = {0},
                                .repo_sz = u64_to_le(blk_total_cnt << fblkarr->blk_sz_order()),
                                .trailer_sz = u16_to_le(trailer_sz),
                                .blk_total_cnt = u32_to_le(blk_total_cnt),
                                .blk_sz_order = u8_to_le(fblkarr->blk_sz_order()),
                                .flags = 0,  // to override
                                .feature_flags_compat = u32_to_le(0),
                                .feature_flags_incompat = u32_to_le(0),
                                .feature_flags_ro_compat = u32_to_le(0),
                                .root = {0},  // to override
                                .checksum = u16_to_le(0),
                                .padding = {0}};


    // Update root and flags
    assert(sizeof(hdr.root) == sizeof(rootbuf));
    memcpy(hdr.root, rootbuf, sizeof(hdr.root));

    hdr.flags = u8_to_le(flags);

    // Compute checksum and write the header
    hdr.checksum = u16_to_le(compute_header_checksum(hdr));

    fblkarr->write_header(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
}

void Repository::write_trailer() {
    struct repo_trailer_t eof = {.magic = {'E', 'O', 'F', 0}};
    fblkarr->write_trailer(reinterpret_cast<const char*>(&eof), sizeof(eof));
}

void Repository::init_new_repository(const struct default_parameters_t& defaults) {
    fblkarr->fail_if_bad_blk_sz(defaults.blk_sz, 0, MIN_BLK_SZ);

    trampoline_segm = Segment::EmptySegment(fblkarr->blk_sz_order());
    root_holder = DescriptorSetHolder::create(*fblkarr.get(), rctx);

    // Ensure that the holder has a valid id.
    root_holder->id(rctx.request_temporal_id());


    // Write any pending write (it should be a few if any due the initialization
    // of the holder's and set's structures. Update the root holder but do not
    // try to release any free space, it should be none (and because fblkarr's allocator
    // is not fully initialized yet).
    //
    // This must be called before write_header() so we can by 100% sure of how
    // many blocks are being used and how large the root holder is and if it
    // fits in the header or not.
    //
    // Note: it is important that the root holder does not do any allocation
    // because fblkarr is not fully initialized yet.
    // In theory we should be fine because root holder does not require
    // alloc any space for an empty set (the initial state of any new repository)
    // and neither write_header nor write_trailer requires alloc space
    // (write_trailer will not try to alloc space for a trampoline because the
    // root holder of an empty set should fit in the header).
    // Once we call bootstrap_repository() we should be fine.
    root_holder->full_sync(false);

    write_header();
    write_trailer();
}

void Repository::flush_writes(const bool release) {
    // Update the root holder. If there is any pending write, this will
    // do it. This may trigger some allocations in fblkarr and if release
    // is true, it may trigger some deallocations (shrinks) too.
    root_holder->full_sync(release);
    if (release) {
        fblkarr->allocator().release();
    }

    write_header();
    write_trailer();
}

void Repository::close() {
    if (closed)
        return;

    closing = true;
    flush_writes(true);

    fblkarr->close();
    closed = true;
    closing = false;
}

void Repository::load_root_holder(struct repo_header_t& hdr) {
    IOSpan root_io(hdr.root, sizeof(hdr.root));

    FileBlockArray& fblkarr_ref = *fblkarr.get();

    if (hdr.flags & 0x80) {
        // The root field in the xoz header contains 2 bytes for the trampoline's content
        // checksum and the segment to the trampoline.
        uint32_t checksum = uint32_t(root_io.read_u16_from_le());
        trampoline_segm = Segment::load_struct_from(root_io, fblkarr_ref.blk_sz_order());

        // Read trampoline's content. We expect to find a set descriptor holder there.
        auto trampoline_io = IOSegment(fblkarr_ref, trampoline_segm);

        // See if the holder is in the trampoline. Build a shared ptr to DescriptorSetHolder.
        auto dsc = DescriptorSetHolder::load_struct_from(trampoline_io, rctx, fblkarr_ref);
        root_holder = Descriptor::cast<DescriptorSetHolder>(dsc);

        // Check that trampoline's content checksum is correct.
        auto checksum_check = fold_inet_checksum(inet_remove(checksum, root_holder->checksum));
        if (not is_inet_checksum_good(checksum_check)) {
            throw InconsistentXOZ((F() << "Root holder trampoline checksum failed:"
                                       << "computed " << std::hex << checksum << " but expected " << std::hex
                                       << root_holder->checksum << " (chk " << std::hex << checksum_check << ")")
                                          .str());
        }
    } else {
        // No trampoline
        trampoline_segm = Segment::EmptySegment(fblkarr_ref.blk_sz_order());

        // The root field has the descriptor set holder
        auto dsc = DescriptorSetHolder::load_struct_from(root_io, rctx, fblkarr_ref);
        root_holder = Descriptor::cast<DescriptorSetHolder>(dsc);
    }
}

void Repository::write_root_holder(uint8_t* rootbuf, const uint32_t rootbuf_sz, uint8_t& flags) {
    assert(rootbuf_sz <= HEADER_ROOT_SET_SZ);
    IOSpan root_io(rootbuf, rootbuf_sz);

    FileBlockArray& fblkarr_ref = *fblkarr.get();

    bool trampoline_required = root_holder->calc_struct_footprint_size() > HEADER_ROOT_SET_SZ;

    if (trampoline_required) {
        // Expand/shrink the trampoline space to make room for the
        // root descriptor set holder (with its space was updated/recalculated
        // in the call to root_holder.update_header() made by the caller)
        update_trampoline_space();

        // Write the holder in the trampoline
        auto trampoline_io = IOSegment(fblkarr_ref, trampoline_segm);
        root_holder->write_struct_into(trampoline_io, rctx);

        // Write in the xoz file header the checksum of the trampoline
        // and the segment that points to.
        root_io.write_u16_to_le(inet_to_u16(root_holder->checksum));
        trampoline_segm.write_struct_into(root_io);

        flags |= uint8_t((trampoline_required) << 7);
    } else {
        // No trampoline required, release/dealloc it if we have one
        if (trampoline_segm.length() != 0) {
            fblkarr_ref.allocator().dealloc(trampoline_segm);
            trampoline_segm.clear();
        }

        root_holder->write_struct_into(root_io, rctx);

        flags &= uint8_t(0x7f);
    }
}

void Repository::update_trampoline_space() {
    uint32_t cur_sz = trampoline_segm.calc_data_space_size();
    uint32_t req_sz = root_holder->calc_struct_footprint_size();
    assert(req_sz > 0);

    const bool should_expand = (cur_sz < req_sz);
    const bool should_shrink = ((cur_sz >> 1) >= req_sz);
    if (should_expand or should_shrink) {
        if (trampoline_segm.length() == 0) {
            trampoline_segm = fblkarr->allocator().alloc(req_sz);
        } else {
            trampoline_segm = fblkarr->allocator().realloc(trampoline_segm, req_sz);
        }
    }

    // ensure the trampoline segment has an end so we can load it correctly
    // in load_root_holder()
    trampoline_segm.add_end_of_segment();

    // It may be possible that the allocator gave us a segment too fragmented
    // with too many extents ant that the final size of the segment is too
    // large to fit in the header.
    // In this case we alloc a single extent that we know it has a size smaller
    // than the available space
    //
    // TODO test this part
    if (trampoline_segm.calc_struct_footprint_size() > HEADER_ROOT_SET_SZ) {
        fblkarr->allocator().dealloc(trampoline_segm);
        const auto ext = fblkarr->allocator().alloc_single_extent(req_sz);
        trampoline_segm = Segment::EmptySegment(fblkarr->blk_sz_order());
        trampoline_segm.add_extent(ext);
        trampoline_segm.add_end_of_segment();

        assert(trampoline_segm.calc_struct_footprint_size() <= HEADER_ROOT_SET_SZ);
    }
}

void Repository::check_header_magic(struct repo_header_t& hdr) {
    if (strncmp(reinterpret_cast<const char*>(&hdr.magic), "XOZ", 4) != 0) {
        throw std::runtime_error("magic string 'XOZ' not found in the header.");
    }
}

uint16_t Repository::compute_header_checksum(struct repo_header_t& hdr) {
    // Compute the checksum of the header. The field 'checksum' must be temporally
    // zeroed to do the computation
    uint16_t stored_checksum = hdr.checksum;
    hdr.checksum = 0;

    uint32_t checksum = inet_checksum(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
    hdr.checksum = stored_checksum;

    return inet_to_u16(checksum);
}

void Repository::compute_and_check_header_checksum(struct repo_header_t& hdr) {
    uint16_t stored_checksum = hdr.checksum;
    uint32_t checksum = uint32_t(compute_header_checksum(hdr));

    // Check the checksum of the header against the one stored in the header itself.
    auto checksum_check = fold_inet_checksum(inet_remove(checksum, stored_checksum));
    if (not is_inet_checksum_good(checksum_check)) {
        throw InconsistentXOZ((F() << "Header checksum failed:"
                                   << "computed " << std::hex << checksum << " but expected " << std::hex
                                   << stored_checksum << " (chk " << std::hex << checksum_check << ")")
                                      .str());
    }
}

void Repository::check_blk_sz_order(const uint8_t blk_sz_order) {
    if (blk_sz_order < MIN_BLK_SZ_ORDER or blk_sz_order > MAX_BLK_SZ_ORDER) {
        throw std::runtime_error((F() << "block size order " << int(blk_sz_order) << " is out of range ["
                                      << int(MIN_BLK_SZ_ORDER) << " to " << int(MAX_BLK_SZ_ORDER)
                                      << "] (block sizes of " << (1 << MIN_BLK_SZ_ORDER) << " to "
                                      << (1 << (MAX_BLK_SZ_ORDER - 10)) << "K)")
                                         .str());
    }
}
