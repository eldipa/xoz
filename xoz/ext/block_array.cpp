#include "xoz/ext/block_array.h"

#include <string>

#include "xoz/exceptions.h"
#include "xoz/ext/extent.h"

void BlockArray::initialize_block_array(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t blk_cnt) {
    _blk_sz = blk_sz;
    _begin_blk_nr = begin_blk_nr;
    _blk_cnt = blk_cnt;
}

BlockArray::BlockArray(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t blk_cnt) {
    initialize_block_array(blk_sz, begin_blk_nr, blk_cnt);
}

BlockArray::BlockArray(): BlockArray(0, 0, 0) {}

uint32_t BlockArray::grow_by_blocks(uint16_t blk_cnt) {
    if (blk_cnt == 0)
        throw std::runtime_error("alloc of 0 blocks is not allowed");

    assert(not u32_add_will_overflow(_blk_cnt, blk_cnt));

    auto blk_nr = impl_grow_by_blocks(blk_cnt);
    assert(blk_nr == past_end_blk_nr());

    _blk_cnt += blk_cnt;
    return blk_nr;
}

void BlockArray::shrink_by_blocks(uint32_t blk_cnt) {
    if (blk_cnt == 0) {
        throw std::runtime_error("free of 0 blocks is not allowed");
    }

    if (blk_cnt > _blk_cnt) {
        throw std::runtime_error((F() << "free of " << blk_cnt << " blocks is not allowed because at most " << _blk_cnt
                                      << " blocks can be freed.")
                                         .str());
    }

    impl_shrink_by_blocks(blk_cnt);
    _blk_cnt -= blk_cnt;
}


// Call is_extent_within_boundaries(ext) and if it is false
// raise ExtentOutOfBounds with the given message
void BlockArray::fail_if_out_of_boundaries(const Extent& ext, const std::string& msg) const {
    if (not is_extent_within_boundaries(ext)) {
        throw ExtentOutOfBounds(*this, ext, msg);
    }
}