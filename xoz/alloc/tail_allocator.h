#pragma once

#include <cstdint>

#include "xoz/ext/extent.h"
#include "xoz/repo/repository.h"

class TailAllocator {
private:
    Repository& repo;

public:
    explicit TailAllocator(Repository& repo);

    // Result of an allocation.
    struct alloc_result_t {
        Extent ext;
        bool success;
    };

    struct alloc_result_t alloc(uint16_t blk_cnt);

    bool dealloc(const Extent& ext);

    bool dealloc(const uint32_t blk_nr, const uint16_t blk_cnt);

    bool is_at_the_end(const Extent& ext) const;
};
