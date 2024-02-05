#pragma once

#include <memory>
#include <tuple>

#include "xoz/blk/block_array.h"
#include "xoz/io/iosegment.h"

class Segment;

/*
 * SegmentBlockArray provides blocks to the caller (BlockArray interface) chopping
 * the space owned by a single segment from an underlying (backend) block array.
 * */
class SegmentBlockArray: public BlockArray {
protected:
    std::tuple<uint32_t, uint16_t> impl_grow_by_blocks(uint16_t ar_blk_cnt) override;

    uint32_t impl_shrink_by_blocks(uint32_t ar_blk_cnt) override;

    uint32_t impl_read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) override;

    uint32_t impl_write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) override;

    uint32_t impl_release_blocks() override;

private:
    Segment& segm;
    uint32_t remain_shrink_sz;

    BlockArray& sg_blkarr;

    std::unique_ptr<IOSegment> sg_io;

    struct SegmentAllocator::req_t default_req;

    uint32_t _impl_shrink_by_blocks(uint32_t ar_blk_cnt, bool release_blocks);

public:
    /*
     * Initialize the SegmentBlockArray object with the given segment that references/owns blocks
     * from the sg_blkarr (the backend).
     *
     * The SegmentBlockArray will chop/cut blocks of blk_sz from the segment's space. If more
     * space is needed, it will allocate more from sg_blkarr (and if less are needed, it will deallocate them).
     * For the alloc/dealloc strategy, see impl_grow_by_blocks() and impl_shrink_by_blocks() internal
     * methods.
     *
     * The sg_blkarr must be previously initialized by the caller, with the extents of the segment marked as
     * allocated.
     * */
    SegmentBlockArray(Segment& segm, BlockArray& sg_blkarr, uint32_t blk_sz);
    ~SegmentBlockArray();

    const IOSegment& expose_mem_fp() const { return *sg_io.get(); }
};
