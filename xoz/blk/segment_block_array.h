#pragma once

#include <memory>
#include <tuple>

#include "xoz/blk/block_array.h"
#include "xoz/io/iosegment.h"

namespace xoz {
class Segment;

/*
 * SegmentBlockArray provides blocks to the caller (BlockArray interface) chopping
 * the space owned by a single segment from an underlying (backend) block array.
 * */
class SegmentBlockArray: public BlockArray {
protected:
    std::tuple<uint32_t, uint16_t> impl_grow_by_blocks(uint16_t fg_blk_cnt) override;

    uint32_t impl_shrink_by_blocks(uint32_t fg_blk_cnt) override;

    uint32_t impl_release_blocks() override;

    void impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) override;

    void impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) override;

private:
    Segment& segm;

    BlockArray& bg_blkarr;

    std::unique_ptr<IOSegment> bg_io;

    struct SegmentAllocator::req_t default_req;

    uint32_t flags;

    uint32_t _impl_shrink_by_blocks(uint32_t fg_blk_cnt, bool release_blocks);

public:
    /*
     * Initialize the SegmentBlockArray object with the given segment that references/owns blocks
     * from the bg_blkarr (the backend).
     *
     * The SegmentBlockArray will chop/cut blocks of blk_sz from the segment's space. If more
     * space is needed, it will allocate more from bg_blkarr (and if less are needed, it will deallocate them).
     * For the alloc/dealloc strategy, see impl_grow_by_blocks() and impl_shrink_by_blocks() internal
     * methods.
     *
     * The bg_blkarr must be previously initialized by the caller, with the extents of the segment marked as
     * allocated.
     * */
    SegmentBlockArray(Segment& segm, BlockArray& bg_blkarr, uint32_t blk_sz, uint32_t flags);
    ~SegmentBlockArray();

    const IOSegment& expose_mem_fp() const { return *bg_io.get(); }
};
}  // namespace xoz
