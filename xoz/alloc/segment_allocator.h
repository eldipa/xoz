#pragma once

#include <cstdint>
#include <list>

#include "xoz/alloc/free_map.h"
#include "xoz/alloc/subblock_free_map.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"

namespace xoz {
class BlockArray;

class SegmentAllocator {
public:
    const static uint8_t StatsExtPerSegmLen = 8;

    struct req_t {
        // During an allocation, the allocator will try to allocate the requested
        // bytes in a single contiguos Extent (no fragmentation) but it may not be possible.
        // In this case, segm_frag_threshold says the maximum number of extents that
        // the segment will be fragmented.
        //
        // This is a suggestion and the segment returned may have more extents than this
        // threshold says.
        uint16_t segm_frag_threshold;

        // Because the requested bytes for allocation may not be a multiple of the block size,
        // the last bytes would fall in a partially empty block. This generates internal
        // fragmentation and makes the performance of the allocator worse.
        // If allow_suballoc is True, the last bytes will be put in a block *shared* with other
        // allocations, hence minimizing the unfilled space.
        // If max_inline_sz is non-zero, the last bytes will not be put in a block but in the
        // same returned Segment object.
        //
        // Both allow_suballoc and max_inline_sz can be combined.
        uint8_t max_inline_sz;
        bool allow_suballoc;

        // If set, the allocator will allow and return a segment of one single extent
        // of contiguos blocks even if that requires to expand the underlying blk array.
        //
        // If set,
        //  - segm_frag_threshold must be 1
        //  - max_inline_sz must be 0
        //  - allow_suballoc must be false.
        //
        // Note: alloc of sizes larger than what a single Extent can handle will
        // throw error.
        bool single_extent;
    };


private:
    BlockArray* _blkarr;

    bool alloc_initialized;

    uint32_t blk_sz;
    uint8_t blk_sz_order;
    uint32_t subblk_sz;

    xoz::alloc::internals::TailAllocator tail;

    xoz::alloc::internals::FreeMap fr_map;
    xoz::alloc::internals::SubBlockFreeMap subfr_map;

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

    struct req_t default_req;

    uint32_t ops_blocked_stack_cnt;

public:
    constexpr static struct req_t XOZDefaultReq = {
            .segm_frag_threshold = 2, .max_inline_sz = 8, .allow_suballoc = true, .single_extent = false};

    /*
     * Partially creates a SegmentAllocator. To be functional at all, caller must call
     * manage_block_array() *once* with a BlockArray fully initialized. Once called,
     * the method cannot be called again.
     *
     * This 2-step creation allows the user to defer the setup of the block array for later
     * (perhaps because its parameters must be loaded from somewhere else and cannot be done
     * in a contructor).
     * */
    explicit SegmentAllocator(bool coalescing_enabled = true, uint16_t split_above_threshold = 0,
                              const struct req_t& default_req = XOZDefaultReq);

    void manage_block_array(BlockArray& blkarr);

    void set_default_alloc_requirements(const struct req_t& new_req) { default_req = new_req; }
    const struct req_t& get_default_alloc_requirements() const { return default_req; }

    Segment alloc(const uint32_t sz);
    Segment alloc(const uint32_t sz, const struct req_t& req);
    void dealloc(const Segment& segm, const bool zero_it = false);

    Extent alloc_single_extent(const uint32_t sz);
    void dealloc_single_extent(const Extent& ext);

    /*
     * Resize the given segment in place deallocating parts of the segment not longer
     * needed or allocating new parts.
     *
     * The resize is "in place" in a best-effort fashion:
     * When the size is increased, new extents (and possibly inline data) will be added to segment
     * to reach the desired size. If on the contrary the segment shrinks, extents are
     * removed (and possible a few are added, including, may be inline data).
     *
     * The implementation will try to avoid doing real reallocations of blocks to minimize
     * the copies and the writes, all at the expense of leaving a possibly more fragmented
     * segment. Do not use realloc to consolidate/compact a segment!
     *
     * In all the cases the data is preserved (obviously, if a shrink happen, that will be lost).
     * New space allocated has undefined data: caller should either override with useful data
     * or zero'd it.
     * */
    void realloc(Segment& segm, const uint32_t sz);
    void realloc(Segment& segm, const uint32_t sz, const struct req_t& req);

    /*
     * Initialize the segment allocator saying which segments/extents are already
     * allocated. Any space in between or between them and the boundaries of the
     * block array will be considered free and allowed to be allocated.
     *
     * Two flavors: one for allocated segments, the other for allocated extents.
     *
     * The method must be called once.
     * */
    void initialize_from_allocated(const std::list<Segment>& allocated_segms);
    void initialize_from_allocated(const std::list<Extent>& allocated_exts);
    void initialize_with_nothing_allocated();

    /*
     * Deallocate everything. This method should be called only if the caller
     * is sure that the current allocated extents/segments are not longer
     * needed.
     * This also reset the stats.
     *
     * Calling reset() implies release()
     * */
    void reset();

    /*
     * Release any pending-to-free space. The allocator may keep as "allocated"
     * some extents for performance reasons. With a call to release(), the allocator
     * will try to release anything pending as much as possible.
     * This release() will also request a release of any pending-to-free blocks
     * in the block array managed by this allocator.
     * */
    void release();

    struct i_stats_t {
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

        // External fragmentation is defined as how many blocks the xoz file
        // has (without counting metadata) and how many were allocated by
        // the Segment allocator.
        //
        // The difference, in block count, are the blocks unallocated by
        // the allocator (aka free) but not released back the xoz file
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

    struct stats_t {
        struct i_stats_t current;
        struct i_stats_t before_reset;
        uint64_t reset_cnt;
    };

    struct stats_t stats() const;

    inline const BlockArray& blkarr() const {
        assert(_blkarr);
        return *_blkarr;
    }


    typedef xoz::alloc::internals::ConstExtentMergeIterator<
            xoz::alloc::internals::FreeMap::const_iterator_by_blk_nr_t,
            xoz::alloc::internals::SubBlockFreeMap::const_iterator_by_blk_nr_t, true>
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
    friend std::ostream& operator<<(std::ostream& out, const SegmentAllocator& sg_alloc);

public:
    struct BlockOperations {
    public:
        explicit BlockOperations(SegmentAllocator& sg_alloc): sg_alloc(sg_alloc), hold(true) {
            sg_alloc.block_all_alloc_dealloc();
        }
        ~BlockOperations() { sg_alloc.unblock_all_alloc_dealloc(); }

        BlockOperations(BlockOperations&& other): sg_alloc(other.sg_alloc), hold(other.hold) { other.hold = false; }
        BlockOperations& operator=(BlockOperations&& other) = delete;

        BlockOperations(const BlockOperations&) = delete;
        BlockOperations& operator=(const BlockOperations&) = delete;

    private:
        SegmentAllocator& sg_alloc;
        bool hold;
    };

    /*
     * block_all_alloc_dealloc() prevents any allocation/deallocation,
     * even a release on the allocator.
     * This is meant to protect the allocator and the underlying block array
     * while during an operation that should not use the allocator but
     * it may call use it by mistake.
     * On any attempt, raise an exception.
     *
     * The unblock_all_alloc_dealloc() removes the blocking.
     *
     * Blocking/unblocking are stacked: 2 blocking requires 2 unblocking.
     * If the allocator isn't blocked (aka stack empty), an unblock call
     * will throw.
     *
     * Call block_all_alloc_dealloc_guard to create an object that calls
     * block_all_alloc_dealloc on its construction and
     * and unblock_all_alloc_dealloc in its destruction.
     * */
    void block_all_alloc_dealloc();
    void unblock_all_alloc_dealloc();
    BlockOperations block_all_alloc_dealloc_guard() { return BlockOperations(*this); }

private:
    uint32_t allocate_extents(Segment& segm, uint32_t blk_cnt_remain, uint16_t segm_frag_threshold,
                              bool ignore_segm_frag_threshold, bool use_parent);

    uint8_t allocate_subblk_extent(Segment& segm, uint8_t subblk_cnt_remain);

    bool provide_more_space_to_fr_map(uint16_t blk_cnt);
    bool provide_more_space_to_subfr_map();

    void reclaim_free_space_from_fr_map();
    void reclaim_free_space_from_subfr_map();

    void calc_ext_per_segm_stats(const Segment& segm, bool is_alloc);

    void fail_if_block_array_not_initialized() const;
    void fail_if_allocator_not_initialized() const;
    void fail_if_allocator_is_blocked() const;

    void _initialize_from_allocated(std::list<Extent>& allocated);

private:
    struct i_stats_t stats_before_reset;
    uint64_t reset_cnt;
};
}  // namespace xoz
