#include "test/testing_xoz.h"
#include <iomanip>

std::string testing_xoz::helpers::hexdump(std::stringstream& fp) {
    auto cur = fp.tellg();

    std::stringstream out;

    std::string bytes = fp.str();
    for (unsigned i = 0; i < bytes.size(); ++i) {
        out << std::setfill('0') << std::setw(2) << std::hex << (int)(unsigned char)bytes[i];
        if (i % 2 == 1 and i+1 < bytes.size())
            out << " ";
    }

    fp.seekg(cur);
    return out.str();
}
