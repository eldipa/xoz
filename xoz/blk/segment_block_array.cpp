#include "xoz/blk/segment_block_array.h"

#include "xoz/blk/block_array.h"
#include "xoz/blk/segment_block_array_flags.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/io/iosegment.h"
#include "xoz/segm/segment.h"

namespace xoz {
// Note:
//  - fg_blk_cnt means blk count in terms of the array's blocks (public API)
//  - bg_blk_cnt means blk count in terms of the segment's blocks (internal implementation API)
std::tuple<uint32_t, uint16_t> SegmentBlockArray::impl_grow_by_blocks(uint16_t fg_blk_cnt) {
    // How many bytes are those?
    uint32_t grow_sz = blk2bytes(fg_blk_cnt);

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

    // Realloc (expand) the current segment or allocate a new segment
    // to extend the current one.
    // The former is more efficient when there are
    // several and tiny grows that incur in suballocations (by the bg allocator)
    // because realloc() will try to deallocate those suballocations and reallocate
    // a single larger one. The downside is this incurs in some copy of the data.
    // The latter is less efficient but no copy happens.
    uint32_t orig_sg_sz = segm->calc_data_space_size();
    if (flags & SG_BLKARR_REALLOC_ON_GROW) {
        bg_blkarr.allocator().realloc(*segm, orig_sg_sz + grow_sz, default_req);
        assert(not segm->is_inline_present());
    } else {
        auto additional_segm = bg_blkarr.allocator().alloc(grow_sz, default_req);
        segm->extend(additional_segm);
    }

    // Create a new io segment because the underlying segment changed
    // (like when we need to create a new iterator if the container changed)
    bg_io.reset(new IOSegment(bg_blkarr, *segm));

    // How many we really allocated? We requested grow_sz bytes so we should
    // have at least fg_blk_cnt blocks but we may had got more
    // (for example, bg_blkarr.blk_sz is large enough and we cannot split it
    // into tiny chunks and if we request grow_sz small enough we may get
    // a single tiny chunk for it but larger than it, hence, this translate
    // to having allocated more blocks than the initially requested/expected
    // fg_blk_cnt
    uint32_t real_grow_sz = segm->calc_data_space_size() - orig_sg_sz;
    uint16_t real_front_blk_cnt = bytes2blk_cnt(real_grow_sz);
    assert(real_grow_sz >= grow_sz);
    assert(real_front_blk_cnt >= fg_blk_cnt);

    return {past_end_blk_nr(), real_front_blk_cnt};
}

uint32_t SegmentBlockArray::impl_shrink_by_blocks(uint32_t fg_blk_cnt) {
    return _impl_shrink_by_blocks(fg_blk_cnt, false);
}

uint32_t SegmentBlockArray::_impl_shrink_by_blocks(uint32_t fg_blk_cnt, bool release_blocks) {
    // How many blocks are pending-to-be-removed?
    uint32_t fg_pending_blk_cnt = capacity() - blk_cnt();

    // How many bytes are those?
    uint32_t shrink_sz = blk2bytes(fg_blk_cnt + fg_pending_blk_cnt);
    uint32_t shrank_sz = 0;

    // Note: the following must remove/free an entire number of blocks; subblocks
    // or smaller is not supported. This makes the code easier and simple and also
    // allows us to calculate fg_pending_blk_cnt as the diff of real and past-end.
    Segment to_free(bg_blkarr.blk_sz_order());
    while (shrink_sz > 0) {
        assert(segm->ext_cnt() >= 1);

        auto last_ext = segm->exts().back();
        uint32_t alloc_sz = last_ext.calc_data_space_size(bg_blkarr.blk_sz_order());

        if (alloc_sz <= shrink_sz) {
            to_free.add_extent(last_ext);
            segm->remove_last_extent();
            shrink_sz -= alloc_sz;
            shrank_sz += alloc_sz;
        } else {
            if (release_blocks) {
                Extent ext2 = Extent::EmptyExtent();
                if (last_ext.is_suballoc()) {
                    const uint16_t bg_shrink_subblk_cnt =
                            bg_blkarr.bytes2subblk_cnt(shrink_sz, BlockArray::RoundMode::floor);
                    if (bg_shrink_subblk_cnt) {
                        assert(bg_shrink_subblk_cnt < last_ext.subblk_cnt());

                        const uint16_t bg_non_free_subblk_cnt = last_ext.subblk_cnt() - bg_shrink_subblk_cnt;
                        ext2 = last_ext.split(bg_non_free_subblk_cnt);
                    }
                } else {
                    const uint16_t bg_shrink_blk_cnt = bg_blkarr.bytes2blk_cnt(shrink_sz, BlockArray::RoundMode::floor);

                    if (bg_shrink_blk_cnt) {
                        assert(bg_shrink_blk_cnt < last_ext.blk_cnt());

                        const uint16_t bg_non_free_blk_cnt = last_ext.blk_cnt() - bg_shrink_blk_cnt;
                        ext2 = last_ext.split(bg_non_free_blk_cnt);
                    }
                }

                if (not ext2.is_empty()) {
                    to_free.add_extent(ext2);

                    segm->remove_last_extent();
                    segm->add_extent(last_ext);

                    uint32_t alloc_sz2 = ext2.calc_data_space_size(bg_blkarr.blk_sz_order());
                    shrink_sz -= alloc_sz2;
                    shrank_sz += alloc_sz2;
                }
            }

            break;
        }
    }


    if (to_free.ext_cnt() >= 1) {
        bg_blkarr.allocator().dealloc(to_free);
        bg_io.reset(new IOSegment(bg_blkarr, *segm));

        assert(shrank_sz > 0);
        assert(shrank_sz % blk_sz() == 0);
        return bytes2blk_cnt(shrank_sz);
    }

    assert(shrank_sz == 0);
    return 0;
}

uint32_t SegmentBlockArray::impl_release_blocks() {
    // Shrink by 0 blocks: the side effect is that if there is any pending shrink,
    // it will happen there.
    return _impl_shrink_by_blocks(0, true);
}

void SegmentBlockArray::impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    bg_io->seek_rd(blk2bytes(blk_nr) + offset);
    bg_io->readall(buf, exact_sz);
}

void SegmentBlockArray::impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    bg_io->seek_wr(blk2bytes(blk_nr) + offset);
    bg_io->writeall(buf, exact_sz);
}


SegmentBlockArray::SegmentBlockArray(Segment& segm, BlockArray& bg_blkarr, uint32_t fg_blk_sz, uint32_t flags):
        BlockArray(), segm(nullptr), bg_blkarr(bg_blkarr), flags(flags), fg_blk_sz(fg_blk_sz) {
    fail_if_bad_blk_sz(fg_blk_sz);

    initialize_segment(segm);

    // TODO bg_blkarr.fg_blk_sz() % fg_blk_sz == 0 and (bg_blkarr.fg_blk_sz()/16) % fg_blk_sz == 0

    default_req = bg_blkarr.allocator().get_default_alloc_requirements();
    default_req.max_inline_sz = 0;
}

SegmentBlockArray::~SegmentBlockArray() {}

SegmentBlockArray::SegmentBlockArray(BlockArray& bg_blkarr, uint32_t fg_blk_sz, uint32_t flags):
        BlockArray(), segm(nullptr), bg_blkarr(bg_blkarr), flags(flags), fg_blk_sz(fg_blk_sz) {
    fail_if_bad_blk_sz(fg_blk_sz);

    // TODO bg_blkarr.fg_blk_sz() % fg_blk_sz == 0 and (bg_blkarr.fg_blk_sz()/16) % fg_blk_sz == 0

    default_req = bg_blkarr.allocator().get_default_alloc_requirements();
    default_req.max_inline_sz = 0;
}

void SegmentBlockArray::initialize_segment(Segment& segm) {
    if (this->segm != nullptr) {
        throw std::runtime_error("Segment block array already initialized (managed). initialize_segment called twice?");
    }

    if (segm.inline_data_sz() != 0) {
        throw std::runtime_error("Segment cannot contain inline data to be used for SegmentBlockArray");
    }
    segm.remove_inline_data();

    bg_io.reset(new IOSegment(bg_blkarr, segm));

    if (bg_io->remain_rd() % fg_blk_sz != 0) {
        throw std::runtime_error(
                "Segment does not has space multiple of the block size and cannot be used for SegmentBlockArray");
    }

    this->segm = &segm;
    initialize_block_array(fg_blk_sz, 0, bg_io->remain_rd() / fg_blk_sz);
}
}  // namespace xoz
