#include "xoz/alloc/segment_allocator.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>

#include "xoz/alloc/free_map.h"
#include "xoz/alloc/subblock_free_map.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/trace.h"

#define TRACE TRACE_ON(0x01)

#define TRACE_SECTION(x) TRACE << (x) << " "
#define TRACE_LINE TRACE << "    "

SegmentAllocator::SegmentAllocator(bool coalescing_enabled, uint16_t split_above_threshold,
                                   const struct req_t& default_req):
        _blkarr(nullptr),
        alloc_initialized(false),
        blk_sz(0),
        blk_sz_order(0),
        subblk_sz(0),
        tail(),
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
        internal_frag_avg_sz(0),
        default_req(default_req),
        ops_blocked_stack_cnt(0) {
    memset(in_use_ext_per_segm, 0, sizeof(in_use_ext_per_segm));
}

void SegmentAllocator::manage_block_array(BlockArray& blkarr) {
    if (_blkarr) {
        throw std::runtime_error("The segment allocator is already managing a block array.");
    }

    if (blkarr.blk_sz() == 0 or blkarr.blk_sz_order() == 0) {
        throw std::runtime_error(
                "Block array is not properly initialized yet and cannot be used/managed by the segment allocator.");
    }

    if (blkarr.subblk_sz() == 0 and default_req.allow_suballoc) {
        throw std::runtime_error("Block array has a sub-block size of 0 bytes and cannot be used for suballocation; "
                                 "this conflicts with the default alloc requirements.");
    }

    // Keep a reference
    _blkarr = &blkarr;

    blk_sz = _blkarr->blk_sz();
    blk_sz_order = _blkarr->blk_sz_order();
    subblk_sz = _blkarr->subblk_sz();

    tail.manage_block_array(blkarr);
}

Segment SegmentAllocator::alloc(const uint32_t sz) { return alloc(sz, default_req); }

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
    //
    fail_if_block_array_not_initialized();
    fail_if_allocator_not_initialized();
    fail_if_allocator_is_blocked();

    if (subblk_sz == 0 and req.allow_suballoc) {
        throw std::runtime_error("Subblock size 0 cannot be used for suballocation");
    }

    Segment segm;
    uint32_t sz_remain = sz;
    uint32_t avail_sz = 0;

    if (req.single_extent) {
        if (req.allow_suballoc or req.segm_frag_threshold != 1 or req.max_inline_sz != 0) {
            throw std::runtime_error("Alloc requirements allow_suballoc/segm_frag_threshold/max_inline_sz are "
                                     "incompatible with single_extent.");
        }
    }

    // How many blocks are needed?
    uint32_t blk_cnt_remain = sz_remain / blk_sz;
    sz_remain = sz_remain % blk_sz;

    // How many sub blocks are needed?
    uint32_t subblk_cnt_remain;
    if (req.allow_suballoc) {
        subblk_cnt_remain = sz_remain / subblk_sz;
        sz_remain = sz_remain % subblk_sz;
    } else {
        subblk_cnt_remain = 0;
    }

    // How many bytes are going to be inline'd?
    uint32_t inline_sz = sz_remain;
    sz_remain = 0;

    // Backpressure: if inline sz is greater than the limit,
    // put it into its own subblock
    // In this case the inline must be 0 (unused)
    //
    // By contruction inline_sz is less than a subblk sz, if subblk is allowed,
    // or less than a blk sz otherwise. So if we reach the maximum inline, all
    // the inline can be perfectly put into a new subblk/blk.
    if (inline_sz > req.max_inline_sz) {
        if (req.allow_suballoc) {
            assert(inline_sz <= subblk_sz);
            ++subblk_cnt_remain;
        } else {
            assert(inline_sz <= blk_sz);
            ++blk_cnt_remain;
        }
        inline_sz = 0;
    }

    // Backpressure: if subblk count can fill an entire block
    // do it
    //
    // The subblk_cnt_remain should be always less than SUBBLK_CNT_PER_BLK
    // due how the count is initialized. However, a +1 may happen due
    // the backpressure of the inline and the count may reach
    // SUBBLK_CNT_PER_BLK. In this case we can fill an entire block
    if (subblk_cnt_remain == Extent::SUBBLK_CNT_PER_BLK) {
        ++blk_cnt_remain;
        subblk_cnt_remain = 0;
    }

    // sanity checks: these should hold if we didn't have a mistake
    // in the computation above.
    assert(inline_sz <= req.max_inline_sz);
    assert(subblk_cnt_remain <= Extent::SUBBLK_CNT_PER_BLK);

    // due rounding/backpressure we may going to allocate more than the requested, hence the '>='
    assert(blk_cnt_remain * blk_sz + subblk_cnt_remain * subblk_sz + inline_sz >= sz);

    TRACE_SECTION("A") << std::setw(5) << sz << " b" << TRACE_ENDL;

    // Allocate extents trying to not expand the repository
    // but instead reusing free space already present even if
    // that means to fragment the segment a little more
    //
    // If single_extent, skip this as it may require expand the repository
    if (blk_cnt_remain and not req.single_extent) {
        TRACE_LINE << "to alloc,not grow -> " << blk_cnt_remain << "+" << subblk_cnt_remain << "+" << inline_sz
                   << "   -----v" << TRACE_ENDL;
        blk_cnt_remain = allocate_extents(segm, blk_cnt_remain, req.segm_frag_threshold, false, false);
    }

    // If we still require to allocate more blocks, just allow
    // to expand the repository to get more free space
    if (blk_cnt_remain) {
        TRACE_LINE << "to alloc,may grow -> " << blk_cnt_remain << "+" << subblk_cnt_remain << "+" << inline_sz
                   << "   -----v" << TRACE_ENDL;

        // At this point we may give up the fragmentation threshold and split/fragment even
        // more than the threshold in order to fulfill the alloc.
        // However, if single_extent is set, we will not do that and we hope that we can alloc
        // in a single try, including expand the repo if necessary.
        const bool ignore_segm_frag_threshold = not req.single_extent;
        blk_cnt_remain =
                allocate_extents(segm, blk_cnt_remain, req.segm_frag_threshold, ignore_segm_frag_threshold, true);
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

    avail_sz = segm.calc_data_space_size(blk_sz_order);

    // sanity check: we may allocate more if the user requeste to have no-inline
    // and the sz requested is not multiple of subblk_sz *or* to have no-inline
    // and no subblk and sz not multiple of blk_sz *or* some other combination.
    // In any case we must had allocated *enough* to store at least sz bytes
    assert(avail_sz >= sz);

    // update stats
    in_use_by_user_sz += avail_sz;
    in_use_ext_cnt += segm.ext_cnt();
    in_use_inlined_sz += segm.inline_data_sz();
    in_use_blk_cnt += segm.full_blk_cnt();  // blks for suballoc are counted in provide()
    in_use_subblk_cnt += segm.subblk_cnt();

    calc_ext_per_segm_stats(segm, true);

    internal_frag_avg_sz += segm.estimate_on_avg_internal_frag_sz(blk_sz_order);

    ++alloc_call_cnt;
    return segm;
no_free_space:
    // TODO catch and do free (revert any partially allocated extent)
    throw "no free space";
}

Extent SegmentAllocator::alloc_single_extent(const uint32_t sz) {
    fail_if_block_array_not_initialized();
    fail_if_allocator_not_initialized();
    fail_if_allocator_is_blocked();
    if (sz == 0) {
        // We can allocate a Segment of zero bytes, it is just an empty Segment
        // but we cannot allocate an Extent of zero bytes because it is not well defined
        // What would be its blk nr? Any number may introduce problems so it is better
        // an exception here and prevent any futher problem.
        throw std::runtime_error("Cannot allocate a single extent of zero bytes");
    }

    struct req_t req = {.segm_frag_threshold = 1, .max_inline_sz = 0, .allow_suballoc = false, .single_extent = true};
    assert(subblk_sz != 0 or not req.allow_suballoc);  // sanity chk, this is double chk in alloc() method

    Segment segm = this->alloc(sz, req);
    assert(segm.subblk_cnt() == 0);
    assert(segm.inline_data_sz() == 0);
    assert(not segm.has_end_of_segment());
    assert(segm.exts().size() == 1);

    return segm.exts().front();
}

void SegmentAllocator::dealloc(const Segment& segm) {
    fail_if_block_array_not_initialized();
    fail_if_allocator_not_initialized();
    fail_if_allocator_is_blocked();
    auto sz = segm.calc_data_space_size(blk_sz_order);

    TRACE_SECTION("D") << std::setw(5) << sz << " b" << TRACE_ENDL;
    TRACE_LINE << "* segment: " << segm << TRACE_ENDL;

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

    internal_frag_avg_sz -= segm.estimate_on_avg_internal_frag_sz(blk_sz_order);

    reclaim_free_space_from_subfr_map();
}

void SegmentAllocator::dealloc_single_extent(const Extent& ext) {
    fail_if_block_array_not_initialized();
    fail_if_allocator_not_initialized();
    fail_if_allocator_is_blocked();
    if (ext.is_empty()) {
        throw std::runtime_error("The extent to be deallocated cannot be empty.");
    }

    Segment segm;
    segm.add_extent(ext);
    this->dealloc(segm);
}

void SegmentAllocator::initialize_from_allocated(const std::list<Segment>& allocated_segms) {
    fail_if_block_array_not_initialized();

    // Collect all the allocated extents of all the segments (this includes full and suballoc'd blocks)
    std::list<Extent> allocated;
    for (const auto& segm: allocated_segms) {
        allocated.insert(allocated.end(), segm.exts().begin(), segm.exts().end());

        in_use_by_user_sz += segm.calc_data_space_size(blk_sz_order);
        in_use_ext_cnt += segm.ext_cnt();
        in_use_inlined_sz += segm.inline_data_sz();
        in_use_blk_cnt += segm.full_blk_cnt();
        in_use_subblk_cnt += segm.subblk_cnt();

        calc_ext_per_segm_stats(segm, true);
        internal_frag_avg_sz += segm.estimate_on_avg_internal_frag_sz(blk_sz_order);
    }

    _initialize_from_allocated(allocated);
}


void SegmentAllocator::initialize_from_allocated(const std::list<Extent>& allocated_exts) {
    fail_if_block_array_not_initialized();

    // Collect all the allocated extents of all the segments (this includes full and suballoc'd blocks)
    for (const auto& ext: allocated_exts) {
        in_use_by_user_sz += ext.calc_data_space_size(blk_sz_order);

        ++in_use_ext_cnt;

        if (ext.is_suballoc()) {
            in_use_subblk_cnt += ext.subblk_cnt();
        } else {
            in_use_blk_cnt += ext.blk_cnt();
        }

        ++in_use_ext_per_segm[1];  // the [1] is because we are counting for 1 extent
        internal_frag_avg_sz += ext.estimate_on_avg_internal_frag_sz(blk_sz_order);
    }

    std::list<Extent> allocated(allocated_exts);
    _initialize_from_allocated(allocated);
}

void SegmentAllocator::_initialize_from_allocated(std::list<Extent>& allocated) {
    fail_if_block_array_not_initialized();
    // Sort them by block number
    allocated.sort(Extent::cmp_by_blk_nr);

    // Now, track the subblocks in use
    std::map<uint32_t, uint16_t> suballocated_bitmap_by_nr;
    for (const auto& ext: allocated) {
        if (not ext.is_suballoc()) {
            continue;
        }

        const uint16_t suballocated_bitmap = suballocated_bitmap_by_nr[ext.blk_nr()];

        if (suballocated_bitmap & ext.blk_bitmap()) {
            const Extent ref(ext.blk_nr(), suballocated_bitmap, true);
            throw ExtentOverlapError("allocated", ref, "pending to allocate", ext,
                                     "error found during SegmentAllocator initialization");
        }

        // We must collect and merge all the suballocated bitmaps before
        // knowing which subblocks are truly free
        suballocated_bitmap_by_nr[ext.blk_nr()] |= ext.blk_bitmap();
    }

    // Provide the free subblocks and track the blocks for suballocation
    // as allocated full-block extents
    for (const auto& p: suballocated_bitmap_by_nr) {
        uint32_t blk_nr = p.first;
        uint16_t free_bitmap = ~p.second;  // negation of the allocated bitmap

        if (free_bitmap) {
            subfr_map.provide(Extent(blk_nr, free_bitmap, true));
        }

        // Count here how many blocks are for suballocation because
        // suballocated_bitmap_by_nr will have one and only one entry
        // per block for suballoction.
        ++in_use_blk_for_suballoc_cnt;

        // Also, add the blocks for suballoction to the in_use_blk_cnt total count.
        // This count was underestimated in the first for-loop because there we used
        // segm.full_blk_cnt() that consideres only blocks for non-suballocation.
        ++in_use_blk_cnt;

        // The allocated list already has an Extent in this blk_nr, in fact, it
        // may have multiple Extents in this same blk_nr, all of them for suballocation.
        // Here we add another Extent of 1 block-length marked for non-suballocation.
        //
        // In the for-loop below we are going to ignore any Extent for suballocation
        // so the only one that will count is this one we are creating here.
        //
        // This has the nice additional effect of checking out-of-bounds and overlap errors
        // in one single place
        allocated.push_back(Extent(blk_nr, 1, false));
    }

    // Sort them by block number, again
    allocated.sort(Extent::cmp_by_blk_nr);


    // Find the gaps between consecutive allocated extents (ignoring the ones for suballocation)
    // These gaps are the free extents to initialize the free maps
    uint32_t cur_nr = _blkarr->begin_blk_nr();
    Extent prev(0, 0, false);
    for (const auto& ext: allocated) {
        if (ext.is_suballoc()) {
            // already handled
            continue;
        }

        _blkarr->fail_if_out_of_boundaries(ext, "error found during SegmentAllocator initialization");

        // Technically this overlap check is not needed because fr_map.provide will
        // do it for us. Nevertheless, I prefer to double check and make an explicit
        // check here.
        Extent::fail_if_overlap(prev, ext);
        prev = ext;

        if (ext.blk_nr() == cur_nr) {
            // skip extent, there is no free blocks in between this and the previous extent
            // to add the free_map (there is no gap)
            cur_nr = ext.past_end_blk_nr();
            continue;
        } else if (ext.blk_nr() < cur_nr) {
            assert(false);
        }

        assert(ext.blk_nr() > cur_nr);

        uint32_t gap = ext.blk_nr() - cur_nr;

        // Non-suballocated Extent can be provided right now
        // We may require multiple extents if the gap is larger than 0xffff
        // This is because an Extent can handle only up to 0xffff free blocks
        // and the gap may perfectly be larger than that. TODO test this
        while (gap) {
            uint16_t len = uint16_t(std::min<uint32_t>(gap, 0xffff));
            fr_map.provide(Extent(cur_nr, len, false));

            gap -= len;
            cur_nr += len;
        }

        assert(cur_nr == ext.blk_nr());
        cur_nr = ext.past_end_blk_nr();
    }

    // Provide the last free extent (if any) that lies after the last
    // allocated extent and the end of the data section
    if (_blkarr->past_end_blk_nr() > cur_nr) {
        uint32_t gap = _blkarr->past_end_blk_nr() - cur_nr;

        while (gap) {
            uint16_t len = uint16_t(std::min<uint32_t>(gap, 0xffff));
            fr_map.provide(Extent(cur_nr, len, false));

            gap -= len;
            cur_nr += len;
        }

        assert(cur_nr == _blkarr->past_end_blk_nr());
    }

    alloc_initialized = true;
}

void SegmentAllocator::release() {
    fail_if_block_array_not_initialized();
    fail_if_allocator_not_initialized();
    fail_if_allocator_is_blocked();
    reclaim_free_space_from_subfr_map();
    reclaim_free_space_from_fr_map();
    tail.release();
}

SegmentAllocator::stats_t SegmentAllocator::stats() const {
    fail_if_block_array_not_initialized();
    fail_if_allocator_not_initialized();
    uint64_t repo_data_sz = (_blkarr->blk_cnt() << blk_sz_order);

    uint64_t external_frag_sz = (_blkarr->blk_cnt() - in_use_blk_cnt) << blk_sz_order;
    double external_frag_sz_kb = double(external_frag_sz) / double(1024.0);
    double external_frag_rel = repo_data_sz == 0 ? 0 : (double(external_frag_sz) / double(repo_data_sz));

    uint64_t internal_frag_avg_sz = this->internal_frag_avg_sz;
    double internal_frag_avg_sz_kb = double(internal_frag_avg_sz) / double(1024.0);
    double internal_frag_avg_rel =
            in_use_by_user_sz == 0 ? 0 : (double(internal_frag_avg_sz) / double(in_use_by_user_sz));

    uint64_t allocable_internal_frag_sz = ((in_use_blk_for_suballoc_cnt << blk_sz_order) -
                                           ((in_use_subblk_cnt << (blk_sz_order - Extent::SUBBLK_SIZE_ORDER))));
    double allocable_internal_frag_sz_kb = double(allocable_internal_frag_sz) / double(1024.0);
    double allocable_internal_frag_rel =
            in_use_blk_for_suballoc_cnt == 0 ?
                    0 :
                    (double(allocable_internal_frag_sz) / double((in_use_blk_for_suballoc_cnt << blk_sz_order)));


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
        TRACE_LINE << " * tail provided " << result.ext.blk_cnt() << " blocks" << TRACE_ENDL;
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

void SegmentAllocator::block_all_alloc_dealloc() {
    if (ops_blocked_stack_cnt >= (uint32_t)(-1)) {
        throw std::runtime_error("SegmentAllocator cannot be blocked because it was blocked too many times.");
    }

    ++ops_blocked_stack_cnt;
}

void SegmentAllocator::unblock_all_alloc_dealloc() {
    if (ops_blocked_stack_cnt <= 0) {
        throw std::runtime_error("SegmentAllocator cannot be unblocked because it is not blocked in the first place.");
    }

    --ops_blocked_stack_cnt;
}

std::ostream& operator<<(std::ostream& out, const SegmentAllocator& sg_alloc) {
    PrintTo(sg_alloc, &out);
    return out;
}


void SegmentAllocator::fail_if_block_array_not_initialized() const {
    if (not _blkarr) {
        throw std::runtime_error("Block array not initialized (managed). Missed call to manage_block_array?");
    }
}

void SegmentAllocator::fail_if_allocator_not_initialized() const {
    if (not alloc_initialized) {
        throw std::runtime_error("SegmentAllocator not initialized. Missed call to initialize()?");
    }
}

void SegmentAllocator::fail_if_allocator_is_blocked() const {
    if (ops_blocked_stack_cnt) {
        throw std::runtime_error("SegmentAllocator is blocked: no allocation/deallocation/release is allowed.");
    }
}
