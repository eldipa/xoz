#pragma once

#include <cstdint>
#include <vector>
#include <iostream>

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

    const static unsigned BLK_CNT_FIELD_SIZE_IN_BYTES = sizeof(((Extent*)0)->_blk_cnt);

    // Create an extent:
    //  - if is_suballoc is False, blk_nr points to the first
    //    block of a contiguous array of blk_cnt blocks
    //  - if is_suballoc is True, blk_nr points to a single
    //    blocks and blk_cnt is a 16-bits bitmap which tells
    //    which sub-blocks belong to this extent
    Extent(uint32_t blk_nr, uint16_t blk_cnt, bool is_suballoc) :
        _blk_nr(blk_nr),
        _blk_cnt(blk_cnt)
    {
        if (is_suballoc)
            this->_blk_nr |= 0x80000000;
    }

    // Create an extent with blk_nr formed from the 16 high bits (hi_blk_nr)
    // and the 16 low bits (lo_blk_nr)
    Extent(uint16_t hi_blk_nr, uint16_t lo_blk_nr, uint16_t blk_cnt, bool is_suballoc) :
        Extent(
                ((uint32_t(hi_blk_nr) << 16) | lo_blk_nr),
                blk_cnt,
                is_suballoc
              ) {}

    inline uint32_t blk_nr() const {
        return _blk_nr & 0x03ffffff;
    }

    inline uint16_t hi_blk_nr() const {
        return (_blk_nr & 0x03ff0000) >> 16;
    }

    inline uint16_t lo_blk_nr() const {
        return _blk_nr & 0x0000ffff;
    }

    inline uint16_t blk_cnt() const {
        return _blk_cnt;
    }

    inline uint16_t blk_bitmap() const {
        return _blk_cnt; // on purpose, an alias of blk_cnt()
    }

    inline bool is_suballoc() const {
        return (bool)(_blk_nr & 0x80000000);
    }

    inline void shrink_by(uint16_t cnt) {
        _blk_cnt -= cnt;
    }
};

struct ExtentGroup {
    std::vector<Extent> arr;

    bool inline_present;
    uint8_t inline_flags;
    std::vector<uint8_t> raw;

    ExtentGroup() : inline_present(false), inline_flags(0) {}

    static ExtentGroup createEmpty() {
        ExtentGroup exts;
        exts.inline_present = true;
        return exts;
    }

    // TODO offer a "borrow" variant
    void set_inline_data(const std::vector<uint8_t>& data) {
        inline_present = true;
        raw = data;
    }

    void add_extent(const Extent& ext) {
        arr.push_back(ext);
    }

    void clear_extents() {
        arr.clear();
    }
};

uint32_t calc_size_in_disk(const ExtentGroup& exts);
uint32_t calc_allocated_size(const ExtentGroup& exts, uint8_t blk_sz_order);

void write_ext_arr(std::ostream& fp, uint64_t endpos, const ExtentGroup& exts);
ExtentGroup load_ext_arr(std::istream& fp, uint64_t endpos);
