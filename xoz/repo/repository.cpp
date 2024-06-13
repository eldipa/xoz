#include "xoz/repo/repository.h"

#include <cstring>
#include <filesystem>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"
#include "xoz/mem/endianness.h"
#include "xoz/mem/inet_checksum.h"

struct Repository::preload_repo_ctx_t Repository::dummy = {false, {0}};

Repository::Repository(const char* fpath):
        fpath(fpath),
        fblkarr(std::make_unique<FileBlockArray>(fpath, std::bind_front(Repository::preload_repo, dummy))),
        closed(true) {
    bootstrap_repository();
    assert(not closed);
    assert(this->fblkarr->begin_blk_nr() >= 1);
}

Repository::Repository(std::unique_ptr<FileBlockArray>&& fblkarr, const struct default_parameters_t& defaults,
                       bool is_a_new_repository):
        fpath(fblkarr->get_file_path()), fblkarr(std::move(fblkarr)), closed(true) {
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

Repository Repository::create(const char* fpath, bool fail_if_exists, const struct default_parameters_t& defaults) {
    // Check that the default block size is large enough and valid.
    // The same check will happen in FileBlockArray::create but we do it here because
    // the minimum block size (REPOSITORY_MIN_BLK_SZ) is an extra requirement of us
    // not of FileBlockArray.
    FileBlockArray::fail_if_bad_blk_sz(defaults.blk_sz, 0, REPOSITORY_MIN_BLK_SZ);

    // We pass defaults to the FileBlockArray::create via preload_repo function
    // so the array is created with the correct dimensions.
    // However, no header is written there so resulting file is not a valid repository yet
    struct preload_repo_ctx_t ctx = {false, defaults};
    auto fblkarr_ptr =
            FileBlockArray::create(fpath, std::bind_front(Repository::preload_repo, std::ref(ctx)), fail_if_exists);

    // We delegate the initialization of the new repository to the Repository constructor
    // that it should call init_new_repository iff ctx.was_file_created
    return Repository(std::move(fblkarr_ptr), defaults, ctx.was_file_created);
}

Repository Repository::create_mem_based(const struct default_parameters_t& defaults) {
    // Check that the default block size is large enough and valid.
    // The same check will happen in FileBlockArray::create but we do it here because
    // the minimum block size (REPOSITORY_MIN_BLK_SZ) is an extra requirement of us
    // not of FileBlockArray.
    FileBlockArray::fail_if_bad_blk_sz(defaults.blk_sz, 0, REPOSITORY_MIN_BLK_SZ);

    auto fblkarr_ptr = FileBlockArray::create_mem_based(defaults.blk_sz, 1 /* begin_blk_nr */);

    // Memory based file block arrays (and therefore Repository too) are always created
    // empty and require an initialization (so is_a_new_repository is always true)
    return Repository(std::move(fblkarr_ptr), defaults, true);
}

void Repository::bootstrap_repository() {
    // During the construction of Repository, in particular of FileBlockArray fblkarr,
    // the block array was initialized so we can read/write extents/header/trailer but we cannot
    // allocate yet (we cannot use fblkarr->allocator() yet).
    assert(not fblkarr->is_closed());
    read_and_check_header_and_trailer();

    // Scan which extents/segments are allocated so we can initialize the allocator.
    auto allocated = scan_descriptor_sets();

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


    static_assert(sizeof(hdr.root) >= 32);
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
                                .flags = u8_to_le(0),
                                .feature_flags_compat = u32_to_le(0),
                                .feature_flags_incompat = u32_to_le(0),
                                .feature_flags_ro_compat = u32_to_le(0),
                                .root = {0},
                                .checksum = u16_to_le(0),
                                .padding = {0}};

    write_root_holder(hdr);
    hdr.checksum = u16_to_le(compute_header_checksum(hdr));

    fblkarr->write_header(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
}

void Repository::write_trailer() {
    struct repo_trailer_t eof = {.magic = {'E', 'O', 'F', 0}};
    fblkarr->write_trailer(reinterpret_cast<const char*>(&eof), sizeof(eof));
}

void Repository::init_new_repository(const struct default_parameters_t& defaults) {
    fblkarr->fail_if_bad_blk_sz(defaults.blk_sz, 0, REPOSITORY_MIN_BLK_SZ);

    trampoline_segm = Segment::EmptySegment();
    root_holder = DescriptorSetHolder::create(*fblkarr.get(), idmgr);

    // Ensure that the holder has a valid id.
    root_holder->id(idmgr.request_temporal_id());

    write_header();
    write_trailer();
}

void Repository::close() {
    if (closed)
        return;

    write_header();
    write_trailer();

    fblkarr->close();
    closed = true;
}

void Repository::load_root_holder(struct repo_header_t& hdr) {
    IOSpan root_io(hdr.root, sizeof(hdr.root));

    FileBlockArray& fblkarr_ref = *fblkarr.get();

    if (hdr.flags & 0x80) {
        // The root field in the xoz header contains 2 bytes for the trampoline's content
        // checksum and the segment to the trampoline.
        uint32_t checksum = uint32_t(root_io.read_u16_from_le());
        trampoline_segm = Segment::load_struct_from(root_io);

        // Read trampoline's content. We expect to find a set descriptor holder there.
        auto trampoline_io = IOSegment(fblkarr_ref, trampoline_segm);

        // See if the holder is in the trampoline. Build a shared ptr to DescriptorSetHolder.
        auto dsc = DescriptorSetHolder::load_struct_from(trampoline_io, idmgr, fblkarr_ref);
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
        trampoline_segm = Segment::EmptySegment();

        // The root field has the descriptor set holder
        auto dsc = DescriptorSetHolder::load_struct_from(root_io, idmgr, fblkarr_ref);
        root_holder = Descriptor::cast<DescriptorSetHolder>(dsc);
    }
}

void Repository::write_root_holder(struct repo_header_t& hdr) {
    IOSpan root_io(hdr.root, sizeof(hdr.root));
    root_holder->update_header();

    FileBlockArray& fblkarr_ref = *fblkarr.get();

    bool trampoline_required = root_holder->calc_struct_footprint_size() > HEADER_ROOT_SET_SZ;

    if (trampoline_required) {
        // Expand/shrink the trampoline space to make room for the
        // root descriptor set holder (with its space was updated/recalculated
        // in the above call to update_header())
        update_trampoline_space();

        // Write the holder in the trampoline
        auto trampoline_io = IOSegment(fblkarr_ref, trampoline_segm);
        root_holder->write_struct_into(trampoline_io);

        // Write in the xoz file header the checksum of the trampoline
        // and the segment that points to.
        root_io.write_u16_to_le(inet_to_u16(root_holder->checksum));
        trampoline_segm.write_struct_into(root_io);

        hdr.flags |= uint8_t((trampoline_required) << 7);
    } else {
        // No trampoline required, release/dealloc it if we have one
        if (trampoline_segm.length() != 0) {
            fblkarr_ref.allocator().dealloc(trampoline_segm);
            trampoline_segm.clear();
        }

        root_holder->write_struct_into(root_io);

        hdr.flags &= uint8_t(0x7f);
    }
}

void Repository::update_trampoline_space() {
    uint32_t cur_sz = trampoline_segm.calc_struct_footprint_size();
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

    // TODO what happen if trampoline_segm->calc_struct_footprint_size() > sizeof(hdr.root)?
    // (aka, it still does not fit?) We need to ensure that the call to alloc()/realloc()
    // above does not fragment too much the allocation so the trampoline_segm fits in the header
    trampoline_segm.add_end_of_segment();
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
    // TODO: is blk_sz_order > 16 a good upper limit? It seems a little artificial.
    if (blk_sz_order < REPOSITORY_MIN_BLK_SZ_ORDER or blk_sz_order > 16) {
        throw std::runtime_error((F() << "block size order " << int(blk_sz_order)
                                      << " is out of range [7 to 16] (block sizes of 128 to 64K).")
                                         .str());
    }
}
