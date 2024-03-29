#include "xoz/err/xoz.h"

#include <bitset>
#include <sstream>
#include <stdexcept>
#include <string>

#include "xoz/repo/repository.h"

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
    ss << "Repository on file '" << repo.fpath << "" /* TODO " (offset " << repo.phy_repo_start_pos*/
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
