#include "xoz/repo/repository.h"

#include <cstring>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"

Repository::Repository(const char* fpath): fpath(fpath), fp(disk_fp), closed(true) {
    std::stringstream ignored;
    open_internal(fpath, std::move(ignored));
    assert(not closed);
    assert(blk_total_cnt >= 1);
}

Repository::Repository(std::stringstream&& mem): fp(mem_fp), closed(true) {
    open_internal(Repository::IN_MEMORY_FPATH, std::move(mem));
    assert(not closed);
    assert(blk_total_cnt >= 1);
}

Repository::~Repository() { close(); }

void Repository::bootstrap_repository() {
    // Initialize BlockArray first. Now we can read/write blocks/extents (but we cannot alloc yet)
    initialize_block_array(gp.blk_sz, 1, blk_total_cnt);

    // Let's load the root descriptor set.
    // So far we have the root segment loaded in root_sg *but*
    // if the particular setting is set, assume that the recently loaded root_sg is
    // not the root segment but a single extent that points a block(s) with the real
    // root segment.
    // This additional indirection allow us to encode large root segments outside the header.
    if (root_sg.inline_data_sz() == 4 and root_sg.ext_cnt() == 1) {
        external_root_sg_loc.add_extent(root_sg.exts()[0]);

        IOSegment io2(*this, external_root_sg_loc);
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
    root_dset = std::make_shared<DescriptorSet>(this->root_sg, *this, *this, idmgr);
    root_dset->load_set();

    // Scan which extents/segments are allocated so we can initialize the allocator.
    auto allocated = scan_descriptor_sets();

    // With this, we can do alloc/dealloc and the Repository is fully operational.
    allocator().initialize_from_allocated(allocated);
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
