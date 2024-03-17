#include "xoz/repo/repository.h"

#include <cstring>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"

namespace {
struct Repository::preload_repo_ctx_t dummy;
}

Repository::Repository(const char* fpath):
        fpath(fpath), fblkarr(fpath, std::bind_front(Repository::preload_repo, dummy)), closed(true) {
    bootstrap_repository();
    assert(not closed);
    assert(fblkarr.begin_blk_nr() >= 1);
}

/*
Repository::Repository(std::stringstream&& mem): fp(mem_fp), closed(true) {
    bootstrap_repository();
    assert(not closed);
    assert(fblkarr.begin_blk_nr() >= 1);
}
*/

Repository::Repository(FileBlockArray&& fblkarr, const GlobalParameters& gp, bool is_a_new_repository):
        fpath(fblkarr.get_file_path()), fblkarr(std::move(fblkarr)), closed(true) {
    if (is_a_new_repository) {
        // The given file block array has a valid and open file but it is not initialized as
        // a repository yet. We do that here.
        _init_new_repository(gp);
    }

    bootstrap_repository();
    assert(not closed);
    assert(fblkarr.begin_blk_nr() >= 1);
}

Repository::~Repository() { close(); }

Repository Repository::create(const char* fpath, bool fail_if_exists, const GlobalParameters& gp) {
    // We pass GlobalParameters gp to the FileBlockArray::create via preload_repo function
    // so the array is created with the correct dimensions.
    // However, no header is written there so resulting file is not a valid repository yet
    struct preload_repo_ctx_t ctx = {false, gp};
    FileBlockArray fblkarr =
            FileBlockArray::create(fpath, std::bind_front(Repository::preload_repo, std::ref(ctx)), fail_if_exists);

    // We delegate the initialization of the new repository to the Repository constructor
    // that it should call _init_new_repository iff ctx.was_file_created
    return Repository(std::move(fblkarr), gp, ctx.was_file_created);
}

Repository Repository::create_mem_based(const GlobalParameters& gp) {
    FileBlockArray fblkarr = FileBlockArray::create_mem_based(gp.blk_sz, 1 /* begin_blk_nr */);

    // Memory based file block arrays (and therefore Repository too) are always created
    // empty and require an initialization (so is_a_new_repository is always true)
    return Repository(std::move(fblkarr), gp, true);
}

void Repository::bootstrap_repository() {
    // During the construction of Repository, in particular of FileBlockArray fblkarr,
    // the block array was initialized so we can read/write extents/header/trailer but we cannot
    // allocate yet.
    assert(not fblkarr.is_closed());
    read_and_check_header();
    read_and_check_trailer(true /* clear_trailer */);

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
    out << "\nRepository size: " << (blk_total_cnt << gp.blk_sz_order) << " bytes, " << blk_total_cnt << " blocks\n"
        << "\nBlock size: " << gp.blk_sz << " bytes (order: " << (uint32_t)gp.blk_sz_order << ")\n"
        << "\nTrailer size: " << trailer_sz << " bytes\n";

    return out;
}

const std::stringstream& Repository::expose_mem_fp() const { return fblkarr.expose_mem_fp(); }

uint32_t Repository::chk_extent_for_rw(bool is_read_op, const Extent& ext, [[maybe_unused]] uint32_t max_data_sz,
                                       [[maybe_unused]] uint32_t start) {
    if (ext.blk_nr() == 0x0) {
        throw NullBlockAccess(F() << "The block 0x00 cannot be " << (is_read_op ? "read" : "written"));
    }  // TODO blk 0x0 but may be 0x1 too

    assert(ext.blk_nr() != 0x0);
    return 0;  // TODO BlockArray::chk_extent_for_rw(is_read_op, ext, max_data_sz, start);
}

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

uint32_t Repository::_grow_by_blocks(uint16_t blk_cnt) { return fblkarr.grow_by_blocks(blk_cnt); }


void Repository::_shrink_by_blocks(uint16_t blk_cnt) { return fblkarr.shrink_by_blocks(blk_cnt); }
