#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "xoz/mem/bits.h"

// An extent can have 2 mutually exclusive interpretations:
//
//  - it either defines a contiguous array of <blk_cnt>
//    full blocks starting from <blk_nr>
//  - or it defines which sub-blocks inside of a single block
//    pointed by <blk_nr> belong to the extent (in this case,
//    <blk_cnt> is not a count but a bitmask that selects
//    the sub-blocks.
//
// A blk_nr is a 26-bits unsigned number in a uint32_t.
// We encode in the higher unused bits if the extent points
// to an array of full blocks or to a single shared block
// for sub-allocation.
//
// The count of sub-blocks that a single block has is entirely defined
// by SUBBLK_CNT_PER_BLK (and SUBBLK_SIZE_ORDER) and it is independent
// of the size (in bytes) of the block.
//
class Extent {
private:
    uint32_t _blk_nr;
    uint16_t _blk_cnt;

public:
    // How many bytes are required to represent the blk_cnt field
    const static unsigned BLK_CNT_FIELD_SIZE_IN_BYTES = sizeof(uint16_t);

    // Which is the size order of a subblock and how many subblocks
    // fit in a single block
    const static unsigned SUBBLK_SIZE_ORDER = 4;
    constexpr static unsigned SUBBLK_CNT_PER_BLK = (1 << SUBBLK_SIZE_ORDER);

    constexpr static unsigned MAX_BLK_CNT = (1 << 16) - 1;

    constexpr static unsigned MAX_BLK_NR = (1 << 26) - 1;

    static Extent EmptyExtent() { return Extent(0, 0, false); }

    // Create an extent:
    //  - if is_suballoc is False, blk_nr points to the first
    //    block of a contiguous array of blk_cnt blocks
    //  - if is_suballoc is True, blk_nr points to a single
    //    blocks and blk_cnt is a 16-bits bitmap which tells
    //    which sub-blocks belong to this extent
    Extent(uint32_t blk_nr, uint16_t blk_cnt, bool is_suballoc);

    inline uint32_t blk_nr() const { return _blk_nr & 0x03ffffff; }

    inline uint16_t blk_cnt() const {
        assert(not is_suballoc());
        return _blk_cnt;
    }

    // Block number of the past-the-end block
    // It works even when is_suballoc is True (assumed block count of 1)
    // or even if blk_cnt is 0.
    inline uint32_t past_end_blk_nr() const {
        const uint16_t cnt = is_suballoc() ? 1 : blk_cnt();
        const uint32_t nr = blk_nr() + cnt;

        assert(nr >= blk_nr());
        return nr;
    }

    inline uint16_t blk_bitmap() const {
        assert(is_suballoc());
        return _blk_cnt;  // on purpose, an alias of blk_cnt()
    }

    inline uint8_t subblk_cnt() const {
        assert(is_suballoc());
        return u16_count_bits(_blk_cnt);
    }

    inline bool is_suballoc() const { return bool(_blk_nr & 0x80000000); }

    inline bool is_empty() const { return is_suballoc() ? blk_bitmap() == 0 : blk_cnt() == 0; }

    inline void shrink_by(uint16_t cnt) {
        assert(not is_suballoc());
        assert(cnt <= _blk_cnt);
        _blk_cnt -= cnt;
    }

    inline void expand_by(uint16_t cnt) {
        assert(not is_suballoc());
        assert(not u16_add_will_overflow(_blk_cnt, cnt));
        _blk_cnt += cnt;
    }

    /*
     * Split the extent into two: the first (this) extent points to the same
     * block number as before and its block count is updated to new_cnt;
     * the second (return) extent points to immediately after the first and
     * it has the remaining blocks.
     *
     * If the extent is for suballoc, the method works the same but in
     * terms of subblocks.
     * */
    Extent split(uint16_t new_cnt) {
        if (is_suballoc()) {
            [[maybe_unused]] uint16_t orig_cnt = subblk_cnt();
            assert(subblk_cnt() >= new_cnt);

            uint16_t subblks_to_transfer = subblk_cnt() - new_cnt;
            uint16_t cur_bitmap = _blk_cnt;
            uint16_t new_bitmap = 0;

            for (unsigned i = 0; i < Extent::SUBBLK_CNT_PER_BLK && subblks_to_transfer > 0; ++i) {
                const auto bit_selection = (1 << i);

                if (cur_bitmap & bit_selection) {
                    new_bitmap |= uint16_t(cur_bitmap & bit_selection);
                    --subblks_to_transfer;
                }
            }

            Extent ext2(blk_nr(), new_bitmap, true);

            // substract the new_bitmap from the current bitmap
            _blk_cnt = uint16_t(_blk_cnt & (~new_bitmap));
            assert(subblk_cnt() == new_cnt);

            assert(subblk_cnt() + ext2.subblk_cnt() == orig_cnt);
            return ext2;

        } else {
            [[maybe_unused]] uint16_t orig_cnt = blk_cnt();
            assert(_blk_cnt >= new_cnt);

            Extent ext2(assert_u32(_blk_nr + new_cnt), assert_u16(_blk_cnt - new_cnt), false);

            _blk_cnt = new_cnt;

            assert(blk_cnt() + ext2.blk_cnt() == orig_cnt);
            return ext2;
        }
    }

    inline void move_to(uint32_t blk_nr) { _blk_nr = (blk_nr & 0x03ffffff) | (_blk_nr & 0xfc000000); }

    inline void set_bitmap(uint16_t bitmap) {
        assert(is_suballoc());
        _blk_cnt = bitmap;
    }

    inline Extent as_suballoc() const {
        if (is_suballoc()) {
            return *this;
        }

        assert(blk_cnt() == 1);
        return Extent(blk_nr(), 0xffff, true);
    }

    inline bool can_be_for_suballoc() const { return blk_cnt() == 1; }

    inline bool can_be_single_blk() const { return blk_bitmap() == 0xffff; }

    inline Extent as_not_suballoc() const {
        if (not is_suballoc()) {
            return *this;
        }

        assert(blk_bitmap() == 0xffff);
        return Extent(blk_nr(), 1, false);
    }

    // Return the size in bytes of the space referenced by the
    // blocks (or subblocks) of this extent.
    uint32_t calc_data_space_size(uint8_t blk_sz_order) const;

    uint32_t estimate_on_avg_internal_frag_sz(uint8_t blk_sz_order) const;

    struct blk_distance_t {
        const uint32_t blk_cnt;
        const bool is_backwards;
        const bool is_near;
    };

    static blk_distance_t distance_in_blks(const Extent& ref, const Extent& target);
    static void fail_if_overlap(const Extent& ref, const Extent& target) { distance_in_blks(ref, target); }

    inline bool operator==(const Extent& other) const {
        return _blk_nr == other._blk_nr and _blk_cnt == other._blk_cnt;
    }

    inline bool operator!=(const Extent& other) const { return _blk_nr != other._blk_nr or _blk_cnt != other._blk_cnt; }

    // Pretty print. The signature of the method is required
    // by GoogleTest
    friend void PrintTo(const Extent& ext, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const Extent& ext);

    static bool cmp_by_blk_nr(const Extent& a, const Extent& b) { return a.blk_nr() < b.blk_nr(); }

    // Copy are allowed
    Extent(const Extent&) = default;
    Extent& operator=(const Extent&) = default;

    struct Compare {
        bool operator()(const Extent& lhs, const Extent& rhs) const {
            if (lhs.blk_nr() != rhs.blk_nr())
                return lhs.blk_nr() < rhs.blk_nr();

            if (lhs.is_suballoc() and rhs.is_suballoc())
                return lhs.blk_bitmap() < rhs.blk_bitmap();

            if (not lhs.is_suballoc() and not rhs.is_suballoc())
                return lhs.blk_cnt() < rhs.blk_cnt();

            return lhs.is_suballoc();
        }
    };
};
