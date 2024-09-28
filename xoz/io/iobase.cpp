#include "xoz/io/iobase.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "xoz/err/exceptions.h"
#include "xoz/mem/asserts.h"
#include "xoz/mem/casts.h"

#define TMP_BUF_SZ 64

namespace xoz {
IOBase::IOBase(const uint32_t src_sz):
        src_sz(src_sz), rd_min(0), wr_min(0), rd_end(src_sz), wr_end(src_sz), read_only(false), rd(0), wr(0) {}

void IOBase::rw_operation_exact_sz(const bool is_read_op, char* data, const uint32_t exact_sz) {
    if (read_only and not is_read_op) {
        throw std::runtime_error("Write operation is not allowed, io is read-only.");
    }

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

uint32_t IOBase::calc_seek(bool is_rd, uint32_t pos, uint32_t cur, IOBase::Seekdir way) const {
    const uint32_t min_pos = is_rd ? rd_min : wr_min;
    const uint32_t cur_end = is_rd ? rd_end : wr_end;
    switch (way) {
        case Seekdir::beg:
            if (pos > cur_end) {
                return cur_end;
            } else if (pos < min_pos) {
                return min_pos;
            }
            return pos;
        case Seekdir::end:
            if (src_sz < pos or src_sz - pos < min_pos) {
                return min_pos;
            } else if (src_sz - pos > cur_end) {
                return cur_end;
            }
            return src_sz - pos;
        case Seekdir::fwd:
            cur = cur + pos;
            if (cur < pos or cur > cur_end) {
                return cur_end;  // overflow
            }
            return cur;
        case Seekdir::bwd:
            if ((cur - min_pos) < pos) {
                return min_pos;  // underflow
            }
            cur = cur - pos;
            return cur;
    }
    assert(0);
    return min_pos;
}

void IOBase::limit(bool is_rd, uint32_t min_pos, uint32_t new_sz) {
    if (min_pos > src_sz) {
        min_pos = src_sz;  // handle overflow of min pos outside the real src sz
    }

    uint32_t end_pos = src_sz;  // assume overflow of size
    if (new_sz <= src_sz - min_pos) {
        end_pos = min_pos + new_sz;  // ok, it doesn't overflow, correct the end pos
    }

    // Update the limits and put the rd|wr pointers within the new range
    if (is_rd) {
        rd_min = min_pos;
        rd_end = end_pos;

        if (rd < rd_min) {
            rd = rd_min;
        } else if (rd > end_pos) {
            rd = end_pos;
        }
    } else {
        wr_min = min_pos;
        wr_end = end_pos;

        if (wr < wr_min) {
            wr = wr_min;
        } else if (wr > end_pos) {
            wr = end_pos;
        }
    }
}

uint32_t IOBase::chk_write_request_sizes(const std::vector<char>& data, const uint32_t sz) const {
    auto avail_sz = data.size();
    return chk_write_request_sizes(avail_sz, sz, "vector");
}

uint32_t IOBase::chk_write_request_sizes(std::istream& input, const uint32_t sz) const {
    auto begin = input.tellg();
    input.seekg(0, std::ios_base::end);
    auto avail_sz = assert_u32(input.tellg() - begin);

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

        return assert_u32(avail_sz);
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
    char pad[TMP_BUF_SZ];
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
    uint32_t i = 0;

    for (; i < len; ++i) {
        if (col == 0) {
            out << std::setfill('0') << std::setw(5) << std::hex << i << ": ";
        }

        const int c = int(static_cast<unsigned char>(read_char()));
        out << std::setfill('0') << std::setw(2) << std::hex << c << " ";

        if (col == 7) {
            out << " ";
        }

        if (col == 15) {
            out << "\n";
            col = 0;
        } else {
            ++col;
        }
    }
    if (col != 15) {
        out << "\n";
    }
    out << std::setfill('0') << std::setw(5) << std::hex << i << ": ";

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

void IOBase::rw_operation_exact_sz_iostream(std::ostream* const output, std::istream* const input,
                                            const uint32_t exact_sz, const uint32_t bufsz) {
    // Either it is a read operation (output is not null) or a write operation (input is not null)
    assert(output != nullptr or input != nullptr);
    assert(not(output != nullptr and input != nullptr));

    const bool is_read_op = output != nullptr;
    std::ios* iostr;
    if (is_read_op) {
        iostr = output;
    } else {
        iostr = input;
    }

    if (read_only and not is_read_op) {
        throw std::runtime_error("Write operation is not allowed, io is read-only.");
    }

    // Disable any IO exception by the moment
    auto exception_mask = iostr->exceptions();
    iostr->exceptions(std::ios_base::goodbit);

    const uint32_t remain_sz = is_read_op ? remain_rd() : remain_wr();

    if (remain_sz < exact_sz) {
        throw NotEnoughRoom(exact_sz, remain_sz,
                            F() << (is_read_op ? "Read " : "Write ") << "exact-byte-count operation at position "
                                << (is_read_op ? rd : wr) << " failed; detected before the "
                                << (is_read_op ? "read." : "write."));
    }

    uint32_t total_remaining = exact_sz;
    uint32_t rw_so_far = 0;

    std::vector<char> buf(bufsz);
    while (total_remaining and bool(*iostr)) {
        uint32_t buf_exact_sz = std::min(assert_u32(buf.size()), total_remaining);

        if (not is_read_op) {
            input->read(buf.data(), buf_exact_sz);
            if (!(*input)) {
                // Error detected, assume that the read was done partially.
                // Short-break if zero bytes were read
                buf_exact_sz = uint32_t(input->gcount());
                if (!buf_exact_sz) {
                    continue;  // the condition on the while-loop will break for us
                }
            }
        }

        uint32_t cur_rw_sz = rw_operation(is_read_op, buf.data(), buf_exact_sz);

        rw_so_far += cur_rw_sz;
        total_remaining -= cur_rw_sz;

        if (cur_rw_sz != buf_exact_sz) {
            throw UnexpectedShorten(exact_sz, remain_sz, rw_so_far,
                                    F() << (is_read_op ? "Read " : "Write ")
                                        << "exact-byte-count operation failed due a short "
                                        << (is_read_op ? "read " : "write ") << "(pointer left at position "
                                        << (is_read_op ? rd : wr) << " ).");
        }

        if (is_read_op) {
            output->write(buf.data(), cur_rw_sz);
            if (!(*output)) {
                // Error detected, assume that nothing was written and revert any progress
                // The while-loop will be broken automatically because of the check on output stream
                rw_so_far -= cur_rw_sz;
                total_remaining += cur_rw_sz;
            }
        }
    }

    if (total_remaining) {
        // The standard lib says that if there is a bit set, setting an exception mask will throw
        // the exception. Hopefully, the exception should have a more useful message than our default
        std::string ioerr = "unknown";
        try {
            iostr->exceptions(std::ios_base::badbit | std::ios_base::failbit | std::ios_base::eofbit);
        } catch (const std::ios_base::failure& err) {
            ioerr = std::string(err.what());
        }

        // Let's try to restore the exception mask, clearing any eof/error bit
        iostr->clear();
        iostr->exceptions(exception_mask);

        // Finally, throw our custom exception
        throw std::ios_base::failure(
                (F() << "From " << remain_sz << " bytes available, the requested " << exact_sz
                     << " bytes could not be completed due an IO error in the stream given by argument. "
                     << (is_read_op ? "Read " : "Write ") << "exact-byte-count operation completed only " << rw_so_far
                     << " bytes "
                     << "(pointer left at position " << (is_read_op ? rd : wr) << " ). IO error reported: [" << ioerr
                     << "]")
                        .str());
    }
}

void IOBase::copy_into_self(const uint32_t exact_sz) {
    if (remain_rd() < exact_sz) {
        throw NotEnoughRoom(exact_sz, remain_rd(),
                            F() << "Copy into self IO " << exact_sz << " bytes from read position " << rd
                                << " (this/src) to write position " << wr << " (dst) failed "
                                << "due not enough data to copy-from (src:rd); "
                                << "detected before the copy even started.");
    }
    if (remain_wr() < exact_sz) {
        throw NotEnoughRoom(exact_sz, remain_wr(),
                            F() << "Copy into self IO " << exact_sz << " bytes from read position " << rd
                                << " (this/src) to write position " << wr << " (dst) failed "
                                << "due not enough space to copy-into (dst:wr); "
                                << "detected before the copy even started.");
    }

    if (rd == wr) {
        // No copy, just move the pointers to simulate that something was done
        seek_rd(exact_sz, Seekdir::fwd);
        seek_wr(exact_sz, Seekdir::fwd);
    } else if (rd < wr and wr < (rd + exact_sz)) {
        // Overlap case 1: read buffer is before write buffer
        // We need to copy from the end of the read buffer
        copy_into_self_from_end(exact_sz);
    } else {
        // Two cases:
        //  - Overlap case 2: read buffer is after write buffer
        //  - No overlap
        //
        // In both cases we need to copy from the begin of the read buffer
        copy_into_self_from_start(exact_sz);
    }
}

void IOBase::copy_into(IOBase& dst, const uint32_t exact_sz) {
    if (remain_rd() < exact_sz) {
        throw NotEnoughRoom(exact_sz, remain_rd(),
                            F() << "Copy into another IO " << exact_sz << " bytes from read position " << rd
                                << " (this/src) to write position " << dst.wr << " (dst) failed "
                                << "due not enough data to copy-from (src:rd); "
                                << "detected before the copy even started.");
    }
    if (dst.remain_wr() < exact_sz) {
        throw NotEnoughRoom(exact_sz, dst.remain_wr(),
                            F() << "Copy into another IO " << exact_sz << " bytes from read position " << rd
                                << " (this/src) to write position " << dst.wr << " (dst) failed "
                                << "due not enough space to copy-into (dst:wr); "
                                << "detected before the copy even started.");
    }

    char buf[TMP_BUF_SZ];

    uint64_t remain = exact_sz;
    while (remain) {
        const auto chk_sz = assert_u32(std::min(sizeof(buf), remain));
        readall(buf, chk_sz);
        dst.writeall(buf, chk_sz);

        remain -= chk_sz;
    }

    // Note: rw and wr pointers are left at the end of the read/written
    // copied area as the method's do says.
}

void IOBase::copy_into_self_from_start(const uint32_t exact_sz) {

    // Note: because in copy_into_self_from_start where we read from
    // and where we write to may overlap, we *need* to copy what we read
    // into a separated buffer and then write it back
    // With the current IOBase API there is no other possibility but
    // it is important to leave this documented just in case
    char buf[TMP_BUF_SZ];

    uint64_t remain = exact_sz;
    while (remain) {
        const auto chk_sz = assert_u32(std::min(sizeof(buf), remain));
        readall(buf, chk_sz);
        writeall(buf, chk_sz);

        remain -= chk_sz;
    }

    // Note: rw and wr pointers are left at the end of the read/written
    // copied area as the method's do says.
}

void IOBase::copy_into_self_from_end(const uint32_t exact_sz) {
    // Note: because in copy_into_self_from_end where we read from
    // and where we write to may overlap, we *need* to copy what we read
    // into a separated buffer and then write it back
    // See copy_into_self_from_start
    char buf[TMP_BUF_SZ];

    seek_rd(exact_sz, Seekdir::fwd);
    seek_wr(exact_sz, Seekdir::fwd);

    uint64_t remain = exact_sz;
    while (remain) {
        const auto chk_sz = assert_u32(std::min(sizeof(buf), remain));
        seek_rd(chk_sz, Seekdir::bwd);
        seek_wr(chk_sz, Seekdir::bwd);

        readall(buf, chk_sz);
        writeall(buf, chk_sz);

        seek_rd(chk_sz, Seekdir::bwd);
        seek_wr(chk_sz, Seekdir::bwd);
        remain -= chk_sz;
    }

    // Note: we need to do these additional seeks so
    // the rw and wr pointers are left at the end of the read/written
    // copied area as the method's do says.
    seek_rd(exact_sz, Seekdir::fwd);
    seek_wr(exact_sz, Seekdir::fwd);
}
}  // namespace xoz
