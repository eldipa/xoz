#include "test/testing_xoz.h"
#include <iomanip>

std::string testing_xoz::helpers::hexdump(const std::stringstream& fp, unsigned at, unsigned len) {
    std::ostringstream out;

    // Handle the case that we overflow the unsigned integer
    // Avoid the wrap around
    if (at + len < at) {
        len = unsigned(-1) - at;
    }

    // fp *must* be a const reference so calling str() does
    // not change its internal get pointer (seekg)
    std::string bytes = fp.str();
    for (unsigned i = at; i < bytes.size() and i < at + len; ++i) {
        out << std::setfill('0') << std::setw(2) << std::hex << (int)(unsigned char)bytes[i];
        if (i % 2 == 1 and i+1 < bytes.size() and i+1 < at + len)
            out << " ";
    }

    return out.str();
}

void testing_xoz::zbreak() {
}
