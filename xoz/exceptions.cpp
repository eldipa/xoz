
#include "xoz/exceptions.h"
#include "xoz/repo.h"

#include <string>
#include <sstream>
#include <stdexcept>

OpenXOZError::OpenXOZError(const char* fpath, const char* msg) {
    std::stringstream ss;
    ss << "Open file '" << fpath << "' failed: " << msg;

    this->msg = ss.str();
}

const char* OpenXOZError::what() const noexcept {
    return this->msg.data();
}

InconsistentXOZ::InconsistentXOZ(const Repository& repo, const char* msg) {
    std::stringstream ss;
    ss << "Repository on file '" << repo.fpath << " (offset " << repo.repo_start_pos << ") seems inconsistent/corrupt."
       << "Reason: " << msg;

    this->msg = ss.str();
}

const char* InconsistentXOZ::what() const noexcept {
    return this->msg.data();
}
