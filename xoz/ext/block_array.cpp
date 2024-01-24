#include "xoz/ext/block_array.h"

#include <algorithm>
#include <string>

#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"

void BlockArray::initialize_block_array(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr) {
    if (blk_sz == 0) {
        throw std::runtime_error("blk_sz 0 is incorrect");
    }

    if (blk_sz < 16 or blk_sz % 16 != 0) {
        if (sg_alloc.get_default_alloc_requirements().allow_suballoc) {
            throw std::runtime_error("blk_sz too small/not multiple of 16 to be suballocated");
        }
    }

    if (begin_blk_nr > past_end_blk_nr) {
        throw std::runtime_error("begin_blk_nr > past_end_blk_nr is incorrect");
    }

    _blk_sz = blk_sz;
    _blk_sz_order = (uint8_t)u32_log2_floor(_blk_sz);
    _begin_blk_nr = begin_blk_nr;
    _past_end_blk_nr = past_end_blk_nr;

    sg_alloc.manage_block_array(*this);
}

BlockArray::BlockArray(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr, bool coalescing_enabled,
                       uint16_t split_above_threshold, const struct SegmentAllocator::req_t& default_req):
        _blk_sz(blk_sz),
        _blk_sz_order((uint8_t)u32_log2_floor(blk_sz)),
        _begin_blk_nr(begin_blk_nr),
        _past_end_blk_nr(past_end_blk_nr),
        sg_alloc(coalescing_enabled, split_above_threshold, default_req) {

    initialize_block_array(blk_sz, begin_blk_nr, past_end_blk_nr);
}

BlockArray::BlockArray(bool coalescing_enabled, uint16_t split_above_threshold,
                       const struct SegmentAllocator::req_t& default_req):
        _blk_sz(0),
        _blk_sz_order(0),
        _begin_blk_nr(0),
        _past_end_blk_nr(0),
        sg_alloc(coalescing_enabled, split_above_threshold, default_req) {}


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

uint32_t BlockArray::chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start) {
    // Checking for an OOB here *before* doing the calculate
    // of the usable space allows us to capture OOB with extent
    // of block count of 0 which otherwise would be silenced
    // (because a count of 0 means 0 usable space and the method
    // would return 0 (EOF) instead of detecting the bogus extent)
    fail_if_out_of_boundaries(ext, (F() << "Detected on a " << (is_read_op ? "read" : "write") << " operation.").str());

    const uint32_t usable_sz = ext.calc_data_space_size(_blk_sz_order);

    // If the caller wants to read/write beyond the usable space, return EOF
    if (usable_sz <= start) {
        return 0;  // EOF
    }

    // How much is readable/writeable and how much the caller is willing to
    // read/write?
    const uint32_t read_writeable_sz = usable_sz - start;
    const uint32_t to_read_write_sz = std::min(read_writeable_sz, max_data_sz);

    if (to_read_write_sz == 0) {
        // This could happen because the 'start' is at the
        // end of the usable space so there is no readable/writeable bytes
        // (aka read_writeable_sz == 0) which translates to EOF
        //
        // Or it could happen because max_data_sz is 0.
        // We return EOF and the caller should distinguish this
        // from a real EOF (this is how POSIX read() and write() works)
        return 0;
    }

    return to_read_write_sz;
}
