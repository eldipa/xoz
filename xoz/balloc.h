#pragma once

#include "xoz/ext/extent.h"
#include "xoz/repo/repo.h"
#include <list>
#include <cstdint>
#include <iostream>

struct BlockRequest {
    // How many blocks we are requiring
    uint16_t blk_cnt;

    // Try to allocate the requested blocks from this group
    //
    // The allocator may try to avoid a split or a group expansion
    // and try to allocate the requested blocks from an
    // neighbor group instead.
    uint32_t group;

    // Count how many splits can we do to the request to turn
    // in into a multiple non-contiguous requests.
    //
    // The allocator will try to avoid such splits and try to
    // find a contiguous array of blocks but it may not be possible.
    //
    // This counter says the maximum of splits allowed.
    //
    // A value of 0 means "do not split" and the allocator
    // will really try-hard to find a contiguous array.
    //
    // Note: if the requested block count is too large, it may
    // be impossible to find a contiguous array and the request
    // may be split not matter the max_split limit.
    uint16_t max_split;

    // When looking for free space in neighbor groups, only go
    // as this maximum as depth.
    //
    // If the requested group is L and L has as neighbors P and Q
    // and P has as neighbors X and Y, the relationship will look
    // as:
    //
    //  L +----> P +--> X
    //    |        |
    //    +--> Q   +--> Y
    //
    // A maximum depth of 1 will consider the groups P and Q in
    // addition to L. A maximum depth of 2 will consider the groups
    // X, Y, P and Q in addition to L.
    //
    // A maximum depth of 0 means allocate from the requested
    // group L only, even if that implies a split of the request
    // or a group expansion
    uint32_t max_neighbor_depth;

    // Promise that the object for which we are requesting blocks
    // to store will have a fixed size and it will not change
    // (not shrink, no grow/append)
    //
    // Most likely that any object will not have a size multiple
    // of the block size so the last requested block will probably be
    // under used.
    //
    // If the object has a fixed size, this last under used block
    // may be shared with other fixed size objects.
    //
    // If the caller cannot guarantee a fixed size, the safe bet
    // is set fixed_size_obj to false.
    bool fixed_size_obj;

    // How many bytes (not blocks) the object does require?
    // It should be between:
    //
    //      (blk_cnt-1) < obj_size/blk_sz <= blk_cnt
    //
    // If fixed_size_obj is False, the object may grow or shrink
    // after the allocation so this size is only a hint for the allocator.
    //
    // If fixed_size_obj is True, the object must maintain the
    // size specified.
    uint64_t obj_size;
};

class BlockAllocator {
    private:
        // A statically allocated array of 16 std::list
        //
        // Each nth bin (std::list) will contain the extent of free
        // blocks of lengths L where 2**n <= L < 2**n+1
        //
        // We support extents of up to 2**16 consecutive blocks
        // of length so at most we will have a bin of order 16
        //
        std::list<Extent> bins[16];

        // Just validate the assumption made above: extent's length/count
        // are encoded as 2 bytes (2**16).
        static_assert(Extent::BLK_CNT_FIELD_SIZE_IN_BYTES == 2);

        Repository& repo;
        const GlobalParameters gp;

        // Count how many blocks are free and ready to be allocated
        uint32_t free_blk_cnt;

        // Cout how many bytes are wasted inside a block because
        // the object size allocated was not a multiple of the block
        // size (aka internal fragmentation)
        uint64_t internal_fragmentation;

        // Higher block number seen so far and allocated by the repository
        // This number is >= 1 because block 0 is reserved.
        uint32_t highest_blk_nr;

    public:
        BlockAllocator(Repository& repo);

        std::list<Extent> alloc(const BlockRequest& req);
        void free(const Extent& ext);

        std::ostream& print_stats(std::ostream& out) const;

        void try_release();

    private:
        uint32_t remove_extent_ending_in(uint32_t end_blk_nr);
        Extent find_overlapping(const Extent& target) const;
};
