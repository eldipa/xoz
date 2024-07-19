#include "xoz/err/bug.h"

#include <bitset>
#include <sstream>
#include <stdexcept>
#include <string>


namespace xoz {
InternalError::InternalError(const std::string& msg) {
    std::stringstream ss;
    ss << "[Possible bug detected] " << msg;

    this->msg = ss.str();
}

InternalError::InternalError(const F& msg): InternalError(msg.ss.str()) {}

const char* InternalError::what() const noexcept { return msg.data(); }
}  // namespace xoz
