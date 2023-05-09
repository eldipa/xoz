#pragma once

#include <cstdint>

// An extent defines a contiguous array of <blk_cnt> blocks
// starting from <blk_nr>
struct Extent {
    uint32_t blk_nr;
    uint16_t blk_cnt;
};

