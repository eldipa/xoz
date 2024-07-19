#include "xoz/err/size.h"

#include <bitset>
#include <sstream>
#include <stdexcept>
#include <string>

namespace xoz {
NotEnoughRoom::NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const std::string& msg) {
    std::stringstream ss;
    ss << "Requested " << requested_sz << " bytes but only " << available_sz << " bytes are available. ";

    ss << msg;

    this->msg = ss.str();
}

NotEnoughRoom::NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const F& msg):
        NotEnoughRoom(requested_sz, available_sz, msg.ss.str()) {}

const char* NotEnoughRoom::what() const noexcept { return msg.data(); }

UnexpectedShorten::UnexpectedShorten(uint64_t requested_sz, uint64_t available_sz, uint64_t short_sz,
                                     const std::string& msg) {
    std::stringstream ss;
    ss << "From " << available_sz << " bytes available, the requested " << requested_sz << " bytes returned only "
       << short_sz << " bytes. ";

    ss << msg;

    this->msg = ss.str();
}

UnexpectedShorten::UnexpectedShorten(uint64_t requested_sz, uint64_t available_sz, uint64_t short_sz, const F& msg):
        UnexpectedShorten(requested_sz, available_sz, short_sz, msg.ss.str()) {}

const char* UnexpectedShorten::what() const noexcept { return msg.data(); }
}  // namespace xoz
