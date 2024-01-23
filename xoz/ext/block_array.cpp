#include "xoz/ext/block_array.h"

#include <algorithm>
#include <string>

#include "xoz/err/exceptions.h"
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

uint32_t BlockArray::read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) {
    return impl_read_extent(ext, data, max_data_sz, start);
}

uint32_t BlockArray::read_extent(const Extent& ext, std::vector<char>& data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t usable_sz = ext.calc_data_space_size(blk_sz_order());
    const uint32_t reserve_sz = std::min(usable_sz, max_data_sz);
    data.resize(reserve_sz);

    const uint32_t read_ok = read_extent(ext, data.data(), reserve_sz, start);
    data.resize(read_ok);
    return read_ok;
}

uint32_t BlockArray::write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) {
    return impl_write_extent(ext, data, max_data_sz, start);
}

uint32_t BlockArray::write_extent(const Extent& ext, const std::vector<char>& data, uint32_t max_data_sz,
                                  uint32_t start) {
    static_assert(sizeof(uint32_t) <= sizeof(size_t));
    if (data.size() > uint32_t(-1)) {
        throw std::runtime_error("");
    }

    return write_extent(ext, data.data(), uint32_t(std::min(data.size(), size_t(max_data_sz))), start);
}
