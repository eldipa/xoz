#pragma once

#include <string>

#include "xoz/ext/extent.h"
#include "xoz/mem/bits.h"


class BlockArray {
protected:
    void initialize_block_array(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr);

    virtual uint32_t impl_grow_by_blocks(uint16_t blk_cnt) = 0;
    virtual void impl_shrink_by_blocks(uint32_t blk_cnt) = 0;

private:
    uint32_t _blk_sz;
    uint32_t _begin_blk_nr;
    uint32_t _past_end_blk_nr;


public:
    BlockArray(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr);

    BlockArray();

    // Block definition
    inline uint32_t subblk_sz() const { return _blk_sz >> Extent::SUBBLK_SIZE_ORDER; }

    inline uint32_t blk_sz() const { return _blk_sz; }

    inline uint8_t blk_sz_order() const { return (uint8_t)u32_log2_floor(_blk_sz); }

    // Main primitive to allocate / free blocks
    //
    // This expands/shrinks the block array and the underlying
    // backend space.
    //
    // grow_by_blocks() returns the block number of the first
    // new allocated blocks.
    uint32_t grow_by_blocks(uint16_t blk_cnt);
    void shrink_by_blocks(uint32_t blk_cnt);

    // Return the block number of the first block with data
    // (begin_blk_nr) and the past-the-end data section
    // (past_end_blk_nr).
    //
    // Blocks smaller (strict) than begin_blk_nr()
    // and the blocks equal to or greater than past_end_blk_nr()
    // are reserved (it may not even exist in the backend)
    //
    // The total count of readable/writable data blocks by
    // the callers is (past_end_blk_nr() - begin_blk_nr())
    // and it may be zero (blk_cnt)
    inline uint32_t begin_blk_nr() const { return _begin_blk_nr; }

    inline uint32_t past_end_blk_nr() const { return _past_end_blk_nr; }

    inline uint32_t blk_cnt() const { return past_end_blk_nr() - begin_blk_nr(); }

    // Check if the extent is within the boundaries of the block array.
    inline bool is_extent_within_boundaries(const Extent& ext) const {
        return not(ext.blk_nr() < begin_blk_nr() or ext.blk_nr() >= past_end_blk_nr() or
                   ext.past_end_blk_nr() > past_end_blk_nr());
    }

    // Call is_extent_within_boundaries(ext) and if it is false
    // raise ExtentOutOfBounds with the given message
    void fail_if_out_of_boundaries(const Extent& ext, const std::string& msg) const;

    virtual ~BlockArray() {}
};
