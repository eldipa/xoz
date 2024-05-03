#include "xoz/err/extent.h"

#include <bitset>
#include <sstream>
#include <stdexcept>
#include <string>

#include "xoz/blk/block_array.h"
#include "xoz/ext/extent.h"

ExtentOutOfBounds::ExtentOutOfBounds(const BlockArray& blkarr, const Extent& ext, const std::string& msg) {
    std::stringstream ss;

    if (ext.is_suballoc()) {
        if (ext.blk_bitmap() != 0) {
            ss << "The extent for suballocation [bitmap: " << std::bitset<Extent::SUBBLK_CNT_PER_BLK>(ext.blk_bitmap())
               << "] at block " << ext.blk_nr();
        } else {
            ss << "The extent for suballocation (empty) at block " << ext.blk_nr();
        }
    } else {
        if (ext.blk_cnt() > 0) {
            ss << "The extent of " << ext.blk_cnt() << " blocks that starts at block " << ext.blk_nr()
               << " and ends at block " << (ext.blk_nr() + ext.blk_cnt()) - 1;
        } else {
            ss << "The extent of " << ext.blk_cnt() << " blocks (empty) at block " << ext.blk_nr();
        }
    }

    if (ext.blk_nr() >= blkarr.past_end_blk_nr()) {
        ss << " completely falls out of bounds. ";
    } else {
        ss << " partially falls out of bounds. ";
    }

    if (blkarr.blk_cnt() > 0) {
        ss << "The blocks from " << blkarr.begin_blk_nr() << " to " << (blkarr.past_end_blk_nr() - 1)
           << " (inclusive) are within the bounds and allowed. ";
    } else {
        ss << "The block array has 0 blocks (it is empty, with " << blkarr.capacity() << " blocks of capacity)";
    }

    ss << msg;

    this->msg = ss.str();
}

ExtentOutOfBounds::ExtentOutOfBounds(const BlockArray& blkarr, const Extent& ext, const F& msg):
        ExtentOutOfBounds(blkarr, ext, msg.ss.str()) {}

const char* ExtentOutOfBounds::what() const noexcept { return msg.data(); }

ExtentOverlapError::ExtentOverlapError(const std::string& ref_name, const Extent& ref, const std::string& ext_name,
                                       const Extent& ext, const std::string& msg) {
    std::stringstream ss;

    if (ext.is_suballoc()) {
        ss << "The suballoc'd block ";
    } else {
        ss << "The extent ";
    }

    PrintTo(ext, &ss);

    if (ext_name.size() > 0) {
        ss << " (" << ext_name << ")";
    }

    ss << " overlaps with the ";

    if (ref.is_suballoc()) {
        ss << "suballoc'd block ";
    } else {
        ss << "extent ";
    }

    PrintTo(ref, &ss);

    if (ref_name.size() > 0) {
        ss << " (" << ref_name << ")";
    }

    if (msg.size() > 0) {
        ss << ": " << msg;
    }

    this->msg = ss.str();
}

ExtentOverlapError::ExtentOverlapError(const std::string& ref_name, const Extent& ref, const std::string& ext_name,
                                       const Extent& ext, const F& msg):
        ExtentOverlapError(ref_name, ref, ext_name, ext, msg.ss.str()) {}

ExtentOverlapError::ExtentOverlapError(const Extent& ref, const Extent& ext, const F& msg):
        ExtentOverlapError("reference extent", ref, "", ext, msg.ss.str()) {}
ExtentOverlapError::ExtentOverlapError(const Extent& ref, const Extent& ext, const std::string& msg):
        ExtentOverlapError("reference extent", ref, "", ext, msg) {}

const char* ExtentOverlapError::what() const noexcept { return msg.data(); }
