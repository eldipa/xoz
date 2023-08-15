#include "xoz/alloc/segm_allocator.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <list>

#include "xoz/alloc/free_map.h"
#include "xoz/alloc/subblock_free_map.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/repo/repo.h"
#include "xoz/trace.h"

#define TRACE TRACE_ON(0x01)

#define TRACE_SECTION(x) TRACE << (x) << " "
#define TRACE_LINE TRACE << "    "

SegmentAllocator::SegmentAllocator(Repository& repo, bool coalescing_enabled, uint16_t split_above_threshold):
        repo(repo),
        tail(repo),
        fr_map(coalescing_enabled, split_above_threshold),
        subfr_map(),
        coalescing_enabled(coalescing_enabled),
        in_use_by_user_sz(0),
        in_use_blk_cnt(0),
        in_use_blk_for_suballoc_cnt(0),
        in_use_subblk_cnt(0),
        in_use_ext_cnt(0),
        in_use_inlined_sz(0),
        alloc_call_cnt(0),
        dealloc_call_cnt(0),
        accum_internal_frag_avg_sz(0) {
    memset(in_use_ext_per_segm, 0, sizeof(in_use_ext_per_segm));
}

Segment SegmentAllocator::alloc(const uint32_t sz, const struct req_t& req) {
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
    uint32_t subblk_cnt_remain;
    if (req.allow_suballoc) {
        subblk_cnt_remain = sz_remain / repo.subblk_sz();
        sz_remain = sz_remain % repo.subblk_sz();
    } else {
        subblk_cnt_remain = 0;
    }

    // How many bytes are going to be inline'd?
    uint32_t inline_sz = sz_remain;
    sz_remain = 0;

    // Backpressure: if inline sz is greater than the limit,
    // put it into its own subblock
    // In this case the inline must be 0 (unused)
    if (inline_sz > req.max_inline_sz) {
        if (req.allow_suballoc) {
            assert(inline_sz <= repo.subblk_sz());
            ++subblk_cnt_remain;
        } else {
            assert(inline_sz <= repo.blk_sz());
            ++blk_cnt_remain;
        }
        inline_sz = 0;
    }

    // Backpressure: if subblk count can fill an entire block
    // do it
    if (subblk_cnt_remain == Extent::SUBBLK_CNT_PER_BLK) {
        ++blk_cnt_remain;
        subblk_cnt_remain = 0;
    }

    TRACE_SECTION("A") << std::setw(5) << sz << " b" << TRACE_ENDL;

    // Allocate extents trying to not expand the repository
    // but instead reusing free space already present even if
    // that means to fragment the segment a little more
    if (blk_cnt_remain) {
        TRACE_LINE << "to alloc,not grow -> " << blk_cnt_remain << "+" << subblk_cnt_remain << "+" << inline_sz
                   << "   -----v" << TRACE_ENDL;
        blk_cnt_remain = allocate_extents(segm, blk_cnt_remain, req.segm_frag_threshold, false, false);
    }

    // If we still require to allocate more blocks, just allow
    // to expand the repository to get more free space
    if (blk_cnt_remain) {
        TRACE_LINE << "to alloc,may grow -> " << blk_cnt_remain << "+" << subblk_cnt_remain << "+" << inline_sz
                   << "   -----v" << TRACE_ENDL;
        blk_cnt_remain = allocate_extents(segm, blk_cnt_remain, req.segm_frag_threshold, true, true);
    }

    if (blk_cnt_remain) {
        goto no_free_space;
    }

    if (subblk_cnt_remain) {
        TRACE_LINE << "to suballoc  ->      " << blk_cnt_remain << "+" << subblk_cnt_remain << "+" << inline_sz
                   << "   -----v" << TRACE_ENDL;
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

    TRACE_LINE << "* segment: " << segm << TRACE_ENDL;

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

    accum_internal_frag_avg_sz += (avail_sz - sz);

    ++alloc_call_cnt;
    return segm;
no_free_space:
    throw "no free space";
}

void SegmentAllocator::dealloc(const Segment& segm) {
    auto sz = segm.calc_usable_space_size(repo.blk_sz_order());

    TRACE_SECTION("D") << std::setw(5) << sz << " b" << TRACE_ENDL;
    TRACE_LINE << "* segment: " << segm << TRACE_ENDL;

    // bool used_suballoc = false;
    auto blk_cnt = 0;
    auto subblk_cnt = 0;
    for (auto const& ext: segm.exts()) {
        if (ext.is_suballoc()) {
            subfr_map.dealloc(ext);
            subblk_cnt += ext.subblk_cnt();
            // used_suballoc = true;
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

    uint64_t avg_truncated =
            uint64_t((double(accum_internal_frag_avg_sz) / double(alloc_call_cnt - (dealloc_call_cnt - 1))));
    accum_internal_frag_avg_sz -= avg_truncated;

    reclaim_free_space_from_subfr_map();
}

void SegmentAllocator::release() {
    reclaim_free_space_from_subfr_map();
    reclaim_free_space_from_fr_map();
}

SegmentAllocator::stats_t SegmentAllocator::stats() const {
    uint64_t repo_data_sz = (repo.data_blk_cnt() << repo.blk_sz_order());

    uint64_t external_frag_sz = (repo.data_blk_cnt() - in_use_blk_cnt) << repo.blk_sz_order();
    double external_frag_sz_kb = double(external_frag_sz) / double(1024.0);
    double external_frag_rel = repo_data_sz == 0 ? 0 : (double(external_frag_sz) / double(repo_data_sz));

    uint64_t internal_frag_avg_sz = accum_internal_frag_avg_sz;
    double internal_frag_avg_sz_kb = double(internal_frag_avg_sz) / double(1024.0);
    double internal_frag_avg_rel =
            in_use_by_user_sz == 0 ? 0 : (double(internal_frag_avg_sz) / double(in_use_by_user_sz));

    uint64_t allocable_internal_frag_sz = ((in_use_blk_for_suballoc_cnt << repo.blk_sz_order()) -
                                           ((in_use_subblk_cnt << (repo.blk_sz_order() - Extent::SUBBLK_SIZE_ORDER))));
    double allocable_internal_frag_sz_kb = double(allocable_internal_frag_sz) / double(1024.0);
    double allocable_internal_frag_rel =
            in_use_blk_for_suballoc_cnt == 0 ?
                    0 :
                    (double(allocable_internal_frag_sz) / double((in_use_blk_for_suballoc_cnt << repo.blk_sz_order())));


    uint64_t in_use_segment_cnt = alloc_call_cnt - dealloc_call_cnt;

    double in_use_by_user_sz_kb = double(in_use_by_user_sz) / double(1024.0);

    struct stats_t st = {.in_use_by_user_sz = in_use_by_user_sz,
                         .in_use_by_user_sz_kb = in_use_by_user_sz_kb,

                         .in_use_blk_cnt = in_use_blk_cnt,
                         .in_use_blk_for_suballoc_cnt = in_use_blk_for_suballoc_cnt,
                         .in_use_subblk_cnt = in_use_subblk_cnt,

                         .in_use_ext_cnt = in_use_ext_cnt,
                         .in_use_segment_cnt = in_use_segment_cnt,
                         .in_use_inlined_sz = in_use_inlined_sz,

                         .alloc_call_cnt = alloc_call_cnt,
                         .dealloc_call_cnt = dealloc_call_cnt,

                         .external_frag_sz = external_frag_sz,
                         .external_frag_sz_kb = external_frag_sz_kb,
                         .external_frag_rel = external_frag_rel,
                         .internal_frag_avg_sz = internal_frag_avg_sz,
                         .internal_frag_avg_sz_kb = internal_frag_avg_sz_kb,
                         .internal_frag_avg_rel = internal_frag_avg_rel,

                         .allocable_internal_frag_sz = allocable_internal_frag_sz,
                         .allocable_internal_frag_sz_kb = allocable_internal_frag_sz_kb,
                         .allocable_internal_frag_rel = allocable_internal_frag_rel,

                         .in_use_ext_per_segm = {0},
                         .suballoc_bin_cnts = {0}};

    memcpy(st.in_use_ext_per_segm, in_use_ext_per_segm, sizeof(st.in_use_ext_per_segm));

    subfr_map.fill_bin_stats(st.suballoc_bin_cnts, sizeof(st.suballoc_bin_cnts) / sizeof(st.suballoc_bin_cnts[0]));

    return st;
}

uint32_t SegmentAllocator::allocate_extents(Segment& segm, uint32_t blk_cnt_remain, uint16_t segm_frag_threshold,
                                            bool ignore_segm_frag_threshold, bool use_parent) {
    uint32_t current_segm_frag = segm.ext_cnt() <= 1 ? 0 : (segm.ext_cnt() - 1);

    bool frag_level_ok = (current_segm_frag < segm_frag_threshold) or ignore_segm_frag_threshold;

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
            ++current_segm_frag;

            blk_cnt_remain -= result.ext.blk_cnt();

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
                ++current_segm_frag;
            }
        }

        frag_level_ok = (current_segm_frag < segm_frag_threshold) or ignore_segm_frag_threshold;
    }

    return blk_cnt_remain;
}

uint8_t SegmentAllocator::allocate_subblk_extent(Segment& segm, uint8_t subblk_cnt_remain) {
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

bool SegmentAllocator::provide_more_space_to_fr_map(uint16_t blk_cnt) {
    TRACE_LINE << "tail provides to freemap  " << TRACE_FLUSH;
    auto orig_blk_cnt = blk_cnt;
    if (coalescing_enabled) {
        auto last_free_it = fr_map.crbegin_by_blk_nr();
        if (last_free_it != fr_map.crend_by_blk_nr() and tail.is_at_the_end(*last_free_it)) {
            auto extendable_cnt = last_free_it->blk_cnt();
            blk_cnt = (blk_cnt <= extendable_cnt) ? 0 : (blk_cnt - extendable_cnt);

            if (!blk_cnt) {
                ++blk_cnt;
            }
        }
    }

    TRACE << std::setw(5) << blk_cnt << " blks" << TRACE_FLUSH;
    if (orig_blk_cnt != blk_cnt) {
        TRACE << " (orig req: " << orig_blk_cnt << ")" << TRACE_FLUSH;
    }
    TRACE << " ----v" << TRACE_ENDL;

    auto result = tail.alloc(blk_cnt);
    if (result.success) {
        fr_map.provide(result.ext);
        return true;
    } else {
        TRACE_LINE << " * tail couldn't provide" << TRACE_ENDL;
    }

    return false;
}

bool SegmentAllocator::provide_more_space_to_subfr_map() {
    TRACE_LINE << "freemap provides subblks to subfreemap -------v" << TRACE_ENDL;
    auto result = fr_map.alloc(1);
    if (result.success) {
        subfr_map.provide(result.ext);
        in_use_blk_for_suballoc_cnt += result.ext.blk_cnt();
        in_use_blk_cnt += result.ext.blk_cnt();
        return true;
    } else {
        TRACE_LINE << " * freemap couldn't provide" << TRACE_ENDL;
    }

    return false;
}

void SegmentAllocator::reclaim_free_space_from_fr_map() {
    TRACE_LINE << "tail reclaims blks from freemap --------------v" << TRACE_ENDL;
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

void SegmentAllocator::reclaim_free_space_from_subfr_map() {
    TRACE_LINE << "freemap reclaims subblks from subfreemap -----v" << TRACE_ENDL;
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

void SegmentAllocator::calc_ext_per_segm_stats(const Segment& segm, bool is_alloc) {
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

    assert(StatsExtPerSegmLen == 8);

    if (is_alloc) {
        ++in_use_ext_per_segm[index];
    } else {
        --in_use_ext_per_segm[index];
    }
}

void PrintTo(const SegmentAllocator& alloc, std::ostream* out) {
    struct SegmentAllocator::stats_t st = alloc.stats();
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << "Calls to alloc:    " << std::setfill(' ') << std::setw(12) << st.alloc_call_cnt << "\n"
           << "Calls to dealloc:  " << std::setfill(' ') << std::setw(12) << st.dealloc_call_cnt << "\n"
           << "\n"

           << "Available to user: " << std::setfill(' ') << std::setw(12) << st.in_use_by_user_sz_kb << " kb\n"
           << "\n"

           << "Blocks in use:     " << std::setfill(' ') << std::setw(12) << st.in_use_blk_cnt << " blocks\n"
           << "- for suballoc:    " << std::setfill(' ') << std::setw(12) << st.in_use_blk_for_suballoc_cnt
           << " blocks\n"
           << "Subblocks in use:  " << std::setfill(' ') << std::setw(12) << st.in_use_subblk_cnt << " subblocks\n"
           << "\n"
           << "Blocks for suballocation:\n";


    assert(Extent::SUBBLK_CNT_PER_BLK == 16);
    for (unsigned i = 0; i < Extent::SUBBLK_CNT_PER_BLK / 2; ++i) {
        (*out) << "- with " << std::setfill(' ') << std::setw(2) << i + 1 << " subblks free: " << std::setfill(' ')
               << std::setw(12) << st.suballoc_bin_cnts[i] << " blocks"
               << "       "
               << "- with " << std::setfill(' ') << std::setw(2) << i + 9 << " subblks free: " << std::setfill(' ')
               << std::setw(12) << st.suballoc_bin_cnts[i + 8] << " blocks"
               << "\n";
    }

    (*out) << "\n"
           << "External fragmentation:       " << std::setfill(' ') << std::setw(12) << std::setprecision(2)
           << st.external_frag_sz_kb << " kb (" << std::setfill(' ') << std::setw(5) << std::fixed
           << std::setprecision(2) << (st.external_frag_rel * 100) << "%)\n"

           << "Internal fragmentation (avg): " << std::setfill(' ') << std::setw(12) << std::setprecision(2)
           << st.internal_frag_avg_sz_kb << " kb (" << std::setfill(' ') << std::setw(5) << std::fixed
           << std::setprecision(2) << (st.internal_frag_avg_rel * 100) << "%)\n"

           << "Allocable fragmentation:      " << std::setfill(' ') << std::setw(12) << std::setprecision(2)
           << st.allocable_internal_frag_sz_kb << " kb (" << std::setfill(' ') << std::setw(5) << std::fixed
           << std::setprecision(2) << (st.allocable_internal_frag_rel * 100) << "%)\n"
           << "\n"

           << "Data inlined:      " << std::setfill(' ') << std::setw(12) << st.in_use_inlined_sz << " bytes\n"
           << "\n"

           << "Extent in use:     " << std::setfill(' ') << std::setw(12) << st.in_use_ext_cnt << " extents\n"
           << "Segment in use:    " << std::setfill(' ') << std::setw(12) << st.in_use_segment_cnt << " segments\n"
           << "\n"

           << "Data fragmentation: \n"

           << "- only 0 extents:  " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[0] << " segments\n"
           << "- only 1 extents:  " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[1] << " segments\n"
           << "- only 2 extents:  " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[2] << " segments\n"
           << "- only 3 extents:  " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[3] << " segments\n"
           << "- only 4 extents:  " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[4] << " segments\n"
           << "- 5 to 8 extents:  " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[5] << " segments\n"
           << "- 9 to 16 extents: " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[6] << " segments\n"
           << "- 17 to * extents: " << std::setfill(' ') << std::setw(12) << st.in_use_ext_per_segm[7] << " segments\n"
           << "\n";

    out->flags(ioflags);
    assert(SegmentAllocator::StatsExtPerSegmLen == 8);
}
