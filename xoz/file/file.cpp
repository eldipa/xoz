#include "xoz/file/file.h"

#include <cstring>
#include <filesystem>
#include <utility>

#include "xoz/dsc/id_mapping.h"
#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"
#include "xoz/mem/endianness.h"
#include "xoz/mem/inet_checksum.h"

namespace xoz {
struct File::preload_file_ctx_t File::dummy = {false, {0}};

File::File(const DescriptorMapping& dmap, const char* fpath, const struct runtime_config_t& runcfg):
        fpath(fpath),
        fblkarr(std::make_unique<FileBlockArray>(fpath, std::bind_front(File::preload_file, dummy))),
        closed(true),
        closing(false),
        rctx(dmap, runcfg),
        trampoline_segm(fblkarr->blk_sz_order()) {
    bootstrap_file();
    assert(not closed);
    assert(this->fblkarr->begin_blk_nr() >= 1);
}

File::File(const DescriptorMapping& dmap, std::unique_ptr<FileBlockArray>&& fblkarr_,
           const struct default_parameters_t& defaults, bool is_a_new_file, const struct runtime_config_t& runcfg):
        fpath(fblkarr_->get_file_path()),
        fblkarr(std::move(fblkarr_)),
        closed(true),
        closing(false),
        rctx(dmap, runcfg),
        trampoline_segm(fblkarr->blk_sz_order()) {
    if (is_a_new_file) {
        // The given file block array has a valid and open file but it is not initialized as
        // a xoz file yet. We do that here.
        init_new_file(defaults);
    }

    bootstrap_file();
    assert(not closed);
    assert(this->fblkarr->begin_blk_nr() >= 1);
}

File::~File() { close(); }

File File::create(const DescriptorMapping& dmap, const char* fpath, bool fail_if_exists,
                  const struct default_parameters_t& defaults, const struct runtime_config_t& runcfg) {
    // Check that the default block size is large enough and valid.
    // The same check will happen in FileBlockArray::create but we do it here because
    // the minimum block size (MIN_BLK_SZ) is an extra requirement of us
    // not of FileBlockArray.
    FileBlockArray::fail_if_bad_blk_sz(defaults.blk_sz, 0, MIN_BLK_SZ);

    // We pass defaults to the FileBlockArray::create via preload_file function
    // so the array is created with the correct dimensions.
    // However, no header is written there so resulting file is not a valid xoz file yet
    struct preload_file_ctx_t ctx = {false, defaults};
    auto fblkarr_ptr =
            FileBlockArray::create(fpath, std::bind_front(File::preload_file, std::ref(ctx)), fail_if_exists);

    // We delegate the initialization of the new xoz file to the File constructor
    // that it should call init_new_file iff ctx.was_file_created
    return File(dmap, std::move(fblkarr_ptr), defaults, ctx.was_file_created, runcfg);
}

File File::create_mem_based(const DescriptorMapping& dmap, const struct default_parameters_t& defaults,
                            const struct runtime_config_t& runcfg) {
    // Check that the default block size is large enough and valid.
    // The same check will happen in FileBlockArray::create but we do it here because
    // the minimum block size (MIN_BLK_SZ) is an extra requirement of us
    // not of FileBlockArray.
    FileBlockArray::fail_if_bad_blk_sz(defaults.blk_sz, 0, MIN_BLK_SZ);

    auto fblkarr_ptr = FileBlockArray::create_mem_based(defaults.blk_sz, 1 /* begin_blk_nr */);

    // Memory based file block arrays (and therefore File too) are always created
    // empty and require an initialization (so is_a_new_file is always true)
    return File(dmap, std::move(fblkarr_ptr), defaults, true, runcfg);
}

void File::bootstrap_file() {
    // During the construction of File, in particular of FileBlockArray fblkarr,
    // the block array was initialized so we can read/write extents/header/trailer but we cannot
    // allocate yet (we cannot use fblkarr->allocator() yet).
    assert(not fblkarr->is_closed());
    read_and_check_header_and_trailer();

    // Scan which extents/segments are allocated so we can initialize the allocator.
    auto allocated = collect_allocated_segments_of_descriptors();

    // Add the trampoline segment, if any
    if (trampoline_segm.length()) {
        allocated.push_back(trampoline_segm);
    }

    // With this, we can do alloc/dealloc and the File is fully operational.
    fblkarr->allocator().initialize_from_allocated(allocated);

    // Now that the root set, its subsets and all descriptors were loaded
    // and the allocator is fully operational, let the descriptors know
    // that we are ready
    notify_load_to_all_descriptors();

    closed = false;
}

const std::stringstream& File::expose_mem_fp() const { return fblkarr->expose_mem_fp(); }

std::list<Segment> File::collect_allocated_segments_of_descriptors() const {
    std::list<Segment> allocated;
    allocated.push_back(root_set->segment());

    DescriptorSet::depth_first_for_each_set(*root_set, [&allocated](const DescriptorSet* dset) {
        for (auto it = dset->cbegin(); it != dset->cend(); ++it) {
            auto& dsc(*it);
            if (dsc->does_own_content()) {
                allocated.push_back(dsc->content_segment_ref());
            }
        }
    });

    return allocated;
}

void File::notify_load_to_all_descriptors() {
    DescriptorSet::depth_first_for_each_set(*root_set, [this](DescriptorSet* s) {
        for (auto it = s->begin(); it != s->end(); ++it) {
            (*it)->on_after_load(root_set);
        }
    });
}

struct File::stats_t File::stats() const {
    struct stats_t st;

    auto fblkarr_st = fblkarr->stats();
    memcpy(&st.fblkarr_stats, &fblkarr_st, sizeof(st.fblkarr_stats));

    auto allocator_st = fblkarr->allocator().stats();
    memcpy(&st.allocator_stats, &allocator_st, sizeof(st.allocator_stats));

    st.capacity_file_sz = (fblkarr->capacity() + fblkarr->begin_blk_nr()) << fblkarr->blk_sz_order();
    st.in_use_file_sz = (fblkarr->blk_cnt() + fblkarr->begin_blk_nr()) << fblkarr->blk_sz_order();

    st.capacity_file_sz += fblkarr->trailer_sz();
    st.in_use_file_sz += fblkarr->trailer_sz();

    st.capacity_file_sz_kb = double(st.capacity_file_sz) / double(1024.0);
    st.in_use_file_sz_kb = double(st.in_use_file_sz) / double(1024.0);

    st.in_use_file_sz_rel = (st.capacity_file_sz == 0) ? 0 : (double(st.in_use_file_sz) / double(st.capacity_file_sz));

    st.header_sz = fblkarr->header_sz();
    st.trailer_sz = fblkarr->trailer_sz();
    return st;
}

void PrintTo(const File& xfile, std::ostream* out) {
    struct File::stats_t st = xfile.stats();
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << "File:              " << std::setfill(' ') << std::setw(12) << xfile.fblkarr->get_file_path() << "\n";

    auto& fp = xfile.fblkarr->phy_file_stream();
    (*out) << "Status:            ";
    if (xfile.fblkarr->is_closed()) {
        (*out) << std::setfill(' ') << std::setw(12) << "closed\n\n";
    } else {
        (*out) << "        open "
               << "[fail: " << fp.fail() << ", bad: " << fp.bad() << ", eof: " << fp.eof() << ", good: " << fp.good()
               << "]\n\n";
    }

    (*out) << "-- File -----------------\n"
           << "Capacity:          " << std::setfill(' ') << std::setw(12) << st.capacity_file_sz_kb << " kb\n"
           << "In use:            " << std::setfill(' ') << std::setw(12) << st.in_use_file_sz_kb << " kb ("
           << std::setfill(' ') << std::setw(5) << std::fixed << std::setprecision(2) << (st.in_use_file_sz_rel * 100)
           << "%)\n"
           << " - Header:         " << std::setfill(' ') << std::setw(12) << st.header_sz << " bytes\n"
           << " - Trailer:        " << std::setfill(' ') << std::setw(12) << st.trailer_sz << " bytes\n"
           << "\n";

    (*out) << "-- Block Array ----------------\n"
           << (*xfile.fblkarr) << "\n"
           << "\n"
           << "-- Allocator ------------------\n"
           << xfile.fblkarr->allocator() << "\n";

    out->flags(ioflags);
}

std::ostream& operator<<(std::ostream& out, const File& xfile) {
    PrintTo(xfile, &out);
    return out;
}


void File::preload_file(struct File::preload_file_ctx_t& ctx, std::istream& is,
                        struct FileBlockArray::blkarr_cfg_t& cfg, bool on_create) {
    if (on_create) {
        cfg.blk_sz = ctx.defaults.blk_sz;
        cfg.begin_blk_nr = 1;

        ctx.was_file_created = true;
        return;
    }

    struct file_header_t hdr;
    is.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    check_header_magic(hdr);
    compute_and_check_header_checksum(hdr);

    uint8_t blk_sz_order = u8_from_le(hdr.blk_sz_order);
    check_blk_sz_order(blk_sz_order);

    cfg.blk_sz = (1 << blk_sz_order);
    cfg.begin_blk_nr = 1;

    return;
}

void File::read_and_check_header_and_trailer() {
    struct file_header_t hdr;

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
        throw InconsistentXOZ(*this, "the xoz file has incompatible features.");
    }

    if (feature_flags_ro_compat) {
        // TODO implement read-only mode
        throw InconsistentXOZ(
                *this,
                "the xoz file has read-only compatible features and the xoz file was not open in read-only mode.");
    }

    uint8_t blk_sz_order = u8_from_le(hdr.blk_sz_order);
    check_blk_sz_order(blk_sz_order);

    uint32_t blk_sz = (1 << blk_sz_order);


    auto blk_total_cnt = u32_from_le(hdr.blk_total_cnt);
    if (blk_total_cnt == 0) {
        throw InconsistentXOZ(*this, "the xoz file has a declared block total count of zero.");
    }

    // Calculate the xoz file size based on the block count.
    uint64_t file_sz = blk_total_cnt << blk_sz_order;

    // Read the declared xoz file size from the header and
    // check that it matches with what we calculated
    uint64_t file_sz_read = u64_from_le(hdr.file_sz);
    if (file_sz != file_sz_read) {
        throw InconsistentXOZ(*this, F() << "the xoz file declared a size of " << file_sz_read
                                         << " bytes but it is expected to have " << file_sz
                                         << " bytes based on the declared block total count " << blk_total_cnt
                                         << " and block size " << blk_sz << ".");
    }


    /*
     * TODO rewrite these checks
    if (file_sz > fp_end) {
        throw InconsistentXOZ(*this,
                              F() << "the xoz file has a declared size (" << file_sz
                                  << ") starting at "
                                  //<< phy_file_start_pos << " offset this gives an expected end of "
                                  // TODO<< phy_file_end_pos << " which goes beyond the physical file end at " << fp_end
                                  << ".");
    }

    if (fp_end > phy_file_end_pos) {
        // More real bytes than the ones in the xfile
        // Perhaps an incomplete shrink/truncate?
    }
    assert(fp_end >= phy_file_end_pos);
    */


    load_root_set(hdr);

    // TODO this *may* still be useful: assert(phy_file_end_pos > 0);

    uint16_t trailer_sz = u16_from_le(hdr.trailer_sz);
    if (uint64_t(trailer_sz) < sizeof(struct file_trailer_t)) {
        throw InconsistentXOZ(*this, F() << "the declared trailer size (" << trailer_sz
                                         << " bytes) is too small, required at least " << sizeof(struct file_trailer_t)
                                         << " bytes.");
    }

    if (uint64_t(trailer_sz) != fblkarr->trailer_sz()) {
        throw InconsistentXOZ(*this, F() << "mismatch between the declared trailer size (" << trailer_sz
                                         << " bytes) and the real trailer read from the file (" << fblkarr->trailer_sz()
                                         << " bytes).");
    }

    struct file_trailer_t eof;
    fblkarr->read_trailer(reinterpret_cast<char*>(&eof), sizeof(eof));

    if (strncmp(reinterpret_cast<const char*>(&eof.magic), "EOF", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'EOF' not found in the trailer.");
    }
}

void File::write_header() {

    // Write the root set in the buffer. This *may* trigger an (de)allocation
    // in the fblkarr if the use of a trampoline is required or not.
    //
    // Caller *MUST* call root_set->update_header() or full_sync() before calling write_header()
    // so we can be sure that all the descriptor sets (including the root) are up to date
    // and the set descriptor has the latest updated sizes.
    uint8_t rootbuf[HEADER_ROOT_SET_SZ] = {0};
    uint8_t flags = 0;
    write_root_set(rootbuf, HEADER_ROOT_SET_SZ, flags);

    // Despite that close() should be doing a release() of any free block,
    // the write_root_set() may had deallocated stuff making free
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
    uint16_t trailer_sz = assert_u16(sizeof(struct file_trailer_t));

    // note: we declare that the xoz file has the same block count
    // than the file block array *plus* its begin blk number to count
    // for the array's header (where the xfile's header will be written into)
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

    struct file_header_t hdr = {.magic = {'X', 'O', 'Z', 0},
                                .app_name = {0},
                                .file_sz = u64_to_le(blk_total_cnt << fblkarr->blk_sz_order()),
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

void File::write_trailer() {
    struct file_trailer_t eof = {.magic = {'E', 'O', 'F', 0}};
    fblkarr->write_trailer(reinterpret_cast<const char*>(&eof), sizeof(eof));
}

void File::init_new_file(const struct default_parameters_t& defaults) {
    fblkarr->fail_if_bad_blk_sz(defaults.blk_sz, 0, MIN_BLK_SZ);

    trampoline_segm = Segment::EmptySegment(fblkarr->blk_sz_order());
    root_set = DescriptorSet::create(*fblkarr.get(), rctx);

    // Ensure that the descriptor set has a valid id.
    root_set->id(rctx.idmgr.request_temporal_id());


    // Write any pending write (it should be a few if any due the initialization
    // of the set's structures. Update the root but do not
    // try to release any free space, it should be none (and because fblkarr's allocator
    // is not fully initialized yet).
    //
    // This must be called before write_header() so we can by 100% sure of how
    // many blocks are being used and how large the root set is and if it
    // fits in the header or not.
    //
    // Note: it is important that the root set does not do any allocation
    // because fblkarr is not fully initialized yet.
    // In theory we should be fine because root set does not require
    // alloc any space for an empty set (the initial state of any new xoz file)
    // and neither write_header nor write_trailer requires alloc space
    // (write_trailer will not try to alloc space for a trampoline because the
    // root set of an empty set should fit in the header).
    // Once we call bootstrap_file() we should be fine.
    root_set->full_sync(false);

    write_header();
    write_trailer();
}

void File::full_sync(const bool release) {
    // Sync internal/private objects so they will update their descriptors
    // (hence, writing to disk). We need to do this before sync'ing the root set
    // because otherwise the set may contain outdated descriptors.
    full_sync_metadata();

    // Update the root set. If there is any pending write, this will
    // do it. This may trigger some allocations in fblkarr and if release
    // is true, it may trigger some deallocations (shrinks) too.
    root_set->full_sync(release);
    if (release) {
        fblkarr->allocator().release();
    }

    write_header();
    write_trailer();
}

void File::close() {
    if (closed)
        return;

    closing = true;
    full_sync(true);

    fblkarr->close();
    closed = true;
    closing = false;
}

void File::panic_close() {
    if (closed)
        return;

    fblkarr->panic_close();
    closed = true;
    closing = false;
}

void File::full_sync_metadata() {
    // Make the objects flush their state into the descriptors.
    // There is no need to flush/sync the descriptors themselves because
    // they should belong to a set and the sets will be flushed/synced
    // in File::full_sync();
    // Note: the exceptions are the descriptors that were explicitly not
    // added to any set. In this case their writings will be lost.
    if (rctx.runcfg.file.keep_index_updated) {
        rctx.index.flush(idmap);
    }
}

void File::load_root_set(struct file_header_t& hdr) {
    IOSpan root_io(hdr.root, sizeof(hdr.root));

    FileBlockArray& fblkarr_ref = *fblkarr.get();

    if (hdr.flags & 0x80) {
        // The root field in the xoz header contains 2 bytes for the trampoline's content
        // checksum and the segment to the trampoline.
        uint32_t checksum = uint32_t(root_io.read_u16_from_le());
        trampoline_segm = Segment::load_struct_from(root_io, fblkarr_ref.blk_sz_order());

        // Read trampoline's content. We expect to find a set descriptor there.
        auto trampoline_io = IOSegment(fblkarr_ref, trampoline_segm);

        // See if the set descriptor is in the trampoline. Build a shared ptr to DescriptorSet.
        auto dsc = DescriptorSet::load_struct_from(trampoline_io, rctx, fblkarr_ref);
        root_set = Descriptor::cast<DescriptorSet>(dsc);

        // Check that trampoline's content checksum is correct.
        auto checksum_check = fold_inet_checksum(inet_remove(checksum, root_set->checksum));
        if (not is_inet_checksum_good(checksum_check)) {
            throw InconsistentXOZ((F() << "Root descriptor set trampoline checksum failed:"
                                       << "computed " << std::hex << checksum << " but expected " << std::hex
                                       << root_set->checksum << " (chk " << std::hex << checksum_check << ")")
                                          .str());
        }
    } else {
        // No trampoline
        trampoline_segm = Segment::EmptySegment(fblkarr_ref.blk_sz_order());

        // The root field has the descriptor set
        auto dsc = DescriptorSet::load_struct_from(root_io, rctx, fblkarr_ref);
        root_set = Descriptor::cast<DescriptorSet>(dsc);
    }

    load_private_metadata_from_root_set();
}

void File::write_root_set(uint8_t* rootbuf, const uint32_t rootbuf_sz, uint8_t& flags) {
    assert(rootbuf_sz <= HEADER_ROOT_SET_SZ);
    IOSpan root_io(rootbuf, rootbuf_sz);

    FileBlockArray& fblkarr_ref = *fblkarr.get();

    bool trampoline_required = root_set->calc_struct_footprint_size() > HEADER_ROOT_SET_SZ;

    if (trampoline_required) {
        // Expand/shrink the trampoline space to make room for the
        // root descriptor set (with its space was updated/recalculated
        // in the call to root_set->update_header() or full_sync() made by the caller)
        update_trampoline_space();

        // Write the set descriptor in the trampoline
        auto trampoline_io = IOSegment(fblkarr_ref, trampoline_segm);
        root_set->write_struct_into(trampoline_io, rctx);

        // Write in the xoz file header the checksum of the trampoline
        // and the segment that points to.
        root_io.write_u16_to_le(inet_to_u16(root_set->checksum));
        trampoline_segm.write_struct_into(root_io);

        flags |= uint8_t((trampoline_required) << 7);
    } else {
        // No trampoline required, release/dealloc it if we have one
        if (trampoline_segm.length() != 0) {
            fblkarr_ref.allocator().dealloc(trampoline_segm);
            trampoline_segm.clear();
        }

        root_set->write_struct_into(root_io, rctx);

        flags &= uint8_t(0x7f);
    }
}

void File::update_trampoline_space() {
    uint32_t cur_sz = trampoline_segm.calc_data_space_size();
    uint32_t req_sz = root_set->calc_struct_footprint_size();
    assert(req_sz > 0);

    const bool should_expand = (cur_sz < req_sz);
    const bool should_shrink = ((cur_sz >> 1) >= req_sz);
    if (should_expand or should_shrink) {
        if (trampoline_segm.length() == 0) {
            trampoline_segm = fblkarr->allocator().alloc(req_sz);
        } else {
            // Do not call realloc and instead, call dealloc + alloc.
            // The rationale is that realloc will try to expand (or shrink)
            // in place the segment, minimizing the needed copy of the reallocated data
            // However, we are going to override the space anyways so this
            // minimization is pointless and forces an unnecessary more
            // inefficient allocation.
            fblkarr->allocator().dealloc(trampoline_segm);
            trampoline_segm = fblkarr->allocator().alloc(req_sz);
        }
    }

    // ensure the trampoline segment has an end so we can load it correctly
    // in load_root_set()
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

void File::load_private_metadata_from_root_set() {
    xoz_assert("IDMappingDescriptor already loaded", not idmap);
    std::shared_ptr<IDMappingDescriptor> idmap_tmp;

    // Search for the private descriptors that contain xoz-specific metadata
    for (auto it = root_set->begin(); it != root_set->end(); ++it) {
        idmap_tmp = Descriptor::cast<IDMappingDescriptor>(*it, true);
        if (idmap_tmp) {
            if (not idmap) {
                idmap = idmap_tmp;
            } else {
                throw InconsistentXOZ("IDMappingDescriptor (index data) found duplicated.");
            }
        }
    }

    // Create default descriptors if they were not found earlier
    if (not idmap) {
        auto dsc = IDMappingDescriptor::create(*fblkarr);
        if (rctx.runcfg.file.keep_index_updated) {
            auto id = root_set->add(std::move(dsc));
            idmap = root_set->get<IDMappingDescriptor>(id);
        } else {
            idmap = std::shared_ptr<IDMappingDescriptor>(std::move(dsc));
        }
    }

    // Initialize the index
    rctx.index.init_index(*root_set, idmap);
}

void File::check_header_magic(struct file_header_t& hdr) {
    if (strncmp(reinterpret_cast<const char*>(&hdr.magic), "XOZ", 4) != 0) {
        throw std::runtime_error("magic string 'XOZ' not found in the header.");
    }
}

uint16_t File::compute_header_checksum(struct file_header_t& hdr) {
    // Compute the checksum of the header. The field 'checksum' must be temporally
    // zeroed to do the computation
    uint16_t stored_checksum = hdr.checksum;
    hdr.checksum = 0;

    uint32_t checksum = inet_checksum(reinterpret_cast<uint8_t*>(&hdr), sizeof(hdr));
    hdr.checksum = stored_checksum;

    return inet_to_u16(checksum);
}

void File::compute_and_check_header_checksum(struct file_header_t& hdr) {
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

void File::check_blk_sz_order(const uint8_t blk_sz_order) {
    if (blk_sz_order < MIN_BLK_SZ_ORDER or blk_sz_order > MAX_BLK_SZ_ORDER) {
        throw std::runtime_error((F() << "block size order " << int(blk_sz_order) << " is out of range ["
                                      << int(MIN_BLK_SZ_ORDER) << " to " << int(MAX_BLK_SZ_ORDER)
                                      << "] (block sizes of " << (1 << MIN_BLK_SZ_ORDER) << " to "
                                      << (1 << (MAX_BLK_SZ_ORDER - 10)) << "K)")
                                         .str());
    }
}
}  // namespace xoz
