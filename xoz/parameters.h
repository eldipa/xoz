#pragma once

#include <cstdint>
#include <ios>

struct GlobalParameters {
    // Block size in bytes. It will be a be a power of 2.
    uint32_t blk_sz = 4096;

    // Log base 2 of the block size in bytes.
    // Order of 10 are for block size of 1KB;
    // order of 11 are for block size of 2KB; and so on
    //
    // It must be less or equal than 16 (max block size of 64KB)
    uint8_t blk_sz_order = 12;


    // In which position of the file the repository
    // begins (in bytes).
    uint64_t phy_repo_start_pos = 0;

    // Reserve these count of blocks for any new repository.
    // This must be greater than or equal to 1.
    // It cannot be zero because the block 0 is always present
    // (allocated).
    uint32_t blk_init_cnt = 1;

    /*


    // Size in bytes of a group of blocks
    // Must be a multiple of blk sz
    uint32_t group_sz = (4096 << 3); // 32k, 8 blks

    // Count of blks in a group
    inline uint32_t grp_blk_cnt() const {
        return group_sz / blk_sz;
    }

    // Start offset
    uint32_t start_foffset = 0;

    // Segment count in a shared blk.
    // It must be multiple of 8 less than or equal to 256
    uint32_t segm_cnt_in_shared_blk = 256;

    uint32_t blk_cnt_in_group =

    // Calculate the file offset (in bytes) of the given blk
    inline uint32_t foffset(uint32_t blk_nr) {
        return (blk_nr * blk_sz) + start_foffset;
    }

    // The size of the free-segments bitmap in a shared blk
    inline uint32_t bitmap_sz() const {
        // We store the bitmap as an array of uint8_t with
        // each bit representing if a segment is free or not.
        //
        // Given a segment count multiple of 8, we get
        // the following bitmap size, in bytes
        return segm_cnt_in_shared_blk >> 3;
    }

    // The size in bytes for a single segment in a shared blk
    inline uint32_t segm_sz() const {
        return (blk_sz - bitmap_sz()) / segm_cnt_in_shared_blk;
    }
    */
};

