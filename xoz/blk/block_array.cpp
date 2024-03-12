#include "xoz/blk/block_array.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"

void BlockArray::initialize_block_array(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr) {
    fail_if_bad_blk_sz(blk_sz);

    const uint32_t min_blk_sz = Extent::SUBBLK_CNT_PER_BLK * 1;  // 1 byte per subblk
    if (blk_sz < min_blk_sz) {
        // block too small of SUBBLK_CNT_PER_BLK, disable the suballocation
        auto def = sg_alloc.get_default_alloc_requirements();
        def.allow_suballoc = false;
        sg_alloc.set_default_alloc_requirements(def);
    } else {
        assert(blk_sz % min_blk_sz == 0);
    }

    if (begin_blk_nr > past_end_blk_nr) {
        throw std::runtime_error("begin_blk_nr > past_end_blk_nr is incorrect");
    }

    _blk_sz = blk_sz;
    _blk_sz_order = (uint8_t)u32_log2_floor(_blk_sz);
    _begin_blk_nr = begin_blk_nr;
    _past_end_blk_nr = past_end_blk_nr;

    // The difference between _past_end_blk_nr and _real_past_end_blk_nr
    // is an implementation detail of BlockArray, it is not something that
    // the caller is aware so it is safe to assume that at the initialization moment
    // _real_past_end_blk_nr and _past_end_blk_nr are the same.
    _real_past_end_blk_nr = past_end_blk_nr;

    sg_alloc.manage_block_array(*this);
    blkarr_initialized = true;
}

BlockArray::BlockArray(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr, bool coalescing_enabled,
                       uint16_t split_above_threshold, const struct SegmentAllocator::req_t& default_req):
        _blk_sz(blk_sz),
        _blk_sz_order((uint8_t)u32_log2_floor(blk_sz)),
        _begin_blk_nr(begin_blk_nr),
        _past_end_blk_nr(past_end_blk_nr),
        _real_past_end_blk_nr(past_end_blk_nr),
        sg_alloc(coalescing_enabled, split_above_threshold, default_req),
        blkarr_initialized(false),
        _grow_call_cnt(0),
        _grow_expand_capacity_call_cnt(0),
        _shrink_call_cnt(0),
        _release_call_cnt(0) {

    initialize_block_array(blk_sz, begin_blk_nr, past_end_blk_nr);
}

BlockArray::BlockArray(bool coalescing_enabled, uint16_t split_above_threshold,
                       const struct SegmentAllocator::req_t& default_req):
        _blk_sz(0),
        _blk_sz_order(0),
        _begin_blk_nr(0),
        _past_end_blk_nr(0),
        _real_past_end_blk_nr(0),
        sg_alloc(coalescing_enabled, split_above_threshold, default_req),
        blkarr_initialized(false),
        _grow_call_cnt(0),
        _grow_expand_capacity_call_cnt(0),
        _shrink_call_cnt(0),
        _release_call_cnt(0) {}


uint32_t BlockArray::grow_by_blocks(uint16_t blk_cnt) {
    fail_if_block_array_not_initialized();
    if (blk_cnt == 0)
        throw std::runtime_error("alloc of 0 blocks is not allowed");

    assert(not u32_add_will_overflow(_past_end_blk_nr, blk_cnt));
    assert(not u32_add_will_overflow(_real_past_end_blk_nr, blk_cnt));

    assert(_begin_blk_nr <= _past_end_blk_nr);
    assert(_past_end_blk_nr <= _real_past_end_blk_nr);

    ++_grow_call_cnt;

    if (_real_past_end_blk_nr - _past_end_blk_nr >= blk_cnt) {
        // no need to grow, we can reuse the slack space
        uint32_t blk_nr = _past_end_blk_nr;
        _past_end_blk_nr += blk_cnt;
        return blk_nr;
    }

    // ok, the slack space is not enough, we need to grow but, by how much?
    // we may still use the remaining slack.
    // note: the cast is OK because:
    //
    //  - _real_past_end_blk_nr >= _past_end_blk_nr to it is non-negative
    //  - _real_past_end_blk_nr - _past_end_blk_nr < blk_cnt so fits in a uint16_t
    //    and the blk_cnt -= ... does not underflow.
    blk_cnt -= uint16_t(_real_past_end_blk_nr - _past_end_blk_nr);

    ++_grow_expand_capacity_call_cnt;
    auto [blk_nr, real_blk_cnt] = impl_grow_by_blocks(blk_cnt);
    assert(real_blk_cnt >= blk_cnt);

    assert(not u32_add_will_overflow(_past_end_blk_nr, real_blk_cnt));
    assert(not u32_add_will_overflow(_real_past_end_blk_nr, real_blk_cnt));

    // update the pointers
    _real_past_end_blk_nr += real_blk_cnt;
    _past_end_blk_nr = _real_past_end_blk_nr;

    return blk_nr;
}

void BlockArray::shrink_by_blocks(uint32_t blk_cnt) {
    fail_if_block_array_not_initialized();
    if (blk_cnt == 0) {
        throw std::runtime_error("free of 0 blocks is not allowed");
    }

    if (blk_cnt > this->blk_cnt()) {
        throw std::runtime_error((F() << "free of " << blk_cnt << " blocks is not allowed because at most "
                                      << this->blk_cnt() << " blocks can be freed.")
                                         .str());
    }

    ++_shrink_call_cnt;
    uint32_t real_blk_cnt = impl_shrink_by_blocks(blk_cnt);

    // We update the past_end_blk_nr pointer by blk_cnt backwards *as if*
    // all those blocks were truly released.
    // We track the *real* end with _real_past_end_blk_nr pointer
    _past_end_blk_nr -= blk_cnt;
    _real_past_end_blk_nr -= real_blk_cnt;

    // These must hold: the real_blk_cnt may be larger than the requested blk_cnt
    // but that only says that a previous shrink returned a real_blk_cnt < blk_cnt
    // hence have a "debt" of blocks to shrink.
    // When real_blk_cnt > blk_cnt it means that it is "paying off the debt" but
    // it must never happen that _past_end_blk_nr > _real_past_end_blk_nr.
    //
    // TODO chg these to exceptions
    assert(_begin_blk_nr <= _past_end_blk_nr);
    assert(_past_end_blk_nr <= _real_past_end_blk_nr);
}

uint32_t BlockArray::release_blocks() {
    fail_if_block_array_not_initialized();
    ++_release_call_cnt;
    uint32_t real_blk_cnt = impl_release_blocks();

    _real_past_end_blk_nr -= real_blk_cnt;

    assert(_begin_blk_nr <= _past_end_blk_nr);
    assert(_past_end_blk_nr <= _real_past_end_blk_nr);
    return real_blk_cnt;
}


// Call is_extent_within_boundaries(ext) and if it is false
// raise ExtentOutOfBounds with the given message
void BlockArray::fail_if_out_of_boundaries(const Extent& ext, const std::string& msg) const {
    fail_if_block_array_not_initialized();
    if (not is_extent_within_boundaries(ext)) {
        throw ExtentOutOfBounds(*this, ext, msg);
    }
}

uint32_t BlockArray::read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) {
    fail_if_block_array_not_initialized();
    const uint32_t to_read_sz = chk_extent_for_rw(true, ext, max_data_sz, start);
    if (to_read_sz == 0) {
        return 0;
    }

    if (ext.is_suballoc()) {
        return rw_suballocated_extent(true, ext, data, to_read_sz, start);
    } else {
        return rw_fully_allocated_extent(true, ext, data, to_read_sz, start);
    }
}

uint32_t BlockArray::read_extent(const Extent& ext, std::vector<char>& data, uint32_t max_data_sz, uint32_t start) {
    fail_if_block_array_not_initialized();
    const uint32_t usable_sz = ext.calc_data_space_size(blk_sz_order());
    const uint32_t reserve_sz = std::min(usable_sz, max_data_sz);
    data.resize(reserve_sz);

    const uint32_t read_ok = read_extent(ext, data.data(), reserve_sz, start);
    data.resize(read_ok);
    return read_ok;
}

uint32_t BlockArray::write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) {
    fail_if_block_array_not_initialized();
    const uint32_t to_write_sz = chk_extent_for_rw(false, ext, max_data_sz, start);
    if (to_write_sz == 0) {
        return 0;
    }

    if (ext.is_suballoc()) {
        return rw_suballocated_extent(false, ext, (char*)data, to_write_sz, start);
    } else {
        return rw_fully_allocated_extent(false, ext, (char*)data, to_write_sz, start);
    }
}

uint32_t BlockArray::write_extent(const Extent& ext, const std::vector<char>& data, uint32_t max_data_sz,
                                  uint32_t start) {
    fail_if_block_array_not_initialized();
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

uint32_t BlockArray::rw_suballocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz,
                                            uint32_t start) {
    const uint32_t _subblk_sz = subblk_sz();
    const uint32_t subblk_cnt_per_blk = Extent::SUBBLK_CNT_PER_BLK;

    const uint16_t bitmap = ext.blk_bitmap();

    unsigned blkoffset = 0, doffset = 0;
    uint32_t skipoffset = start;

    // note: to_rw_sz already takes into account
    // the start/skipoffset
    uint32_t remain_to_copy = to_rw_sz;

    for (unsigned i = 0; i < subblk_cnt_per_blk && remain_to_copy > 0; ++i) {
        const auto bit_selection = (1 << (subblk_cnt_per_blk - i - 1));

        if (bitmap & bit_selection) {
            if (skipoffset >= _subblk_sz) {
                // skip the subblock entirely
                skipoffset -= _subblk_sz;
            } else {
                const uint32_t copy_sz = std::min(_subblk_sz - skipoffset, remain_to_copy);
                if (is_read_op) {
                    impl_read(ext.blk_nr(), blkoffset + skipoffset, data + doffset, copy_sz);
                } else {
                    impl_write(ext.blk_nr(), blkoffset + skipoffset, data + doffset, copy_sz);
                }

                // advance the data pointer
                doffset += copy_sz;

                // consume
                remain_to_copy -= copy_sz;

                // next iterations will copy full subblocks
                skipoffset = 0;
            }
        }

        // unconditionally advance the offset for the inside block
        blkoffset += _subblk_sz;
    }

    // We didn't forget to read/write anything
    assert(remain_to_copy == 0);

    // Eventually at least 1 subblock was really copied (even if copied partially)
    // (otherwise we couldn't never had decremented remain_to_copy to 0)
    assert(skipoffset == 0);

    return to_rw_sz;
}

uint32_t BlockArray::rw_fully_allocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz,
                                               uint32_t start) {
    // this should never happen
    assert(ext.blk_cnt() > 0);
    assert(to_rw_sz > 0);

    // Seek to the begin of the extent and advance as many
    // bytes as the caller said
    if (is_read_op) {
        impl_read(ext.blk_nr(), start, data, to_rw_sz);
    } else {
        impl_write(ext.blk_nr(), start, data, to_rw_sz);
    }

    return to_rw_sz;
}

struct BlockArray::stats_t BlockArray::stats() const {
    fail_if_block_array_not_initialized();
    struct stats_t st = {.begin_blk_nr = _begin_blk_nr,
                         .past_end_blk_nr = _past_end_blk_nr,
                         .real_past_end_blk_nr = _real_past_end_blk_nr,

                         .blk_cnt = blk_cnt(),
                         .capacity = capacity(),
                         .total_blk_cnt = st.begin_blk_nr + capacity(),

                         .accessible_blk_sz_kb = double(blk_cnt() << blk_sz_order()) / double(1024.0),
                         .capacity_blk_sz_kb = double(capacity() << blk_sz_order()) / double(1024.0),
                         .total_blk_sz_kb = double((st.begin_blk_nr + capacity()) << blk_sz_order()) / double(1024.0),

                         .blk_sz = _blk_sz,
                         .blk_sz_order = _blk_sz_order,

                         .grow_call_cnt = _grow_call_cnt,
                         .grow_expand_capacity_call_cnt = _grow_expand_capacity_call_cnt,
                         .shrink_call_cnt = _shrink_call_cnt,
                         .release_call_cnt = _release_call_cnt};

    return st;
}

void PrintTo(const BlockArray& blkarr, std::ostream* out) {
    struct BlockArray::stats_t st = blkarr.stats();
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << "Calls to grow:    " << std::setfill(' ') << std::setw(12) << st.grow_call_cnt << "\n"
           << " - than expanded: " << std::setfill(' ') << std::setw(12) << st.grow_expand_capacity_call_cnt << "\n"
           << "Calls to shrink:  " << std::setfill(' ') << std::setw(12) << st.shrink_call_cnt << "\n"
           << "Calls to release: " << std::setfill(' ') << std::setw(12) << st.release_call_cnt << "\n"
           << "\n"

           << "Array layout:\n"
           << " - Begin at:      " << std::setfill(' ') << std::setw(12) << st.begin_blk_nr
           << " block number (inclusive) -"
           << " " << st.begin_blk_nr << " inaccessible blocks\n"
           << " - Past-end at:   " << std::setfill(' ') << std::setw(12) << st.past_end_blk_nr
           << " block number (exclusive) -"
           << " " << st.blk_cnt << " accessible blocks\n"
           << " - Alloc-end at:  " << std::setfill(' ') << std::setw(12) << st.real_past_end_blk_nr
           << " block number (exclusive) -"
           << " " << (st.blk_cnt - st.capacity) << " next-grow accessible blocks\n"
           << "\n"

           << "Accessible:       " << std::setfill(' ') << std::setw(12) << st.blk_cnt << " blocks, "
           << st.accessible_blk_sz_kb << " kb\n"
           << "Capacity:         " << std::setfill(' ') << std::setw(12) << st.capacity << " blocks, "
           << st.capacity_blk_sz_kb << " kb\n"
           << "Total:            " << std::setfill(' ') << std::setw(12) << st.total_blk_cnt << " blocks, "
           << st.total_blk_sz_kb << " kb\n";

    out->flags(ioflags);
}

std::ostream& operator<<(std::ostream& out, const BlockArray& blkarr) {
    PrintTo(blkarr, &out);
    return out;
}

void BlockArray::fail_if_block_array_not_initialized() const {
    if (not blkarr_initialized) {
        throw std::runtime_error("Block array not initialized (managed). Missed call to initialize_block_array?");
    }
}

void BlockArray::fail_if_bad_blk_sz(uint32_t blk_sz, uint32_t min_subblk_sz) {
    if (blk_sz == 0) {
        throw std::runtime_error("Block size cannot be zero.");
    }

    if (blk_sz != uint32_t(1 << u32_log2_floor(blk_sz))) {
        throw std::runtime_error((F() << "Block size must be a power of 2, but given " << blk_sz << ".").str());
    }

    if (min_subblk_sz != 0) {
        if (min_subblk_sz != uint32_t(1 << u32_log2_floor(min_subblk_sz))) {
            throw std::runtime_error(
                    (F() << "Sub block size must be a power of 2, but given " << min_subblk_sz << ".").str());
        }

        const uint32_t min_blk_sz = Extent::SUBBLK_CNT_PER_BLK * min_subblk_sz;
        if (min_blk_sz < min_subblk_sz) {
            // overflow
            throw std::runtime_error((F() << "Sub block size is too large, given " << min_subblk_sz << ".").str());
        }

        if (blk_sz < min_blk_sz) {
            throw std::runtime_error((F() << "Block size of " << blk_sz
                                          << "is too small to be suballocated with subblock sizes of " << min_subblk_sz
                                          << " (minimum).")
                                             .str());
        } else {
            assert(blk_sz % min_blk_sz == 0);
        }
    }
}

void BlockArray::fail_if_bad_blk_nr(uint32_t blk_nr) {
    if (blk_nr > Extent::MAX_BLK_NR) {
        throw std::runtime_error(
                (F() << "Block number " << blk_nr << " is larger than the maximum (" << Extent::MAX_BLK_NR << ").")
                        .str());
    }
}
