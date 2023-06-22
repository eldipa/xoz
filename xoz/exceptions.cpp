
#include "xoz/exceptions.h"
#include "xoz/repo.h"
#include "xoz/extent.h"

#include <string>
#include <sstream>
#include <stdexcept>
#include <bitset>

OpenXOZError::OpenXOZError(const char* fpath, const std::string& msg) {
    std::stringstream ss;
    ss << "Open file '" << fpath << "' failed.\n";
    ss << msg;

    this->msg = ss.str();
}

OpenXOZError::OpenXOZError(const char* fpath, const F& msg) : OpenXOZError(fpath, msg.ss.str()) {}

const char* OpenXOZError::what() const noexcept {
    return msg.data();
}

InconsistentXOZ::InconsistentXOZ(const Repository& repo, const std::string& msg) {
    std::stringstream ss;
    ss << "Repository on file '" << repo.fpath << " (offset " << repo.phy_repo_start_pos << ") seems inconsistent/corrupt.\n";
    ss << msg;

    this->msg = ss.str();
}

InconsistentXOZ::InconsistentXOZ(const Repository& repo, const F& msg) : InconsistentXOZ(repo, msg.ss.str()) {}

const char* InconsistentXOZ::what() const noexcept {
    return msg.data();
}

WouldEndUpInconsistentXOZ::WouldEndUpInconsistentXOZ(const std::string& msg) {
    this->msg = msg;
}

WouldEndUpInconsistentXOZ::WouldEndUpInconsistentXOZ(const F& msg) : WouldEndUpInconsistentXOZ(msg.ss.str()) {}

const char* WouldEndUpInconsistentXOZ::what() const noexcept {
    return msg.data();
}

NullBlockAccess::NullBlockAccess(const std::string& msg) {
    this->msg = msg;
}

NullBlockAccess::NullBlockAccess(const F& msg) : NullBlockAccess(msg.ss.str()) {}

const char* NullBlockAccess::what() const noexcept {
    return msg.data();
}

ExtentOutOfBounds::ExtentOutOfBounds(const Repository& repo, const Extent& ext, const std::string& msg) {
    std::stringstream ss;

    if (ext.is_suballoc()) {
        if (ext.blk_bitmap() != 0) {
            ss << "The extent for suballocation [bitmap: "
               << std::bitset<Extent::SUBBLK_CNT_PER_BLK>(ext.blk_bitmap())
               << "] at block "
               << ext.blk_nr();
        } else {
            ss << "The extent for suballocation (empty) at block "
               << ext.blk_nr();
        }
    } else {
        if (ext.blk_cnt() > 0) {
            ss << "The extent of "
               << ext.blk_cnt()
               << " blocks that starts at block "
               << ext.blk_nr()
               << " and ends at block "
               << (ext.blk_nr() + ext.blk_cnt()) - 1;
        } else {
            ss << "The extent of "
               << ext.blk_cnt()
               << " blocks (empty) at block "
               << ext.blk_nr();
        }
    }

    if (ext.blk_nr() >= repo.blk_total_cnt) {
        ss << " completely falls out of bounds. ";
    } else {
        ss << " partially falls out of bounds. ";
    }

    ss << "The block "
       << (repo.blk_total_cnt - 1)
       << " is the last valid before the end. ";

    ss << msg;

    this->msg = ss.str();
}

ExtentOutOfBounds::ExtentOutOfBounds(const Repository& repo, const Extent& ext, const F& msg) : ExtentOutOfBounds(repo, ext, msg.ss.str()) {}

const char* ExtentOutOfBounds::what() const noexcept {
    return msg.data();
}
