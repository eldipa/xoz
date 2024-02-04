#include "xoz/ext/extent.h"

#include <bitset>
#include <iomanip>
#include <numeric>

#include "xoz/err/exceptions.h"

Extent::Extent(uint32_t blk_nr, uint16_t blk_cnt, bool is_suballoc): _blk_nr(blk_nr & 0x03ffffff), _blk_cnt(blk_cnt) {
    if (blk_nr & (~0x03ffffff)) {
        throw std::runtime_error((F() << "Invalid block number " << blk_nr << ", it is more than 26 bits. "
                                      << "Error when creating a new extent of block count " << blk_cnt
                                      << " (is suballoc: " << is_suballoc << ")")
                                         .str());
    }

    if (is_suballoc) {
        this->_blk_nr |= 0x80000000;
    }
}

void PrintTo(const Extent& ext, std::ostream* out) {
    std::ios_base::fmtflags ioflags = out->flags();

    if (ext.is_suballoc()) {
        (*out) << std::setfill('0') << std::setw(5) << std::hex << ext.blk_nr() << " [" << std::setfill('0')
               << std::setw(16) << std::bitset<16>(ext.blk_bitmap()) << "]";
    } else {
        (*out) << std::setfill('0') << std::setw(5) << std::hex << ext.blk_nr() << " " << std::setfill('0')
               << std::setw(5) << std::hex << ext.blk_nr() + ext.blk_cnt() << " [" << std::setfill(' ') << std::setw(4)
               << ext.blk_cnt() << "]";
    }

    out->flags(ioflags);

    /*
     * The following is an untested version of the code above
     * but using std::format.
     *
     * std::format was introduced in C++20 but very few compilers
     * support it. Too sad
     *
    if (ext.is_suballoc()) {
        (*out) << std::format(
                "{:05x} [{:016b}]",
                ext.blk_nr(),
                ext.blk_bitmap()
                );
    } else {
        (*out) << std::format(
                "{:05x} {:05x} [{:04d}]",
                ext.blk_nr(),
                ext.blk_nr() + ext.blk_cnt(),
                ext.blk_cnt()
                );
    }
    */
}

std::ostream& operator<<(std::ostream& out, const Extent& ext) {
    PrintTo(ext, &out);
    return out;
}

// Calculates the distance in blocks between a reference extent
// and the target extent taking into account the length of each
// extent.
//
// A forward distance is the count of blocks between the end of the
// reference extent and the begin of the target extent.
//
// A backward distance is the count of blocks between the end of the
// target extent and the begin of the reference extent.
//
// Both a forward and a backward distances are unsigned numbers
// (0 is a valid value).
//
// The method returns either a forward or a backward distance:
//  - if the target extent is *after* the reference extent, a forward
//    distance is returned.
//  - if the target extent is *before* the reference extent, a backward
//    distance is returned.
//
// If the reference and target extents overlap, an error is raised.
Extent::blk_distance_t Extent::distance_in_blks(const Extent& ref, const Extent& target) {
    const uint16_t ref_blk_cnt = ref.is_suballoc() ? 1 : ref.blk_cnt();
    const uint16_t target_blk_cnt = target.is_suballoc() ? 1 : target.blk_cnt();

    uint32_t blk_cnt = 0;
    bool is_backwards = false;

    if (ref.blk_nr() < target.blk_nr()) {
        // The current extent is *after* the reference extent
        uint32_t forward_dist = target.blk_nr() - ref.blk_nr();
        if (forward_dist < ref_blk_cnt) {
            throw ExtentOverlapError(ref, target, "(ext start is ahead ref)");
        }

        blk_cnt = forward_dist - ref_blk_cnt;
        is_backwards = false;

    } else if (target.blk_nr() < ref.blk_nr()) {
        // The current extent is *before* the reference extent
        uint32_t backward_dist = ref.blk_nr() - target.blk_nr();
        if (backward_dist < target_blk_cnt) {
            throw ExtentOverlapError(ref, target, "(ext start is behind ref)");
        }

        blk_cnt = backward_dist - target_blk_cnt;
        is_backwards = true;

    } else {
        // Both the reference extent starts at the same block number than the
        // target extent it is an error (overlap error). The only exception
        // is if the reference extent has zero blocks.
        if (ref_blk_cnt == 0) {
            blk_cnt = 0;
            is_backwards = false;
        } else {
            throw ExtentOverlapError(ref, target, "(at same start)");
        }
    }

    return {
            .blk_cnt = blk_cnt,
            .is_backwards = is_backwards,
            .is_near = blk_cnt <= 0x1ff,
    };
}

uint32_t Extent::calc_data_space_size(uint8_t blk_sz_order) const {
    const Extent& ext = *this;
    if (ext.is_empty()) {
        return 0;
    }

    if (ext.is_suballoc()) {
        return std::popcount(ext.blk_bitmap()) << (blk_sz_order - 4);
    } else {
        return ext.blk_cnt() << blk_sz_order;
    }
}
