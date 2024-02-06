#include "xoz/blk/segment_block_array.h"

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/io/iosegment.h"
#include "xoz/segm/segment.h"

// Note:
//  - ar_blk_cnt means blk count in terms of the array's blocks (public API)
//  - sg_blk_cnt means blk count in terms of the segment's blocks (internal implementation API)
std::tuple<uint32_t, uint16_t> SegmentBlockArray::impl_grow_by_blocks(uint16_t ar_blk_cnt) {
    // How many bytes are those?
    uint32_t grow_sz = blk2bytes(ar_blk_cnt);

    // Tiny grows are not good in general because each tiny grow requires
    // at least 1 extent so multiple grows enlarge the segment (higher footprint)
    //
    // However, for simplicity, we are not going to apply any rule and let
    // the caller to do a cleaver strategy for us.
    //
    // At the same time we are not going to do any round up of the grow size:
    // we are going to let the allocator to do it for ourselves. This works
    // well because the allocator has the inline disabled (see default_req)
    // so we can merge/extend the segments (ours and the new one) flawless.

    // Allocate a segment to extend the current one
    auto additional_segm = sg_blkarr.allocator().alloc(grow_sz, default_req);
    segm.extend(additional_segm);

    // Create a new io segment because the underlying segment changed
    // (like when we need to create a new iterator if the container changed)
    sg_io.reset(new IOSegment(sg_blkarr, segm));

    return {past_end_blk_nr(), ar_blk_cnt};
}

uint32_t SegmentBlockArray::impl_shrink_by_blocks(uint32_t ar_blk_cnt) {
    return _impl_shrink_by_blocks(ar_blk_cnt, false);
}

uint32_t SegmentBlockArray::_impl_shrink_by_blocks(uint32_t ar_blk_cnt, bool release_blocks) {
    // How many blocks are pending-to-be-removed?
    uint32_t ar_pending_blk_cnt = capacity() - blk_cnt();

    // How many bytes are those?
    uint32_t shrink_sz = blk2bytes(ar_blk_cnt + ar_pending_blk_cnt);
    uint32_t shrank_sz = 0;

    // Note: the following must remove/free an entire number of blocks; subblocks
    // or smaller is not supported. This makes the code easier and simple and also
    // allows us to calculate ar_pending_blk_cnt as the diff of real and past-end.
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
            if (not sg_last_ext.is_suballoc() and release_blocks) {
                const uint16_t sg_shrink_blk_cnt = sg_blkarr.bytes2blk_cnt(shrink_sz);

                if (sg_shrink_blk_cnt) {
                    assert(sg_shrink_blk_cnt < sg_last_ext.blk_cnt());

                    const uint16_t sg_non_free_blk_cnt = sg_last_ext.blk_cnt() - sg_shrink_blk_cnt;
                    auto sg_ext2 = sg_last_ext.split(sg_non_free_blk_cnt);
                    sg_to_free.add_extent(sg_ext2);

                    segm.remove_last_extent();
                    segm.add_extent(sg_last_ext);

                    uint32_t alloc_sz2 = sg_ext2.calc_data_space_size(sg_blkarr.blk_sz_order());
                    shrink_sz -= alloc_sz2;
                    shrank_sz += alloc_sz2;
                }
            }

            break;
        }
    }


    if (sg_to_free.ext_cnt() >= 1) {
        sg_blkarr.allocator().dealloc(sg_to_free);
        sg_io.reset(new IOSegment(sg_blkarr, segm));

        assert(shrank_sz > 0);
        assert(shrank_sz % blk_sz() == 0);
        return bytes2blk_cnt(shrank_sz);
    }

    assert(shrank_sz == 0);
    return 0;
}

uint32_t SegmentBlockArray::impl_read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t to_read_sz = chk_extent_for_rw(true, ext, max_data_sz, start);
    if (to_read_sz == 0) {
        return 0;
    }

    sg_io->seek_rd(blk2bytes(ext.blk_nr()) + start);
    sg_io->readall(data, to_read_sz);
    return to_read_sz;
}

uint32_t SegmentBlockArray::impl_write_extent(const Extent& ext, const char* data, uint32_t max_data_sz,
                                              uint32_t start) {
    const uint32_t to_write_sz = chk_extent_for_rw(false, ext, max_data_sz, start);
    if (to_write_sz == 0) {
        return 0;
    }

    sg_io->seek_wr(blk2bytes(ext.blk_nr()) + start);
    sg_io->writeall(data, to_write_sz);
    return to_write_sz;
}

uint32_t SegmentBlockArray::impl_release_blocks() {
    // Shrink by 0 blocks: the side effect is that if there is any pending shrink,
    // it will happen there.
    return _impl_shrink_by_blocks(0, true);
}

SegmentBlockArray::SegmentBlockArray(Segment& segm, BlockArray& sg_blkarr, uint32_t blk_sz):
        BlockArray(), segm(segm), sg_blkarr(sg_blkarr) {
    if (segm.inline_data_sz() != 0) {
        throw std::runtime_error("Segment cannot contain inline data to be used for SegmentBlockArray");
    }
    segm.remove_inline_data();  // remove the end-of-segment as side effect

    sg_io.reset(new IOSegment(sg_blkarr, segm));

    if (sg_io->remain_rd() % blk_sz != 0) {
        throw std::runtime_error(
                "Segment does not has space multiple of the block size and cannot be used for SegmentBlockArray");
    }

    initialize_block_array(blk_sz, 0, sg_io->remain_rd() / blk_sz);

    default_req = sg_blkarr.allocator().get_default_alloc_requirements();
    default_req.max_inline_sz = 0;
}

SegmentBlockArray::~SegmentBlockArray() {}
