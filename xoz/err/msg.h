#pragma once

#include <sstream>
#include <string>

struct F {
    std::stringstream ss;

    template <typename T>
    F& operator<<(const T& val) {
        ss << val;
        return *this;
    }

    std::string str() { return ss.str(); }
};
