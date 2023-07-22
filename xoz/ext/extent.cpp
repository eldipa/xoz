#include "xoz/ext/extent.h"

#include <bitset>
#include <iomanip>

void PrintTo(const Extent& ext, std::ostream* out) {
    if (ext.is_suballoc()) {
        (*out) << std::setfill('0') << std::setw(5) << std::hex << ext.blk_nr() << " [" << std::setfill('0')
               << std::setw(16) << std::bitset<16>(ext.blk_bitmap()) << "]";
    } else {
        (*out) << std::setfill('0') << std::setw(5) << std::hex << ext.blk_nr() << " " << std::setfill('0')
               << std::setw(5) << std::hex << ext.blk_nr() + ext.blk_cnt() << " [" << std::setfill(' ') << std::setw(4)
               << ext.blk_cnt() << "]";
    }

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
