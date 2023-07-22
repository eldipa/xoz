
#include "xoz/exceptions.h"

#include <bitset>
#include <sstream>
#include <stdexcept>
#include <string>

#include "xoz/ext/extent.h"
#include "xoz/repo/repo.h"

OpenXOZError::OpenXOZError(const char* fpath, const std::string& msg) {
    std::stringstream ss;
    ss << "Open file '" << fpath << "' failed.\n";
    ss << msg;

    this->msg = ss.str();
}

OpenXOZError::OpenXOZError(const char* fpath, const F& msg): OpenXOZError(fpath, msg.ss.str()) {}

const char* OpenXOZError::what() const noexcept { return msg.data(); }

InconsistentXOZ::InconsistentXOZ(const Repository& repo, const std::string& msg) {
    std::stringstream ss;
    ss << "Repository on file '" << repo.fpath << " (offset " << repo.phy_repo_start_pos
       << ") seems inconsistent/corrupt.\n";
    ss << msg;

    this->msg = ss.str();
}

InconsistentXOZ::InconsistentXOZ(const Repository& repo, const F& msg): InconsistentXOZ(repo, msg.ss.str()) {}

InconsistentXOZ::InconsistentXOZ(const std::string& msg) {
    std::stringstream ss;
    ss << "Repository seems inconsistent/corrupt. " << msg;

    this->msg = ss.str();
}

InconsistentXOZ::InconsistentXOZ(const F& msg): InconsistentXOZ(msg.ss.str()) {}

const char* InconsistentXOZ::what() const noexcept { return msg.data(); }

WouldEndUpInconsistentXOZ::WouldEndUpInconsistentXOZ(const std::string& msg) { this->msg = msg; }

WouldEndUpInconsistentXOZ::WouldEndUpInconsistentXOZ(const F& msg): WouldEndUpInconsistentXOZ(msg.ss.str()) {}

const char* WouldEndUpInconsistentXOZ::what() const noexcept { return msg.data(); }

NullBlockAccess::NullBlockAccess(const std::string& msg) { this->msg = msg; }

NullBlockAccess::NullBlockAccess(const F& msg): NullBlockAccess(msg.ss.str()) {}

const char* NullBlockAccess::what() const noexcept { return msg.data(); }

ExtentOutOfBounds::ExtentOutOfBounds(const Repository& repo, const Extent& ext, const std::string& msg) {
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

    if (ext.blk_nr() >= repo.blk_total_cnt) {
        ss << " completely falls out of bounds. ";
    } else {
        ss << " partially falls out of bounds. ";
    }

    ss << "The block " << (repo.blk_total_cnt - 1) << " is the last valid before the end. ";

    ss << msg;

    this->msg = ss.str();
}

ExtentOutOfBounds::ExtentOutOfBounds(const Repository& repo, const Extent& ext, const F& msg):
        ExtentOutOfBounds(repo, ext, msg.ss.str()) {}

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

NotEnoughRoom::NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const std::string& msg) {
    std::stringstream ss;
    ss << "Requested " << requested_sz << " bytes but only " << available_sz << " bytes are available. ";

    ss << msg;

    this->msg = ss.str();
}

NotEnoughRoom::NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const F& msg):
        NotEnoughRoom(requested_sz, available_sz, msg.ss.str()) {}

const char* NotEnoughRoom::what() const noexcept { return msg.data(); }
