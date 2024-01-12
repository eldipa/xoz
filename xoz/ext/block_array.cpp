#include "xoz/ext/block_array.h"

#include <string>

#include "xoz/exceptions.h"
#include "xoz/ext/extent.h"

void BlockArray::initialize_block_array(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr) {
    if (begin_blk_nr > past_end_blk_nr)
        throw std::runtime_error("begin_blk_nr > past_end_blk_nr is incorrect");

    _blk_sz = blk_sz;
    _begin_blk_nr = begin_blk_nr;
    _past_end_blk_nr = past_end_blk_nr;
}

BlockArray::BlockArray(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr) {
    initialize_block_array(blk_sz, begin_blk_nr, past_end_blk_nr);
}

BlockArray::BlockArray(): BlockArray(0, 0, 0) {}

uint32_t BlockArray::grow_by_blocks(uint16_t blk_cnt) {
    if (blk_cnt == 0)
        throw std::runtime_error("alloc of 0 blocks is not allowed");

    assert(not u32_add_will_overflow(_past_end_blk_nr, blk_cnt));

    auto blk_nr = impl_grow_by_blocks(blk_cnt);
    assert(blk_nr == past_end_blk_nr());

    _past_end_blk_nr += blk_cnt;
    return blk_nr;
}

void BlockArray::shrink_by_blocks(uint32_t blk_cnt) {
    if (blk_cnt == 0) {
        throw std::runtime_error("free of 0 blocks is not allowed");
    }

    if (blk_cnt > this->blk_cnt()) {
        throw std::runtime_error((F() << "free of " << blk_cnt << " blocks is not allowed because at most "
                                      << this->blk_cnt() << " blocks can be freed.")
                                         .str());
    }

    impl_shrink_by_blocks(blk_cnt);
    _past_end_blk_nr -= blk_cnt;

    assert(_begin_blk_nr <= _past_end_blk_nr);
}


// Call is_extent_within_boundaries(ext) and if it is false
// raise ExtentOutOfBounds with the given message
void BlockArray::fail_if_out_of_boundaries(const Extent& ext, const std::string& msg) const {
    if (not is_extent_within_boundaries(ext)) {
        throw ExtentOutOfBounds(*this, ext, msg);
    }
}
