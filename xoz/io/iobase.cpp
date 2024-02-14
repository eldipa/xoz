#include "xoz/io/iobase.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <istream>
#include <vector>

#include "xoz/err/exceptions.h"
#include "xoz/mem/bits.h"


IOBase::IOBase(const uint32_t src_sz): src_sz(src_sz), rd(0), wr(0) {}

void IOBase::rw_operation_exact_sz(const bool is_read_op, char* data, const uint32_t exact_sz) {
    const uint32_t remain_sz = is_read_op ? remain_rd() : remain_wr();
    if (remain_sz < exact_sz) {
        throw NotEnoughRoom(exact_sz, remain_sz,
                            F() << (is_read_op ? "Read " : "Write ") << "exact-byte-count operation at position "
                                << (is_read_op ? rd : wr) << " failed; detected before the "
                                << (is_read_op ? "read." : "write."));
    }

    const uint32_t rw_total_sz = rw_operation(is_read_op, data, exact_sz);
    if (rw_total_sz != exact_sz) {
        throw UnexpectedShorten(exact_sz, remain_sz, rw_total_sz,
                                F() << (is_read_op ? "Read " : "Write ")
                                    << "exact-byte-count operation failed due a short "
                                    << (is_read_op ? "read " : "write ") << "(pointer left at position "
                                    << (is_read_op ? rd : wr) << " ).");
    }
}

uint32_t IOBase::calc_seek(uint32_t pos, uint32_t cur, IOBase::Seekdir way) const {
    switch (way) {
        case Seekdir::beg:
            if (pos > src_sz) {
                return src_sz;
            }

            return pos;
        case Seekdir::end:
            if (pos > src_sz) {
                return 0;
            } else {
                return src_sz - pos;
            }
        case Seekdir::fwd:
            cur = cur + pos;
            if (cur < pos or cur > src_sz) {
                return src_sz;  // overflow
            }
            return cur;
        case Seekdir::bwd:
            if (cur < pos) {
                return 0;  // underflow
            }
            cur = cur - pos;
            return cur;
    }
    assert(0);
    return 0;
}

uint32_t IOBase::chk_write_request_sizes(const std::vector<char>& data, const uint32_t sz) const {
    auto avail_sz = data.size();
    return chk_write_request_sizes(avail_sz, sz, "vector");
}

uint32_t IOBase::chk_write_request_sizes(std::istream& input, const uint32_t sz) const {
    auto begin = input.tellg();
    input.seekg(0, std::ios_base::end);
    auto avail_sz = input.tellg() - begin;

    input.seekg(begin);  // rewind

    return chk_write_request_sizes(avail_sz, sz, "file");
}

uint32_t IOBase::chk_write_request_sizes(const uint64_t avail_sz, const uint32_t sz, const char* input_name) const {
    static_assert(sizeof(uint32_t) <= sizeof(avail_sz));

    if (sz == uint32_t(-1)) {
        if (avail_sz > uint32_t(-1)) {
            throw std::overflow_error(
                    (F() << "Requested to write the entire input but input " << input_name << " is too large.").str());
        }

        return (uint32_t)avail_sz;
    } else {
        if (sz > avail_sz) {
            throw std::overflow_error((F() << "Requested to write " << sz << " bytes but input " << input_name
                                           << " has only " << avail_sz << " bytes.")
                                              .str());
        }

        return sz;
    }
}

void IOBase::fill(const char c, const uint32_t sz) {
    const auto hole = sz;
    char pad[64];
    memset(pad, c, sizeof(pad));
    for (unsigned batch = 0; batch < hole / sizeof(pad); ++batch) {
        writeall(pad, sizeof(pad));
    }
    writeall(pad, hole % sizeof(pad));
}


std::string IOBase::hexdump(uint32_t at, uint32_t len) {
    auto pos = tell_rd();
    seek_rd(at);

    if (len == uint32_t(-1)) {
        len = remain_rd();
    }

    len = std::min(remain_rd(), len);

    std::ostringstream out;
    uint8_t col = 0;

    for (uint32_t i = 0; i < len; ++i, ++col) {
        out << std::setfill('0') << std::setw(2) << std::hex << (int)(unsigned char)read_char();
        if (i % 2 == 1 and i + 1 < len) {
            if (col == 16) {
                out << "\n";
                col = 0;
            } else {
                out << " ";
            }
        }
    }

    seek_rd(pos);
    return out.str();
}

std::vector<char> IOBase::dump(uint32_t at, uint32_t len) {
    auto pos = tell_rd();
    seek_rd(at);

    if (len == uint32_t(-1)) {
        len = remain_rd();
    }

    len = std::min(remain_rd(), len);

    std::vector<char> buf;
    readall(buf, len);

    seek_rd(pos);
    return buf;
}
