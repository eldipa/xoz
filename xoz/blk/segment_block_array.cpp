#include "xoz/blk/segment_block_array.h"

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/io/iosegment.h"
#include "xoz/segm/segment.h"

// Note:
//  - ar_blk_cnt means blk count in terms of the array's blocks
//  - sg_blk_cnt means blk count in terms of the segment's blocks
std::tuple<uint32_t, uint16_t> SegmentBlockArray::impl_grow_by_blocks(uint16_t ar_blk_cnt) {
    // How many bytes are those?
    uint32_t grow_sz = ar_blk_cnt << blk_sz_order();

    // Tiny grows are not good in general because each tiny grow requires
    // at least 1 extent so multiple grows enlarge the segment (higher footprint)
    //
    // However, for simplicity, we are not going to apply any rule and let
    // the caller to do a cleaver strategy for us.
    //
    // At the same time we are not going to do any round up of the grow size:
    // we are going to let the allocator to do it for ourselves. This works
    // well because the allocator has the inline disabled so we can merge
    // the segments (ours and the new one) flawless.

    // Allocate a segment to extend the current one
    auto additional_segm = sg_blkarr.allocator().alloc(grow_sz, default_req);
    segm.extend(additional_segm);

    // Create a new io segment because the underlying segment changed
    // (like when we need to create a new iterator if the container changed)
    delete sg_io;
    sg_io = new IOSegment(sg_blkarr, segm);

    return {past_end_blk_nr(), ar_blk_cnt};
}

uint32_t SegmentBlockArray::impl_shrink_by_blocks(uint32_t ar_blk_cnt) {
    // How many bytes are those?
    uint32_t shrink_sz = (ar_blk_cnt << blk_sz_order()) + remain_shrink_sz;
    uint32_t shrank_sz = 0;

    Segment sg_to_free;
    while (shrink_sz > 0) {
        assert(segm.ext_cnt() >= 1);

        auto sg_last_ext = segm.exts().back();
        uint32_t alloc_sz = sg_last_ext.calc_data_space_size(sg_blkarr.blk_sz_order());

        if (alloc_sz <= shrink_sz) {
            sg_to_free.add_extent(sg_last_ext);
            segm.remove_last_extent();
            shrink_sz -= alloc_sz;
            shrank_sz += alloc_sz;
        } else {
            const auto quarter = alloc_sz >> 2;
            const auto half = alloc_sz >> 1;

            if (shrink_sz >= quarter + half and not sg_last_ext.is_suballoc() and sg_last_ext.blk_cnt() >= 2) {
                // ok, the extent will get empty by 75% or more so it makes sense
                // to split it by half and free 50% and keep the other half
                // at 25% empty at least.
                //
                // Note: extent for suballocation are not taken into account.
                // The reasoning is that if we remove some (but not all) of the subblocks
                // of the for-suballocation extent, we are not shrinking the size
                // of the segment segm and we are releasing a very small amount of data.
                // It is better to try to get rid them off entirely and not piece by piece.
                //
                // The same goes for the requirement of sg_last_ext.blk_cnt() >= 2, the idea
                // is that we want to release a significant chunk to worth the effort of splitting

                const uint16_t non_free_blk_cnt = (sg_last_ext.blk_cnt() >> 1) + (sg_last_ext.blk_cnt() % 2);
                auto sg_ext2 = sg_last_ext.split(non_free_blk_cnt);  // non-free: 50% or more
                sg_to_free.add_extent(sg_ext2);
                // no need of this: segm.remove_last_extent(); split works in place

                uint32_t alloc_sz2 = sg_ext2.calc_data_space_size(sg_blkarr.blk_sz_order());
                shrink_sz -= alloc_sz2;
                shrank_sz += alloc_sz2;
            }

            // ok, shrink_sz is non zero but we cannot release anything else
            break;
        }
    }

    remain_shrink_sz = shrink_sz;
    assert(remain_shrink_sz % blk_sz() == 0);  // TODO not sure in general (it will work in practice)

    if (sg_to_free.ext_cnt() >= 1) {
        sg_blkarr.allocator().dealloc(sg_to_free);
        delete sg_io;
        sg_io = new IOSegment(sg_blkarr, segm);

        assert(shrank_sz > 0);
        assert(shrank_sz % blk_sz() == 0);
        return shrank_sz >> blk_sz_order();
    }

    assert(shrank_sz == 0);
    return 0;
}

uint32_t SegmentBlockArray::impl_read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t to_read_sz = chk_extent_for_rw(true, ext, max_data_sz, start);
    if (to_read_sz == 0) {
        return 0;
    }

    sg_io->seek_rd((ext.blk_nr() << blk_sz_order()) + start);
    sg_io->readall(data, to_read_sz);
    return to_read_sz;
}

uint32_t SegmentBlockArray::impl_write_extent(const Extent& ext, const char* data, uint32_t max_data_sz,
                                              uint32_t start) {
    const uint32_t to_write_sz = chk_extent_for_rw(false, ext, max_data_sz, start);
    if (to_write_sz == 0) {
        return 0;
    }

    sg_io->seek_wr((ext.blk_nr() << blk_sz_order()) + start);
    sg_io->writeall(data, to_write_sz);
    return to_write_sz;
}


SegmentBlockArray::SegmentBlockArray(Segment& segm, BlockArray& sg_blkarr, uint32_t blk_sz):
        BlockArray(), segm(segm), remain_shrink_sz(0), sg_blkarr(sg_blkarr) {
    if (blk_sz == 0) {
        throw "";
    }

    if (sg_io->remain_rd() % blk_sz != 0) {
        throw "";
    }

    if (segm.inline_data_sz() != 0) {
        throw "";
    }

    sg_io = new IOSegment(sg_blkarr, segm);

    default_req = sg_blkarr.allocator().get_default_alloc_requirements();
    default_req.max_inline_sz = 0;

    initialize_block_array(blk_sz, 0, sg_io->remain_rd() / blk_sz);
}

SegmentBlockArray::~SegmentBlockArray() { delete sg_io; }
