#pragma once

#include <cstdint>
#include <list>

#include "xoz/alloc/free_map.h"
#include "xoz/alloc/subblock_free_map.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/repo/repo.h"
#include "xoz/segm/segment.h"

class SegmentAllocator {
public:
    const static uint8_t StatsExtPerSegmLen = 8;

private:
    Repository& repo;

    TailAllocator tail;

    FreeMap fr_map;
    SubBlockFreeMap subfr_map;

    bool coalescing_enabled;

    uint64_t in_use_by_user_sz;
    uint64_t in_use_blk_cnt;
    uint64_t in_use_blk_for_suballoc_cnt;
    uint64_t in_use_subblk_cnt;

    uint64_t in_use_ext_cnt;
    uint64_t in_use_inlined_sz;

    uint64_t alloc_call_cnt;
    uint64_t dealloc_call_cnt;

    uint64_t internal_frag_avg_sz;

    uint64_t in_use_ext_per_segm[StatsExtPerSegmLen];

public:
    explicit SegmentAllocator(Repository& repo, bool coalescing_enabled = true, uint16_t split_above_threshold = 0);

    struct req_t {
        uint16_t segm_frag_threshold;
        uint8_t max_inline_sz;
        bool allow_suballoc;
    };

    constexpr static struct req_t DefaultReq = {.segm_frag_threshold = 2, .max_inline_sz = 8, .allow_suballoc = true};

    Segment alloc(const uint32_t sz, const struct req_t& req = SegmentAllocator::DefaultReq);
    void dealloc(const Segment& segm);

    void initialize(const std::list<Segment>& allocated_segms);
    void release();

    struct stats_t {
        // How many bytes are currently used (aka allocated / non-free)?
        uint64_t in_use_by_user_sz;
        double in_use_by_user_sz_kb;

        // How many blocks are currently in use?
        // How many of those blocks are being used for suballocation?
        // How many subblocks are in use?
        uint64_t in_use_blk_cnt;
        uint64_t in_use_blk_for_suballoc_cnt;
        uint64_t in_use_subblk_cnt;

        // How many extents are there? And segments?
        uint64_t in_use_ext_cnt;
        uint64_t in_use_segment_cnt;

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
        // (making not to shrink its size).
        //
        // The difference of blocks is then converted to bytes.
        //
        // A large number may indicate that the SegmentAllocator is not
        // doing a good job finding free space for alloc(), or it is not
        // doing a smart split or the alloc/dealloc pattern is kind of pathological.
        uint64_t external_frag_sz;
        double external_frag_sz_kb;
        double external_frag_rel;

        // Internal fragmentation "average" is defined as how many
        // bytes are allocated (as both blocks and subblocks and inline)
        // minus how many bytes were requested by the user/caller.
        // This assumes that the user will not use the extra space that
        // it is totally wasted.
        //
        // The internal fragmentation is wasted space that lives within
        // the block or subblock so it is not allocable further.
        // This means that the space is lost until the segment
        // that owns it is deallocated.
        //
        // While SegmentAllocator can track accurately the internal
        // fragmentation on alloc(), it cannot do it on dealloc() so
        // instead we provide an average:
        //
        //  - if subblocks are used, the fragmentation per segment
        //    is half subblock
        //  - if no subblocks are used *and* there is at least 1 block,
        //    the fragmentation per segment is half block
        //  - otherwise, the fragmentation per segment is 0,
        //    regardless if exists inlined data.
        //
        // Because SegmentAllocator may reduce the real fragmentation
        // moving data to the inline space which it is not counted
        // by the stats, the internal_frag_avg_sz over estimates it.
        //
        uint64_t internal_frag_avg_sz;
        double internal_frag_avg_sz_kb;
        double internal_frag_avg_rel;

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
        double allocable_internal_frag_sz_kb;
        double allocable_internal_frag_rel;

        // Count how many segments are that have certain count of extents
        // as a measure of the "split-ness", "spread" or "data fragmentation".
        //
        // For the first 5 indexes (array[0] to array[4] inclusive), the count is for
        // segments with 0 to 4 extents (inclusive).
        // For array[5], segments with from 5 to 8 extents (inclusive)
        // For array[6], segments with from 9 to 16 extents (inclusive)
        // For array[7], segments with 17 or more extents
        uint64_t in_use_ext_per_segm[StatsExtPerSegmLen];

        uint64_t suballoc_bin_cnts[Extent::SUBBLK_CNT_PER_BLK];
    };

    struct stats_t stats() const;


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

    // Pretty print. The signature of the method is required
    // by GoogleTest
    friend void PrintTo(const SegmentAllocator& alloc, std::ostream* out);

private:
    uint32_t allocate_extents(Segment& segm, uint32_t blk_cnt_remain, uint16_t segm_frag_threshold,
                              bool ignore_segm_frag_threshold, bool use_parent);

    uint8_t allocate_subblk_extent(Segment& segm, uint8_t subblk_cnt_remain);

    bool provide_more_space_to_fr_map(uint16_t blk_cnt);
    bool provide_more_space_to_subfr_map();

    void reclaim_free_space_from_fr_map();
    void reclaim_free_space_from_subfr_map();

    void calc_ext_per_segm_stats(const Segment& segm, bool is_alloc);
};
