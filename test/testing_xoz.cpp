#include "test/testing_xoz.h"
#include "xoz/io/iosegment.h"
#include <iomanip>
#include <fstream>

std::string testing_xoz::helpers::hexdump(const IOSegment& io, unsigned at, unsigned len) {
    IOSegment rdio = io.dup();
    rdio.seek_rd(0);

    std::vector<char> buf;
    rdio.readall(buf);

    return hexdump(buf, at, len);
}

std::string testing_xoz::helpers::hexdump(const std::vector<char>& buf, unsigned at, unsigned len) {
    std::stringstream fp;
    fp.write(buf.data(), buf.size());

    return hexdump(fp, at, len);
}

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

bool testing_xoz::helpers::are_all_zeros(const std::vector<char>& buf, unsigned at, unsigned len) {
    std::stringstream fp;
    fp.write(buf.data(), buf.size());

    return are_all_zeros(fp, at, len);
}

bool testing_xoz::helpers::are_all_zeros(const std::stringstream& fp, unsigned at, unsigned len) {
    if (at + len < at) {
        len = unsigned(-1) - at;
    }

    std::string bytes = fp.str();
    for (unsigned i = at; i < bytes.size() and i < at + len; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }

    return true;
}

const std::stringstream testing_xoz::helpers::file2mem(const char* path) {
    std::ifstream input(path);
    std::stringstream ss;
    ss << input.rdbuf();

    return ss;
}

void testing_xoz::zbreak() {
}
