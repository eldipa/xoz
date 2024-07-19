#pragma once

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include "xoz/ext/extent.h"
#include "xoz/io/iobase.h"

namespace xoz {
class Segment {
public:
    static const uint32_t MaxInlineSize = (1 << 6) - 1;
    static const uint32_t EndOfSegmentSize = 2;  // 2 bytes

    explicit Segment(uint8_t blk_sz_order): blk_sz_order(blk_sz_order), inline_present(false) {}

    static Segment EmptySegment(uint8_t blk_sz_order) { return Segment(blk_sz_order); }

    static Segment create_empty_zero_inline(uint8_t blk_sz_order) {
        Segment segm(blk_sz_order);
        segm.inline_present = true;
        return segm;
    }

    void set_inline_data(const std::vector<char>& data) {
        inline_present = true;
        raw = data;
    }

    void reserve_inline_data(uint8_t len) {
        inline_present = true;
        raw.resize(len);
    }

    void remove_inline_data() {
        inline_present = false;
        raw.clear();
    }

    std::vector<char>& inline_data() {
        if (not has_end_of_segment()) {
            throw std::runtime_error("Segment has not inline data.");
        }
        return raw;
    }

    /*
     * Return the size in bytes of the inline data (if any).
     * If the inline data is not present (not even empty), return 0.
     *
     * To truly check if the segment has or no inline data (even empty),
     * call has_end_of_segment()
     * */
    uint8_t inline_data_sz() const { return inline_present ? assert_u8(raw.size()) : 0; }

    bool has_end_of_segment() const { return inline_present; }

    Segment& add_end_of_segment() {
        inline_present = true;
        return *this;
    }

    std::vector<Extent> const& exts() const { return arr; }

    void add_extent(const Extent& ext) {
        if (has_end_of_segment()) {
            throw std::runtime_error("Segment with inline data/end of segment cannot be extended.");
        }

        arr.push_back(ext);
    }

    void extend(const Segment& segm) {
        if (has_end_of_segment()) {
            throw std::runtime_error("Segment with inline data/end of segment cannot be extended.");
        }

        // append the other segm's arr to ours
        arr.reserve(arr.capacity() + segm.arr.size());
        std::copy(segm.arr.begin(), segm.arr.end(), std::back_inserter(arr));

        // plain copy - much easier (this is possible because this->has_end_of_segment() is false)
        raw = segm.raw;
        inline_present = segm.inline_present or not raw.empty();
    }

    void remove_last_extent() {
        if (has_end_of_segment()) {
            throw std::runtime_error("Segment with inline data/end of segment cannot be reduced.");
        }

        arr.pop_back();
    }

    void clear_extents() {
        if (has_end_of_segment()) {
            throw std::runtime_error("Segment with inline data/end of segment cannot be reduced.");
        }

        arr.clear();
    }

    void clear() {
        remove_inline_data();
        clear_extents();
    }

    uint32_t ext_cnt() const { return assert_u32(arr.size()); }

    uint32_t length() const {
        // TODO overflow
        return ext_cnt() + (inline_present ? 1 : 0);
    }

    uint32_t full_blk_cnt() const {
        return std::accumulate(arr.cbegin(), arr.cend(), uint32_t(0), [](uint32_t cnt, const Extent& ext) {
            return cnt + (ext.is_suballoc() ? uint32_t(0) : ext.blk_cnt());
        });
    }

    uint32_t subblk_cnt() const {
        return std::accumulate(arr.cbegin(), arr.cend(), uint32_t(0), [](uint32_t cnt, const Extent& ext) {
            return cnt + (ext.is_suballoc() ? ext.subblk_cnt() : uint32_t(0));
        });
    }

    bool is_empty_space() const {
        if (inline_present and raw.size() > 0) {
            return false;
        }

        for (const auto& ext: arr) {
            if (ext.is_suballoc() and ext.subblk_cnt() > 0) {
                return false;
            }

            if (not ext.is_suballoc() and ext.blk_cnt() > 0) {
                return false;
            }
        }

        return true;
    }

    /*
     * For load/read segments, because the segment itself does not have its length, how much
     * should we read?
     *
     *  - InlineEnd: the segment ends with an inline-extent (empty or not); if not present, throw.
     *  - IOEnd: the segment ends when the io ends; if inline present earlier, throw.
     *  - AnyEnd: either InlineEnd or IOEnd, the one that comes first
     *  - ExplicitLen: pass an explicit count of extents to read; if less extents are read (either
     *                 due an earlier inline-extent or the end of the io), throw.
     * */
    enum EndMode {
        InlineEnd = 1,
        IOEnd = 2,
        AnyEnd = 3,
        ExplicitLen = 4,
    };

    static Segment load_struct_from(IOBase& io, uint8_t blk_sz_order, enum EndMode mode = EndMode::AnyEnd,
                                    uint32_t segm_len = uint32_t(-1), uint32_t* checksum_p = nullptr) {
        Segment segm(blk_sz_order);
        segm.read_struct_from(io, mode, segm_len, checksum_p);
        return segm;
    }


    void read_struct_from(IOBase& io, enum EndMode mode = EndMode::AnyEnd, uint32_t segm_len = uint32_t(-1),
                          uint32_t* checksum_p = nullptr);
    void write_struct_into(IOBase& io, uint32_t* checksum_p = nullptr) const;


    static Segment load_struct_from(IOBase&& io, uint8_t blk_sz_order, enum EndMode mode = EndMode::AnyEnd,
                                    uint32_t segm_len = uint32_t(-1), uint32_t* checksum_p = nullptr) {
        return load_struct_from(io, blk_sz_order, mode, segm_len, checksum_p);
    }

    void read_struct_from(IOBase&& io, enum EndMode mode = EndMode::AnyEnd, uint32_t segm_len = uint32_t(-1),
                          uint32_t* checksum_p = nullptr) {
        read_struct_from(io, mode, segm_len, checksum_p);
    }
    void write_struct_into(IOBase&& io, uint32_t* checksum_p = nullptr) const { write_struct_into(io, checksum_p); }


    // Return the size in bytes to represent the Segment structure in disk.
    // This includes the size of the inline data but it does not include the
    // space referenced by the extents' blocks
    uint32_t calc_struct_footprint_size() const;

    // Return the size in bytes of all the space usable (aka data).
    // This is the sum of the space referenced by the extents' blocks and,
    // if include_inline is True, the inline data.
    uint32_t calc_data_space_size(bool include_inline = true) const;

    uint32_t estimate_on_avg_internal_frag_sz() const;

    friend void PrintTo(const Segment& segm, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const Segment& segm);

    bool operator==(const Segment& segm) const;
    bool operator!=(const Segment& segm) const { return not((*this) == segm); }

private:
    void fail_if_bad_inline_sz() const;

private:
    std::vector<Extent> arr;
    uint8_t blk_sz_order;

    bool inline_present;
    std::vector<char> raw;
};
}  // namespace xoz
