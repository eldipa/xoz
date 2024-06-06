#pragma once

#include <cstdint>

#include "xoz/ext/extent.h"

class BlockArray;

class TailAllocator {
private:
    BlockArray* blkarr;
    void fail_if_block_array_not_initialized() const;

public:
    TailAllocator();

    void manage_block_array(BlockArray& blkarr);

    // Result of an allocation.
    struct alloc_result_t {
        Extent ext;
        bool success;
    };

    struct alloc_result_t alloc(uint16_t blk_cnt);

    bool dealloc(const Extent& ext);

    bool dealloc(const uint32_t blk_nr, const uint16_t blk_cnt);

    /*
     * Free any pending-to-free in the allocator and in the block
     * array.
     * */
    void release();

    /*
     * Dealloc all the currently allocated space, shrinking to zero
     * the block array managed by this allocator.
     * This method implies calling release() so any pending-to-free
     * block (either in the allocator or in the block array) will be
     * freed
     * */
    void reset();

    bool is_at_the_end(const Extent& ext) const;
};
