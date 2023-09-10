#pragma once

#include <cstdint>
#include <vector>

#include "xoz/repo/repo.h"
#include "xoz/segm/segment.h"


class IOSegment {
private:
    Repository& repo;
    Segment sg;

    const uint32_t sg_sz;
    const uint32_t sg_no_inline_sz;

    const std::vector<uint32_t> begin_positions;

    uint32_t rd;
    uint32_t wr;

public:
    IOSegment(Repository& repo, const Segment& sg);

    /*
     * Read data from the segment or write data into.
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
     * or the size of the data available in the segment, whatever is the
     * smaller)
     *
     * If a std::vector is used and the size is not given, all the remaining
     * unread data in the segment is read or all the available data in the vector
     * is written into the segment, depending if it is a reading or a writing
     * operation.
     *
     * When a raw char* is given, the size must be set explicitly and it is
     * the caller's responsibility to ensure that raw pointer points to
     * an allocated memory large enough. The API
     * */
    void readall(char* data, const uint32_t exact_sz) { rw_operation_exact_sz(true, data, exact_sz); }

    void readall(std::vector<char>& data, const uint32_t exact_sz = uint32_t(-1)) {
        const uint32_t reserve_sz = exact_sz == uint32_t(-1) ? remain_rd() : exact_sz;
        data.resize(reserve_sz);
        rw_operation_exact_sz(true, data.data(), reserve_sz);
    }

    uint32_t readsome(char* data, const uint32_t max_sz) { return rw_operation(true, data, max_sz); }

    uint32_t readsome(std::vector<char>& data, const uint32_t sz = uint32_t(-1)) {
        const uint32_t reserve_sz = sz == uint32_t(-1) ? remain_rd() : sz;
        data.resize(reserve_sz);
        return rw_operation(true, data.data(), reserve_sz);
    }

    void writeall(const char* data, const uint32_t exact_sz) { rw_operation_exact_sz(false, (char*)data, exact_sz); }

    void writeall(const std::vector<char>& data, const uint32_t exact_sz = uint32_t(-1)) {
        const uint32_t request_sz = chk_write_request_sizes(data, exact_sz);
        rw_operation_exact_sz(false, (char*)data.data(), request_sz);
    }

    uint32_t writesome(const char* data, const uint32_t max_sz) { return rw_operation(false, (char*)data, max_sz); }

    uint32_t writesome(const std::vector<char>& data, const uint32_t sz = uint32_t(-1)) {
        const uint32_t request_sz = chk_write_request_sizes(data, sz);
        return rw_operation(false, (char*)data.data(), request_sz);
    }

    uint32_t tell_rd() const { return rd; }

    uint32_t tell_wr() const { return wr; }

    enum Seekdir { beg = 0, end = 1, fwd = 2, bwd = 3 };

    void seek_rd(uint32_t pos, Seekdir way = Seekdir::beg) {
        rd = calc_seek(pos, rd, way);
        assert(rd <= sg_sz);
    }

    void seek_wr(uint32_t pos, Seekdir way = Seekdir::beg) {
        wr = calc_seek(pos, wr, way);
        assert(wr <= sg_sz);
    }

    uint32_t remain_rd() const { return sg_sz - rd; }

    uint32_t remain_wr() const { return sg_sz - wr; }


private:
    struct ext_ptr_t {
        Extent ext;
        uint32_t offset;
        uint32_t remain;
    };

    const struct ext_ptr_t abs_pos_to_ext(const uint32_t pos) const;

    /*
     * The given buffer must have enough space to hold exact_sz bytes.
     *
     * If the segment has N remaining unread bytes / places to write such
     * N is less than exact_sz, then the method will throw (basically it cannot
     * by completed an operation of exact_sz bytes with less bytes).
     *
     * If after the operation execution the read/written bytes is different
     * than exact_sz, also it throws.
     * */
    void rw_operation_exact_sz(const bool is_read_op, char* data, const uint32_t exact_sz);

    /*
     * The given buffer must have enough space to hold max_data_sz bytes The operation
     * will read/write up to max_data_sz bytes but it may less.
     *
     * The count of bytes read/written is returned.
     *
     * */
    uint32_t rw_operation(const bool is_read_op, char* data, const uint32_t max_data_sz);

    /*
     * Return the new read/write pointer with initial value <cur> as it is were updating
     * to the new position <pos> where this position can be interpreted as:
     *  - absolute position (way == Seekdir::beg)
     *  - absolute backward position (way == Seekdir::end)
     *  - relative to cur position in forward direction (way == Seekdir::fwd)
     *  - relative to cur position in backward direction (way == Seekdir::bwd)
     *
     * In any case if the calculated position goes beyond to the available space
     * (sg_sz), the returned position is clamp to sg_sz (this means that on overflow
     * the position returned is one-past the end of the available space).
     *
     * For the case of underflows (positions past the begin), the position returned
     * is 0.
     *
     * No error happen in both cases.
     **/
    uint32_t calc_seek(uint32_t pos, uint32_t cur, Seekdir way = Seekdir::beg) const {
        switch (way) {
            case Seekdir::beg:
                if (pos > sg_sz) {
                    return sg_sz;
                }

                return pos;
            case Seekdir::end:
                if (pos > sg_sz) {
                    return 0;
                } else {
                    return sg_sz - pos;
                }
            case Seekdir::fwd:
                cur = cur + pos;
                if (cur < pos or cur > sg_sz) {
                    return sg_sz;  // overflow
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

    /*
     * Check that the request size for writing makes sense:
     *
     *  - the caller should not request to write more bytes than the ones
     *    are in his/her data buffer.
     *
     *  - the data buffer should not have more bytes than it can be handled
     *    by IOSegment when the caller request to write everything.
     *
     * Throw an exception on a check failure, return the request size that
     * should be used for writing.
     * */
    uint32_t chk_write_request_sizes(const std::vector<char>& data, const uint32_t sz) {
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
};
