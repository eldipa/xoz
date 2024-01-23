#include "xoz/io/iobase.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "xoz/chk.h"
#include "xoz/err/exceptions.h"


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

uint32_t IOBase::chk_write_request_sizes(const std::vector<char>& data, const uint32_t sz) {
    // Ensure the caller is not trying to write more than uint32_t bytes
    // Larger buffers are not supported.
    static_assert(sizeof(uint32_t) <= sizeof(size_t));
    if (data.size() > uint32_t(-1)) {
        throw std::runtime_error("");
    }

    const uint32_t avail_sz = (uint32_t)data.size();

    // How much the caller wants to write?
    const uint32_t request_sz = sz == uint32_t(-1) ? avail_sz : sz;

    // If the user is requesting N but it is providing a buffer with M < N bytes,
    // it is a clear error in user's code. Fail.
    if (avail_sz < request_sz) {
        throw "";
    }

    return request_sz;
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
