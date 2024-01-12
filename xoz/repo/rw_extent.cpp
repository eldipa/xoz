#include <algorithm>
#include <cstring>

#include "xoz/exceptions.h"
#include "xoz/repo/repository.h"

uint32_t Repository::chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start) {
    if (ext.is_null()) {
        throw NullBlockAccess(F() << "The block 0x00 cannot be " << (is_read_op ? "read" : "written"));
    }

    assert(ext.blk_nr() != 0x0);

    // Checking for an OOB here *before* doing the calculate
    // of the usable space allows us to capture OOB with extent
    // of block count of 0 which otherwise would be silenced
    // (because a count of 0 means 0 usable space and the method
    // would return 0 (EOF) instead of detecting the bogus extent)
    fail_if_out_of_boundaries(ext, (F() << "Detected on a " << (is_read_op ? "read" : "write") << " operation.").str());

    const uint32_t usable_sz = ext.calc_data_space_size(gp.blk_sz_order);

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

uint32_t Repository::rw_suballocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz,
                                            uint32_t start) {
    const uint32_t subblk_sz = gp.blk_sz >> Extent::SUBBLK_SIZE_ORDER;
    const uint32_t subblk_cnt_per_blk = Extent::SUBBLK_CNT_PER_BLK;

    // Seek to the single block of the extent
    // and load it into a temporal buffer (full block)
    // not matter if is_read_op is true or false
    seek_read_blk(ext.blk_nr());
    std::vector<char> scratch(gp.blk_sz);
    fp.read(scratch.data(), gp.blk_sz);

    const uint16_t bitmap = ext.blk_bitmap();

    // If reading (is_read_op is true), copy from
    // the scratch block slices (subblocks) into
    // caller's data buffer reading the bitmap
    // from the highest to the lowest significant bit.
    //
    // If writing (is_read_op is false), do the same but
    // in the opposite direction: copy from data into
    // the scratch block.
    unsigned pscratch = 0, pdata = 0;
    uint32_t skip_offset = start;

    // note: to_rw_sz already takes into account
    // the start/skip_offset
    uint32_t remain_to_copy = to_rw_sz;

    for (unsigned i = 0; i < subblk_cnt_per_blk && remain_to_copy > 0; ++i) {
        const auto bit_selection = (1 << (subblk_cnt_per_blk - i - 1));

        if (bitmap & bit_selection) {
            if (skip_offset >= subblk_sz) {
                // skip the subblock entirely
                skip_offset -= subblk_sz;
            } else {
                const uint32_t copy_sz = std::min(subblk_sz - skip_offset, remain_to_copy);
                if (is_read_op) {
                    memcpy(data + pdata, scratch.data() + pscratch + skip_offset, copy_sz);
                } else {
                    memcpy(scratch.data() + pscratch + skip_offset, data + pdata, copy_sz);
                }

                // advance the data pointer
                pdata += copy_sz;

                // consume
                remain_to_copy -= copy_sz;

                // next iterations will copy full subblocks
                skip_offset = 0;
            }
        }

        // unconditionally advance the scratch pointer
        pscratch += subblk_sz;
    }

    // We didn't forget to read anything
    assert(remain_to_copy == 0);

    // Eventually at least 1 subblock was really copied
    // (otherwise we couldn't never decrement remain_to_copy to 0)
    assert(skip_offset == 0);

    if (is_read_op) {
        // do nothing else
    } else {
        // write back the updated scratch block
        seek_write_blk(ext.blk_nr());
        fp.write(scratch.data(), gp.blk_sz);
    }

    return to_rw_sz;
}

uint32_t Repository::rw_fully_allocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz,
                                               uint32_t start) {
    // this should never happen
    assert(ext.blk_cnt() > 0);

    // Seek to the begin of the extent and advance as many
    // bytes as the caller said
    if (is_read_op) {
        seek_read_blk(ext.blk_nr(), start);
    } else {
        seek_write_blk(ext.blk_nr(), start);
    }

    assert(to_rw_sz > 0);
    if (is_read_op) {
        fp.read(data, to_rw_sz);
    } else {
        fp.write(data, to_rw_sz);
    }

    return to_rw_sz;
}

uint32_t Repository::read_extent(const Extent& ext, std::vector<char>& data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t usable_sz = ext.calc_data_space_size(gp.blk_sz_order);
    const uint32_t reserve_sz = std::min(usable_sz, max_data_sz);
    data.resize(reserve_sz);

    const uint32_t read_ok = read_extent(ext, data.data(), reserve_sz, start);
    data.resize(read_ok);
    return read_ok;
}

uint32_t Repository::read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) {
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

uint32_t Repository::write_extent(const Extent& ext, const std::vector<char>& data, uint32_t max_data_sz,
                                  uint32_t start) {
    static_assert(sizeof(uint32_t) <= sizeof(size_t));
    if (data.size() > uint32_t(-1)) {
        throw std::runtime_error("");
    }

    return write_extent(ext, data.data(), uint32_t(std::min(data.size(), size_t(max_data_sz))), start);
}

uint32_t Repository::write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) {
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
