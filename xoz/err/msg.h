#pragma once

#include <sstream>
#include <string>

namespace xoz {
struct F {
    std::stringstream ss;

    template <typename T>
    F& operator<<(const T& val) {
        ss << val;
        return *this;
    }

    std::string str() { return ss.str(); }
};
}  // namespace xoz
