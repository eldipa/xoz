#pragma once

#include <stdexcept>
#include <string>

#include "xoz/err/msg.h"

namespace xoz {
class NotEnoughRoom: public std::exception {
private:
    std::string msg;

public:
    NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const std::string& msg);
    NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const F& msg);

    const char* what() const noexcept override;
};

class UnexpectedShorten: public std::exception {
private:
    std::string msg;

public:
    UnexpectedShorten(uint64_t requested_sz, uint64_t available_sz, uint64_t short_sz, const std::string& msg);
    UnexpectedShorten(uint64_t requested_sz, uint64_t available_sz, uint64_t short_sz, const F& msg);

    const char* what() const noexcept override;
};
}  // namespace xoz
