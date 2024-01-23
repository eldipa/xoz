#pragma once

#include <stdexcept>
#include <string>

#include "xoz/err/msg.h"

class InternalError: public std::exception {
private:
    std::string msg;

public:
    explicit InternalError(const std::string& msg);
    explicit InternalError(const F& msg);

    const char* what() const noexcept override;
};
