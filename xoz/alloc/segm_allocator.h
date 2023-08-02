#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <list>

#include "xoz/alloc/free_map.h"
#include "xoz/alloc/subblock_free_map.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/repo/repo.h"

class SegmentAllocator {
public:
    const static uint8_t StatsExtPerSegmLen = 8;

private:
    Repository& repo;
    uint32_t max_inline_sz;

    TailAllocator tail;

    FreeMap fr_map;
    SubBlockFreeMap subfr_map;

    float frag_factor;

    uint64_t in_use_by_user_sz;
    uint64_t in_use_blk_cnt;
    uint64_t in_use_blk_for_suballoc_cnt;
    uint64_t in_use_subblk_cnt;

    uint64_t in_use_ext_cnt;
    uint64_t in_use_inlined_sz;

    uint64_t alloc_call_cnt;
    uint64_t dealloc_call_cnt;

    uint64_t all_user_sz;
    uint64_t all_req_sz;

    uint64_t in_use_ext_per_segm[StatsExtPerSegmLen];

public:
    const static uint8_t MaxInlineSize = 8;

    explicit SegmentAllocator(Repository& repo, uint8_t max_inline_sz = MaxInlineSize, bool coalescing_enabled = true,
                              uint16_t split_above_threshold = 0):
            repo(repo),
            max_inline_sz(max_inline_sz),
            tail(repo),
            fr_map(coalescing_enabled, split_above_threshold),
            subfr_map(),
            frag_factor(1),
            in_use_by_user_sz(0),
            in_use_blk_cnt(0),
            in_use_blk_for_suballoc_cnt(0),
            in_use_subblk_cnt(0),
            in_use_ext_cnt(0),
            in_use_inlined_sz(0),
            alloc_call_cnt(0),
            dealloc_call_cnt(0),
            all_user_sz(0),
            all_req_sz(0) {
        memset(in_use_ext_per_segm, 0, sizeof(in_use_ext_per_segm));
    }

    Segment alloc(const uint32_t sz) {
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
        uint32_t avail_sz = 0;

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
            // This should be guaranteed because MaxInlineSize is a uint8_t
            assert(inline_sz < 256);
            segm.reserve_inline_data(uint8_t(inline_sz));
            inline_sz = 0;
        }

        assert(blk_cnt_remain == 0);
        assert(subblk_cnt_remain == 0);
        assert(inline_sz == 0);
        assert(sz_remain == 0);

        avail_sz = segm.calc_usable_space_size(repo.blk_sz_order());
        in_use_by_user_sz += avail_sz;
        in_use_ext_cnt += segm.ext_cnt();
        in_use_inlined_sz += segm.inline_data_sz();
        in_use_blk_cnt += segm.blk_cnt();
        in_use_subblk_cnt += segm.subblk_cnt();

        calc_ext_per_segm_stats(segm, true);

        all_user_sz += avail_sz;
        all_req_sz += sz;

        ++alloc_call_cnt;
        return segm;
    no_free_space:
        throw "no free space";
    }

    void dealloc(const Segment& segm) {
        auto sz = segm.calc_usable_space_size(repo.blk_sz_order());
        auto blk_cnt = 0;
        auto subblk_cnt = 0;
        for (auto const& ext: segm.exts()) {
            if (ext.is_suballoc()) {
                subfr_map.dealloc(ext);
                subblk_cnt += ext.subblk_cnt();
            } else {
                fr_map.dealloc(ext);
                blk_cnt += ext.blk_cnt();
            }
        }

        in_use_by_user_sz -= sz;
        in_use_blk_cnt -= blk_cnt;
        in_use_subblk_cnt -= subblk_cnt;
        in_use_ext_cnt -= segm.ext_cnt();
        in_use_inlined_sz -= segm.inline_data_sz();

        calc_ext_per_segm_stats(segm, false);
        ++dealloc_call_cnt;

        reclaim_free_space_from_subfr_map();
    }

    void release() {
        reclaim_free_space_from_subfr_map();
        reclaim_free_space_from_fr_map();
    }

    struct stats_t {
        // How many bytes are currently used (aka allocated / non-free)?
        uint64_t in_use_by_user_sz;

        // How many blocks are currently in use?
        // How many of those blocks are being used for suballocation?
        // How many subblocks are in use?
        uint64_t in_use_blk_cnt;
        uint64_t in_use_blk_for_suballoc_cnt;
        uint64_t in_use_subblk_cnt;

        // How many extents are there?
        uint64_t in_use_ext_cnt;

        // How many bytes were inlined?
        uint64_t in_use_inlined_sz;

        // How many times alloc() / dealloc() were called?
        // Note: calling dealloc() does not decrement alloc_call_cnt,
        // both counter are monotonic.
        uint64_t alloc_call_cnt;
        uint64_t dealloc_call_cnt;

        // External fragmentation is defined as how many blocks the repository
        // has (without counting metadata) and how many were allocated by
        // the Segment allocator.
        //
        // The difference, in block count, are the blocks unallocated by
        // the allocator (aka free) but not released back the repository
        // (making shrink its size).
        //
        // The difference of blocks is then converted to bytes.
        //
        // An large number may indicate that the SegmentAllocator is not
        // doing a good job finding free space for alloc(), or it is not
        // doing a smart split or the alloc/dealloc pattern is kind of pathological.
        uint64_t external_frag_sz;

        // Internal fragmentation is defined as how many bytes are allocated
        // (as both blocks and subblocks and inline) minus how many bytes
        // were requested by the user/caller. This assumes that the user
        // will not use the extra space that it is totally wasted.
        //
        // This wasted space lives within the block or subblock so it is not
        // allocable. This means that the space is lost until the segment
        // that owns it is deallocated.
        //
        // An large number may indicate that the inline space is not enough
        // and more semi-used subblocks are being used instead or that
        // suballocation is disabled forcing the SegmentAllocator to use
        // full blocks (and the user data is clearly not a multiple of
        // the block size hence the internal fragmentation).
        uint64_t internal_frag_sz;

        // This internal fragmentation is defined as the blocks allocated
        // for suballocation (in bytes) minus the subblocks in use (in bytes).
        //
        // This wasted space lives within the blocks for suballocation
        // and can be allocated in future calls to alloc() without requiring
        // deallocating first the segment that owns that block.
        //
        // An large number may indicate that there are a lot of blocks
        // for suballocation semi-used. Some it is expected but if the
        // number is too large it may indicate that different segments
        // are not reusing the same block for their suballocation.
        uint64_t allocable_internal_frag_sz;

        // Count how many segments are that have certain count of extents
        // as a measure of the "split-ness", "spread" or "data fragmentation".
        //
        // For the first 5 indexes (array[0] to array[4] inclusive), the count is for
        // segments with 0 to 4 extents (inclusive).
        // For array[5], segments with from 5 to 8 extents (inclusive)
        // For array[6], segments with from 9 to 16 extents (inclusive)
        // For array[7], segments with 17 or more extents
        uint64_t in_use_ext_per_segm[StatsExtPerSegmLen];
    };

    struct stats_t stats() const {
        uint64_t external_frag_sz = (repo.data_blk_cnt() - in_use_blk_cnt) << repo.blk_sz_order();

        uint64_t internal_frag_sz = all_user_sz - all_req_sz;

        uint64_t allocable_internal_frag_sz =
                ((in_use_blk_for_suballoc_cnt << repo.blk_sz_order()) -
                 ((in_use_subblk_cnt << (repo.blk_sz_order() - Extent::SUBBLK_SIZE_ORDER))));

        struct stats_t st = {.in_use_by_user_sz = in_use_by_user_sz,
                             .in_use_blk_cnt = in_use_blk_cnt,
                             .in_use_blk_for_suballoc_cnt = in_use_blk_for_suballoc_cnt,
                             .in_use_subblk_cnt = in_use_subblk_cnt,

                             .in_use_ext_cnt = in_use_ext_cnt,
                             .in_use_inlined_sz = in_use_inlined_sz,

                             .alloc_call_cnt = alloc_call_cnt,
                             .dealloc_call_cnt = dealloc_call_cnt,

                             .external_frag_sz = external_frag_sz,
                             .internal_frag_sz = internal_frag_sz,

                             .allocable_internal_frag_sz = allocable_internal_frag_sz,

                             .in_use_ext_per_segm = {0}};

        memcpy(st.in_use_ext_per_segm, in_use_ext_per_segm, sizeof(st.in_use_ext_per_segm));

        return st;
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
            in_use_blk_for_suballoc_cnt += result.ext.blk_cnt();
            in_use_blk_cnt += result.ext.blk_cnt();
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
        uint32_t blk_cnt = 0;

        for (auto it = subfr_map.cbegin_full_blk(); it != subfr_map.cend_full_blk(); ++it) {
            const auto ext = it->as_not_suballoc();
            fr_map.dealloc(ext);
            reclaimed.push_back(ext);
            blk_cnt += ext.blk_cnt();
        }

        subfr_map.release(reclaimed);
        in_use_blk_for_suballoc_cnt -= blk_cnt;
        in_use_blk_cnt -= blk_cnt;
    }

    void calc_ext_per_segm_stats(const Segment& segm, bool is_alloc) {
        auto ext_cnt = segm.ext_cnt();

        // Ext count: 0 1 2 3 4
        auto index = ext_cnt;
        if (ext_cnt > 4) {
            // Ext count: [5 8] (8 16] (16 inf)
            index = 4;
            if (ext_cnt <= 8) {
                index += 1;
            } else if (ext_cnt <= 16) {
                index += 2;
            } else {
                index += 3;
            }
        }

        assert(StatsExtPerSegmLen == 8);  // cppcheck-suppress knownConditionTrueFalse

        if (is_alloc) {
            ++in_use_ext_per_segm[index];
        } else {
            --in_use_ext_per_segm[index];
        }
    }
};
