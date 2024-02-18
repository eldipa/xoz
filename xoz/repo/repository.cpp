#include "xoz/repo/repository.h"

#include <cstring>
#include <utility>

#include "xoz/err/exceptions.h"

Repository::Repository(const char* fpath, uint64_t phy_repo_start_pos): fpath(fpath), fp(disk_fp), closed(true) {
    open(fpath, phy_repo_start_pos);
    assert(not closed);
    assert(blk_total_cnt >= 1);

    _init_repository();
}

Repository::Repository(std::stringstream&& mem, uint64_t phy_repo_start_pos): fp(mem_fp), closed(true) {
    open_internal(Repository::IN_MEMORY_FPATH, std::move(mem), phy_repo_start_pos);
    assert(not closed);
    assert(blk_total_cnt >= 1);

    _init_repository();
}

Repository::~Repository() { close(); }

void Repository::_init_repository() {
    // Initialize BlockArray first
    initialize_block_array(gp.blk_sz, 1, blk_total_cnt);

    // Load the root set.
    // NOTE: if a single descriptor tries to allocate blocks from the array
    // it will crash because the allocator is not initialized yet and we cannot do it
    // because we need to scan the sets to find which extents/segments are already
    // allocated.
    root_dset = std::make_shared<DescriptorSet>(this->root_sg, *this, *this, idmgr);
    root_dset->load_set();

    // Scan which extents/segments are allocated so we can initialize the allocator.
    auto allocated = scan_descriptor_sets();
    allocator().initialize_from_allocated(allocated);
}

std::ostream& Repository::print_stats(std::ostream& out) const {
    out << "XOZ Repository\n"
           "File: '"
        << fpath
        << "' "
           "[start pos: "
        << phy_repo_start_pos << ", end pos: " << phy_repo_end_pos
        << "]\n"
           "File status: ";

    if (closed) {
        out << "closed\n";
    } else {
        out << "open [fail: " << fp.fail() << ", bad: " << fp.bad() << ", eof: " << fp.eof() << ", good: " << fp.good()
            << "]\n";
    }

    out << "\nRepository size: " << (blk_total_cnt << gp.blk_sz_order) << " bytes, " << blk_total_cnt << " blocks\n"
        << "\nBlock size: " << gp.blk_sz << " bytes (order: " << (uint32_t)gp.blk_sz_order << ")\n"
        << "\nTrailer size: " << trailer_sz << " bytes\n";

    return out;
}

const std::stringstream& Repository::expose_mem_fp() const {
    if (std::addressof(fp) == std::addressof(disk_fp)) {
        throw std::runtime_error("The repository is not memory backed.");
    }

    return mem_fp;
}

uint32_t Repository::chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start) {
    if (ext.blk_nr() == 0x0) {
        throw NullBlockAccess(F() << "The block 0x00 cannot be " << (is_read_op ? "read" : "written"));
    }

    assert(ext.blk_nr() != 0x0);
    return BlockArray::chk_extent_for_rw(is_read_op, ext, max_data_sz, start);
}

void Repository::impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    seek_read_blk(blk_nr, offset);
    fp.read(buf, exact_sz);
}

void Repository::impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    seek_write_blk(blk_nr, offset);
    fp.write(buf, exact_sz);
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

    auto blkarr_st = BlockArray::stats();
    memcpy(&st.blkarr_st, &blkarr_st, sizeof(st.blkarr_st));

    return st;
}
