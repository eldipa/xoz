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

    void release();

    bool is_at_the_end(const Extent& ext) const;
};
