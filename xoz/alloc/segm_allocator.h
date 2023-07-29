#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <list>

#include "xoz/alloc/free_map.h"
#include "xoz/alloc/subblock_free_map.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/repo/repo.h"

class SegmentAllocator {
private:
    Repository& repo;
    uint16_t max_inline_sz;

    TailAllocator tail;

    FreeMap fr_map;
    SubBlockFreeMap subfr_map;

    float frag_factor;

public:
    const static uint16_t MaxInlineSize = 8;

    explicit SegmentAllocator(Repository& repo, uint16_t max_inline_sz = MaxInlineSize, bool coalescing_enabled = true,
                              uint16_t split_above_threshold = 0):
            repo(repo),
            max_inline_sz(max_inline_sz),
            tail(repo),
            fr_map(coalescing_enabled, split_above_threshold),
            subfr_map(),
            frag_factor(1) {}

    Segment alloc(uint32_t sz) {
        //
        //   [------------------------------------------------------] <-- sz
        //   :                                                      :
        //   :      blk                  blk             blk        :
        //   |----|----|----|----||----|----|----|----||----|----|..:
        //   \___________________/\___________________/\________/:  :
        //      extent (full)            (full)        (not full):  :
        //                                                       :  :
        //        |                      _______________________/   |
        //        |                     /                           |
        //        |                    [............................] <-- sz % blk_sz
        //        |                    :                            :
        //        |                    : subblk                     :
        //        |                    |-------|------|-----|.......:
        //        |                    \____________________/       :
        //        |                        single extent    :       :
        //        |   /----------------- for suballocation  :       :
        //        V   V                                     :       :
        //   +===+===+===+--------+                         :       :
        //   |  extents  | inline |      __________________/        |
        //   +===+===+===+--------+     /                           |
        //          Segment    ^       [............................] <-- (sz % blk_sz) % subblk_sz
        //                     |       :                            :
        //                     \       :                            :
        //                      \      |----------------------------|
        //                       \            inline data
        //                        \----------- /

        Segment segm;
        uint32_t sz_remain = sz;

        // How many blocks are needed?
        uint32_t blk_cnt_remain = sz_remain / repo.blk_sz();
        sz_remain = sz_remain % repo.blk_sz();

        // How many sub blocks are needed?
        uint32_t subblk_cnt_remain = sz_remain / repo.subblk_sz();
        sz_remain = sz_remain % repo.subblk_sz();

        // How many bytes are going to be inline'd?
        uint32_t inline_sz = sz_remain;
        sz_remain = 0;

        // Backpressure: if inline sz is greater than the limit,
        // put it into its own subblock
        if (inline_sz > max_inline_sz) {
            ++subblk_cnt_remain;
            inline_sz = 0;
        }

        // Backpressure: if subblk count can fill an entire block
        // do it
        if (subblk_cnt_remain == Extent::SUBBLK_CNT_PER_BLK) {
            ++blk_cnt_remain;
            subblk_cnt_remain = 0;
        }


        // Count how many extent are we are going to need (in the best
        // scenario where we can fit large sequences of blocks without
        // fragmentation).
        //
        // On top of that, add some slack for fragmentation
        // (frag_factor > 1);
        //
        // TODO check float-to-int and back conversions + overflows
        // USE ORDER
        const uint32_t max_ext_cnt = uint32_t((float(blk_cnt_remain) / Extent::MAX_BLK_CNT) * frag_factor) + 1;

        // Allocate extents trying to not expand the repository
        // but instead reusing free space already present even if
        // that means to fragment the segment a little more
        if (blk_cnt_remain) {
            blk_cnt_remain = allocate_extents(segm, blk_cnt_remain, max_ext_cnt, false, false);
        }

        // If we still require to allocate more blocks, just allow
        // to expand the repository to get more free space
        if (blk_cnt_remain) {
            blk_cnt_remain = allocate_extents(segm, blk_cnt_remain, max_ext_cnt, true, true);
        }

        if (blk_cnt_remain) {
            goto no_free_space;
        }

        if (subblk_cnt_remain) {
            // RFC says 16 subblocks per block only and the code above should
            // ensure that we are dealing with one block at most.
            assert(subblk_cnt_remain < 256);
            assert(subblk_cnt_remain <= Extent::SUBBLK_CNT_PER_BLK);
            subblk_cnt_remain = allocate_subblk_extent(segm, uint8_t(subblk_cnt_remain));
        }

        if (subblk_cnt_remain) {
            goto no_free_space;
        }

        if (inline_sz) {
            // TODO cast
            segm.reserve_inline_data(uint8_t(inline_sz));
            inline_sz = 0;
        }

        assert(blk_cnt_remain == 0);
        assert(subblk_cnt_remain == 0);
        assert(inline_sz == 0);
        assert(sz_remain == 0);

        return segm;
    no_free_space:
        throw "no free space";
    }

    void dealloc(const Segment& segm) {
        for (auto const& ext: segm.exts()) {
            if (ext.is_suballoc()) {
                subfr_map.dealloc(ext);
            } else {
                fr_map.dealloc(ext);
            }
        }

        reclaim_free_space_from_subfr_map();
    }

    void release() {
        reclaim_free_space_from_subfr_map();
        reclaim_free_space_from_fr_map();
    }


    typedef xoz::alloc::internals::ConstExtentMergeIterator<FreeMap::const_iterator_by_blk_nr_t,
                                                            SubBlockFreeMap::const_iterator_by_blk_nr_t, true>
            const_iterator_by_blk_nr_t;

    inline const_iterator_by_blk_nr_t cbegin_by_blk_nr() const {
        return const_iterator_by_blk_nr_t(fr_map.cbegin_by_blk_nr(), fr_map.cend_by_blk_nr(),
                                          subfr_map.cbegin_by_blk_nr(), subfr_map.cend_by_blk_nr());
    }

    inline const_iterator_by_blk_nr_t cend_by_blk_nr() const {
        return const_iterator_by_blk_nr_t(fr_map.cend_by_blk_nr(), fr_map.cend_by_blk_nr(), subfr_map.cend_by_blk_nr(),
                                          subfr_map.cend_by_blk_nr());
    }

private:
    uint32_t allocate_extents(Segment& segm, uint32_t blk_cnt_remain, uint32_t max_ext_cnt, bool ignore_max_ext_cnt,
                              bool use_parent) {
        uint32_t allocated = segm.ext_cnt();
        uint32_t predicted_remaining = (blk_cnt_remain / Extent::MAX_BLK_CNT) + 1;

        // Given N extents already allocated and assuming a perfect world where
        // we can allocate M extents more for the blk_cnt_remain request.
        //
        // The segment would end up having N+M extents. If that is below the
        // max_ext_cnt limit (or the limit is ignored), we continue doing
        // allocations even if in the reality we do allocations smaller
        // than in a perfect would.
        //
        // Eventually or we complete and leave blk_cnt_remain to 0 or we reach
        // the limit (because we are allocating too small extents). In that case
        // we stop to avoid further fragmentation.
        //
        // If ignore_max_ext_cnt is set, we keep allocating.
        bool frag_level_ok = (allocated + predicted_remaining < max_ext_cnt) or ignore_max_ext_cnt;

        // Block count "probe" or "try" to allocate
        uint32_t blk_cnt_probe = (uint16_t)(-1);

        while (blk_cnt_remain and frag_level_ok) {
            // ensure we are not trying to allocate more blocks than can fit
            // in a single Extent or that the ones required to hold sz data
            blk_cnt_probe = std::min(blk_cnt_probe, blk_cnt_remain);
            blk_cnt_probe = std::min(blk_cnt_probe, Extent::MAX_BLK_CNT);

            // Note: cast to uint16_t is OK as blk_cnt_probe is necessary smaller
            // than MAX UINT16 because it is smaller than Extent::MAX_BLK_CNT;
            auto result = fr_map.alloc(uint16_t(blk_cnt_probe));
            if (result.success) {
                assert(blk_cnt_probe == result.ext.blk_cnt());

                segm.add_extent(result.ext);

                ++allocated;
                blk_cnt_remain -= result.ext.blk_cnt();
                predicted_remaining = (blk_cnt_remain / Extent::MAX_BLK_CNT) + 1;

                frag_level_ok = (allocated + predicted_remaining < max_ext_cnt) or ignore_max_ext_cnt;

            } else {
                if (use_parent) {
                    auto ok = provide_more_space_to_fr_map(uint16_t(blk_cnt_probe));
                    if (not ok) {
                        // not enough free space in parent allocator
                        return blk_cnt_remain;
                    }

                } else {
                    uint16_t closest_free_blk_cnt = result.ext.blk_cnt();

                    if (closest_free_blk_cnt == 0) {
                        // There is no free space, return how many blocks
                        // are still to be allocated.
                        return blk_cnt_remain;
                    }

                    // Try to allocate this new (smaller) block count per extent
                    // from now and on
                    blk_cnt_probe = closest_free_blk_cnt;
                }
            }
        }

        return blk_cnt_remain;
    }

    uint8_t allocate_subblk_extent(Segment& segm, uint8_t subblk_cnt_remain) {
        bool ok = false;

    try_subfr_map_alloc:
        auto result = subfr_map.alloc(subblk_cnt_remain);
        if (result.success) {
            segm.add_extent(result.ext);
            return 0;
        }

    try_fr_map_alloc:
        ok = provide_more_space_to_subfr_map();
        if (ok) {
            goto try_subfr_map_alloc;
        }

        ok = provide_more_space_to_fr_map(1);
        if (ok) {
            goto try_fr_map_alloc;
        }

        return subblk_cnt_remain;
    }

    bool provide_more_space_to_fr_map(uint16_t blk_cnt) {
        auto result = tail.alloc(blk_cnt);
        if (result.success) {
            fr_map.provide(result.ext);
            return true;
        }

        return false;
    }

    bool provide_more_space_to_subfr_map() {
        auto result = fr_map.alloc(1);
        if (result.success) {
            subfr_map.provide(result.ext);
            return true;
        }

        return false;
    }

    void reclaim_free_space_from_fr_map() {
        std::list<Extent> reclaimed;

        for (auto it = fr_map.crbegin_by_blk_nr(); it != fr_map.crend_by_blk_nr(); ++it) {
            bool ok = tail.dealloc(*it);
            if (ok) {
                reclaimed.push_back(*it);
            } else {
                break;
            }
        }

        fr_map.release(reclaimed);
    }

    void reclaim_free_space_from_subfr_map() {
        std::list<Extent> reclaimed;

        for (auto it = subfr_map.cbegin_full_blk(); it != subfr_map.cend_full_blk(); ++it) {
            const auto ext = it->as_not_suballoc();
            fr_map.dealloc(ext);
            reclaimed.push_back(ext);
        }

        subfr_map.release(reclaimed);
    }
};
