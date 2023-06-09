#pragma once

#include <cstdint>
#include <vector>
#include <iostream>
#include <cassert>

// An extent defines a contiguous array of <blk_cnt> full  blocks
// starting from <blk_nr>
//
// The <blk_nr> may point not to the begin of the array but to
// a single *shared* block which it is sub-divided in sub-blocks.
//
// Which sub-blocks belong to this extent is determinate by
// <blk_cnt> that act as a bitmap.
//
// A blk_nr is a 26-bits unsigned number in a uint32_t.
// We encode in the higher unused bits if the extent points
// to an array of full blocks or to a single shared block
// for sub-allocation.
//
class Extent {
    private:
    uint32_t _blk_nr;
    uint16_t _blk_cnt;

    public:

    // How many bytes are required to represent the blk_cnt field
    const static unsigned BLK_CNT_FIELD_SIZE_IN_BYTES = sizeof(((Extent*)0)->_blk_cnt);

    // Which is the size order of a subblock and how many subblocks
    // fit in a single block
    const static unsigned SUBBLK_SIZE_ORDER = 4;
    constexpr static unsigned SUBBLK_CNT_PER_BLK = (1 << SUBBLK_SIZE_ORDER);

    // Create an extent:
    //  - if is_suballoc is False, blk_nr points to the first
    //    block of a contiguous array of blk_cnt blocks
    //  - if is_suballoc is True, blk_nr points to a single
    //    blocks and blk_cnt is a 16-bits bitmap which tells
    //    which sub-blocks belong to this extent
    Extent(uint32_t blk_nr, uint16_t blk_cnt, bool is_suballoc) :
        _blk_nr(blk_nr & 0x03ffffff),
        _blk_cnt(blk_cnt)
    {
        if (blk_nr & (~0x03ffffff)) {
            // TODO? throw std::runtime_error("bad blk nr (more than 26 bits)");
        }

        if (is_suballoc) {
            this->_blk_nr |= 0x80000000;
        }
    }

    inline uint32_t blk_nr() const {
        return _blk_nr & 0x03ffffff;
    }

    inline uint16_t blk_cnt() const {
        assert(not is_suballoc());
        return _blk_cnt;
    }

    inline uint16_t blk_bitmap() const {
        assert(is_suballoc());
        return _blk_cnt; // on purpose, an alias of blk_cnt()
    }

    inline bool is_suballoc() const {
        return (bool)(_blk_nr & 0x80000000);
    }

    inline bool is_unallocated() const {
        return blk_nr() == 0x0;
    }

    inline void shrink_by(uint16_t cnt) {
        assert(not is_suballoc());
        assert(cnt <= _blk_cnt);
        _blk_cnt -= cnt;
    }

    uint32_t calc_usable_space_size(uint8_t blk_sz_order) const;

    struct blk_distance_t {
        const uint32_t blk_cnt;
        const bool is_backwards;
        const bool is_near;
    };

    static blk_distance_t distance_in_blks(const Extent& ref, const Extent& target);
};

