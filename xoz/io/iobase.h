#pragma once

#include <cassert>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "xoz/mem/endianness.h"

namespace xoz {
/*
 * Abstract base class to read from and write into a source like
 * a traditional C++ iostream but simpler and with a more
 * binary-oriented API.
 *
 * Subclasses must implement the rw_operation virtual method
 * to handle the real reads/writes into "the source" including
 * the correct update of the pointers rd (for reading) and wr (for writing).
 *
 * IOBase has no assumption of what "the source" can be: it could
 * be a buffer in memory, a file or other. The only requirement
 * is that the source cannot change its size (neither grow nor shrink).
 *
 * */
class IOBase {
public:
    explicit IOBase(const uint32_t src_sz);

    /*
     * Read data from the source or write data into.
     *
     * Two flavours of methods exist:
     *
     *  - the *all() methods that given a size they will read/write
     *    exactly all those bytes and if they cannot, they will throw.
     *
     *  - the *some() methods that given a size they will read/write
     *    at most that size but if they cannot due a limit/boundary,
     *    less bytes will be read/written without error.
     *    (if a file error happen, an exception will be thrown anyways)
     *
     * If a std::vector is used for reading, the vector is resized to
     * make room for the reading (either the given size of how much to read
     * or the size of the data available in the source, whatever is the
     * smaller). If the size of the std::vector is large enough,
     * no resize happens (aka no shrink).
     * The same goes for std::ostream, the reading of the io will assume
     * that the output is large enough (hence, no write should fail).
     *
     * If a std::vector is used and the size is not given, all the remaining
     * unread data in the source is read or all the available data in the vector
     * is written into the source, depending if it is a reading or a writing
     * operation. The same goes for std::ostream (for reading) and std::istream
     * (for writing).
     *
     * However, when writing (either from std::vector or std::istream), if the
     * input size is larger than the remaining io size for writing and error
     * will be throw.
     *
     * When a raw char* is given, the size must be set explicitly and it is
     * the caller's responsibility to ensure that raw pointer points to
     * an allocated memory large enough. This is a low-level API and the
     * std::vector/std::[io]stream API is preferred.
     * */
    void readall(char* data, const uint32_t exact_sz) { rw_operation_exact_sz(true, data, exact_sz); }

    void readall(std::vector<char>& data, const uint32_t exact_sz = uint32_t(-1)) {
        const uint32_t reserve_sz = exact_sz == uint32_t(-1) ? remain_rd() : exact_sz;
        if (data.size() < reserve_sz) {
            data.resize(reserve_sz);
        }
        rw_operation_exact_sz(true, data.data(), reserve_sz);
    }

    void readall(std::ostream& output, const uint32_t exact_sz = uint32_t(-1), const uint32_t bufsz = 1024) {
        rw_operation_exact_sz_iostream(&output, nullptr, exact_sz == uint32_t(-1) ? remain_rd() : exact_sz, bufsz);
    }

    uint32_t readsome(char* data, const uint32_t max_sz) { return rw_operation(true, data, max_sz); }

    uint32_t readsome(std::vector<char>& data, const uint32_t sz = uint32_t(-1)) {
        const uint32_t reserve_sz = sz == uint32_t(-1) ? remain_rd() : sz;
        if (data.size() < reserve_sz) {
            data.resize(reserve_sz);
        }
        return rw_operation(true, data.data(), reserve_sz);
    }

    void writeall(const char* data, const uint32_t exact_sz) {
        rw_operation_exact_sz(false, const_cast<char*>(data), exact_sz);
    }

    void writeall(const std::vector<char>& data, const uint32_t exact_sz = uint32_t(-1)) {
        const uint32_t request_sz = chk_write_request_sizes(data, exact_sz);
        rw_operation_exact_sz(false, const_cast<char*>(data.data()), request_sz);
    }

    void writeall(std::istream& input, const uint32_t exact_sz = uint32_t(-1), const uint32_t bufsz = 1024) {
        const uint32_t request_sz = chk_write_request_sizes(input, exact_sz);
        rw_operation_exact_sz_iostream(nullptr, &input, request_sz, bufsz);
    }

    uint32_t writesome(const char* data, const uint32_t max_sz) {
        return rw_operation(false, const_cast<char*>(data), max_sz);
    }

    uint32_t writesome(const std::vector<char>& data, const uint32_t sz = uint32_t(-1)) {
        const uint32_t request_sz = chk_write_request_sizes(data, sz);
        return rw_operation(false, const_cast<char*>(data.data()), request_sz);
    }

    /*
     * Writes <sz> bytes all with the same value <c> from the starting
     * point tell_wr().
     *
     * It is an error to try to write/fill more bytes than the are available.
     * To write/fill up to the end, call fill(c, remain_wr()).
     * */
    void fill(const char c, const uint32_t sz);

    /*
     * hexdump() return a hexadecimal string representation of the content of the io object
     * reading from it at the given <at> point and up to <len> bytes.
     * dump() does the same but returns a std::vector with the raw bytes (no hexdump).
     *
     * The <rd> pointer is ignored and restored at the end of the call.
     *
     * The method is mostly for introspection and debugging.
     * */
    std::string hexdump(uint32_t at = 0, uint32_t len = uint32_t(-1));
    std::vector<char> dump(uint32_t at = 0, uint32_t len = uint32_t(-1));

    uint32_t tell_rd() const { return rd; }

    uint32_t tell_wr() const { return wr; }

    enum Seekdir { beg = 0, end = 1, fwd = 2, bwd = 3 };

    void seek_rd(uint32_t pos, Seekdir way = Seekdir::beg) {
        rd = calc_seek(true, pos, rd, way);
        chk_within_limits(true);
    }

    void seek_wr(uint32_t pos, Seekdir way = Seekdir::beg) {
        wr = calc_seek(false, pos, wr, way);
        chk_within_limits(false);
    }

    uint32_t remain_rd() const { return rd_end - rd; }

    uint32_t remain_wr() const { return wr_end - wr; }

    void limit_rd(uint32_t min_pos, uint32_t new_sz) { limit(true, min_pos, new_sz); }

    void limit_wr(uint32_t min_pos, uint32_t new_sz) { limit(false, min_pos, new_sz); }

    /*
     * Limit the wr pointer to have 0 available space, making impossible to do any
     * write at all.
     * */
    void limit_to_read_only() { limit(false, tell_wr(), 0); }

    /*
     * Like limit_to_read_only(), make impossible to do any write at all.
     * However, with limit_to_read_only(), the restriction can be removed
     * calling limit_wr or similar.
     * With turn_read_only(), the read-only restriction is permanent.
     * */
    void turn_read_only() {
        limit_to_read_only();
        read_only = true;
    }

    uint8_t read_u8_from_le() {
        uint8_t num = 0;
        readall(reinterpret_cast<char*>(&num), sizeof(num));

        return u8_from_le(num);
    }

    void write_u8_to_le(uint8_t num) {
        num = u8_to_le(num);
        writeall(reinterpret_cast<char*>(&num), sizeof(num));
    }

    uint16_t read_u16_from_le() {
        uint16_t num = 0;
        readall(reinterpret_cast<char*>(&num), sizeof(num));

        return u16_from_le(num);
    }

    void write_u16_to_le(uint16_t num) {
        num = u16_to_le(num);
        writeall(reinterpret_cast<char*>(&num), sizeof(num));
    }

    uint32_t read_u32_from_le() {
        uint32_t num = 0;
        readall(reinterpret_cast<char*>(&num), sizeof(num));

        return u32_from_le(num);
    }

    void write_u32_to_le(uint32_t num) {
        num = u32_to_le(num);
        writeall(reinterpret_cast<char*>(&num), sizeof(num));
    }

    char read_char() {
        char c = 0;
        readall(&c, sizeof(c));

        return c;
    }

    class RewindGuard {
    private:
        IOBase& io;
        uint32_t rd;
        uint32_t wr;
        bool disabled;

    public:
        explicit RewindGuard(IOBase& io): io(io), rd(io.tell_rd()), wr(io.tell_wr()), disabled(false) {}
        ~RewindGuard() {
            if (not disabled) {
                io.seek_rd(rd);
                io.seek_wr(wr);
            }
        }

        void dont_rewind() { disabled = true; }

        RewindGuard(const RewindGuard&) = delete;
        RewindGuard& operator=(const RewindGuard&) = delete;
    };

    /*
     * Create a RAII object that will rewind the read/write pointers
     * to their values at the moment of this call.
     *
     * The object has a dont_rewind() method that if called it will
     * disable the rewind.
     *
     * This RewindGuard object is handy to implement mechanism where
     * the io is rewind if something went wrong (exception) or leave
     * it unchanged otherwise.
     *
     * If the caller wants to combine auto_rewind() with auto_restore_limits(),
     * it should call auto_rewind() first and auto_restore_limits() second otherwise
     * the rewind will happen before restoring the limits and it may not work.
     * */
    RewindGuard auto_rewind() { return RewindGuard(*this); }


    class RestoreLimitsGuard {
    private:
        IOBase& io;
        uint32_t rd_min;
        uint32_t rd_end;
        uint32_t wr_min;
        uint32_t wr_end;
        bool disabled;

    public:
        explicit RestoreLimitsGuard(IOBase& io):
                io(io), rd_min(io.rd_min), rd_end(io.rd_end), wr_min(io.wr_min), wr_end(io.wr_end), disabled(false) {}
        ~RestoreLimitsGuard() {
            if (not disabled) {
                assert(rd_end >= rd_min);
                assert(wr_end >= wr_min);
                io.limit_rd(rd_min, rd_end - rd_min);
                io.limit_wr(wr_min, wr_end - wr_min);
            }
        }

        void dont_restore() { disabled = true; }

        RestoreLimitsGuard(const RestoreLimitsGuard&) = delete;
        RestoreLimitsGuard& operator=(const RestoreLimitsGuard&) = delete;
    };

    /*
     * Create a RAII object that will restore the read/write limits to the values
     * that the IO object has at the moment of this call.
     *
     * The object has a dont_restore() method that if called no limit will be
     * restored.
     *
     * If the caller wants to combine auto_rewind() with auto_restore_limits(),
     * it should call auto_rewind() first and auto_restore_limits() second otherwise
     * the rewind will happen before restoring the limits and it may not work.
     * */
    RestoreLimitsGuard auto_restore_limits() { return RestoreLimitsGuard(*this); }

    /*
     * Copy exact_sz bytes from the current rd position into the current wr position.
     * Caller must call seek_rd/seek_wr to its desire to control the copy-from and copy-to
     * locations.
     * If there is not enough data to read or free space to write, an error will be thrown.
     *
     * The copy-from (src) and copy-to (dst) areas *can* overlap.
     *
     * After the copy, the rd and wr pointers are left at the current position
     * (before the call) plus exact_sz.
     *
     * If an error happen during the copy, the content may had been partially copied
     * and the rd and wr pointers are left in an unspecified state.
     * */
    void copy_into_self(const uint32_t exact_sz);

    /*
     * Like copy_into_self() but read from this (self) and writes into another io (dst).
     *
     * Both this (self) and dst *must* be io objects pointing to different areas
     * (their segment must not overlap).
     *
     * After the copy, the rd and wr pointers are left at the current position
     * (before the call) plus exact_sz.
     *
     * If an error happen during the copy, the content may had been partially copied
     * and the rd and wr pointers are left in an unspecified state.
     * */
    void copy_into(IOBase& dst, const uint32_t exact_sz);

    /*
     * The given buffer must have enough space to hold max_data_sz bytes The operation
     * will read/write up to max_data_sz bytes but it may less.
     *
     * The count of bytes read/written is returned.
     *
     * */
    virtual uint32_t /* protected; --not public-- */ rw_operation(const bool is_read_op, char* data,
                                                                  const uint32_t data_sz) = 0;

private:
    const uint32_t src_sz;

    uint32_t rd_min;
    uint32_t wr_min;
    uint32_t rd_end;
    uint32_t wr_end;

    bool read_only;

protected:
    uint32_t rd;
    uint32_t wr;

    /*
     * The given buffer must have enough space to hold exact_sz bytes.
     *
     * If the source has N remaining unread bytes / places to write such
     * N is less than exact_sz, then the method will throw (basically it cannot
     * by completed an operation of exact_sz bytes with less bytes).
     *
     * If after the operation execution the read/written bytes is different
     * than exact_sz, also it throws.
     * */
    void rw_operation_exact_sz(const bool is_read_op, char* data, const uint32_t exact_sz);

    /*
     * Like rw_operation_exact_sz, rw_operation_exact_sz_iostream ensures reading/writing
     * exact_sz bytes.
     *
     * The method works either reading from an input stream or writing into a output stream.
     * One and only one of them must be non-null.
     * Because this can be a slow operation the method does the reads/writes in batches
     * using a buffer of <bufsz> bytes.
     *
     * In addition to the errors that rw_operation_exact_sz could throw, this method
     * may throw std::ios_base::failure if an error happen with the input/output streams.
     * */
    void rw_operation_exact_sz_iostream(std::ostream* const output, std::istream* const input, const uint32_t exact_sz,
                                        const uint32_t bufsz);

    /*
     * Return the new read/write pointer with initial value <cur> as it is were updating
     * to the new position <pos> where this position can be interpreted as:
     *  - absolute position (way == Seekdir::beg)
     *  - absolute backward position (way == Seekdir::end)
     *  - relative to cur position in forward direction (way == Seekdir::fwd)
     *  - relative to cur position in backward direction (way == Seekdir::bwd)
     *
     * In any case if the calculated position goes beyond to the available space
     * the returned position is clamp to nearest extreme in the available range.
     * (this means that on underflow, the position returned is the minimum position;
     * on overflow the position returned is one-past the end of the available space).
     *
     * For the case of underflows (positions past the begin), the position returned
     * is [rd|wr]_min; for overflows, the position returned is [rd|rw]_end;
     *
     * Whatever this method operates with the read pointer or the write pointer,
     * it depends on is_rd boolean (the value of the cur parameter)
     *
     * No error happen in any case. The current rd|wr pointer (cur parameter) must
     * be within the rd|wr limits otherwise the computation is undefined.
     **/
    uint32_t calc_seek(bool is_rd, uint32_t pos, uint32_t cur, Seekdir way = Seekdir::beg) const;

    /*
     * Set limits on the rd/rw pointers defining a minimum position and the available space
     * from there (the new size).
     * If is_rd, the limits apply to rd only; otherwise to rw only.
     *
     * If after applying the limits the current rd/rw pointer is outside the limits,
     * perform a seek to move it inside the limits.
     * */
    void limit(bool is_rd, uint32_t min_pos, uint32_t new_sz);

    /*
     * Check that the rd/rw pointer is within the limits.
     * */
    bool is_within_limits(bool is_rd, uint32_t pos) const {
        return is_rd ? (rd_min <= pos and pos <= rd_end) : (wr_min <= pos and pos <= wr_end);
    }

    void chk_within_limits([[maybe_unused]] bool is_rd) const { assert(is_within_limits(is_rd, is_rd ? rd : wr)); }

    /*
     * Check that the request size for writing makes sense:
     *
     *  - the caller should not request to write more bytes than the ones
     *    are in his/her data buffer (vector or input file).
     *
     *  - the data buffer (vector or input file) should not have more bytes than
     *    it can be handled by us when the caller request to write everything.
     *
     * Throw an exception on a check failure, return the request size that
     * should be used for writing.
     *
     * Note: these methods don't check if the io object has enough space for the write
     * so even if the checks succeed the exact write may still fail.
     * See rw_operation_exact_sz() for more details on this.
     * */
    uint32_t chk_write_request_sizes(const std::vector<char>& data, const uint32_t sz) const;
    uint32_t chk_write_request_sizes(std::istream& input, const uint32_t sz) const;
    uint32_t chk_write_request_sizes(const uint64_t avail_sz, const uint32_t sz, const char* input_name) const;

    void copy_into_self_from_start(const uint32_t exact_sz);
    void copy_into_self_from_end(const uint32_t exact_sz);

protected:
    virtual ~IOBase() {}
};
}  // namespace xoz
