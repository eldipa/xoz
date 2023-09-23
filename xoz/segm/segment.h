#pragma once

#include <cstdint>
#include <iostream>
#include <numeric>
#include <vector>

#include "xoz/ext/extent.h"
#include "xoz/mem/iobase.h"

class Segment {
private:
    std::vector<Extent> arr;

    bool inline_present;
    std::vector<char> raw;

public:
    static const uint32_t MaxInlineSize = (1 << 6) - 1;

    Segment(): inline_present(false) {}

    static Segment create_empty_zero_inline() {
        Segment segm;
        segm.inline_present = true;
        return segm;
    }

    // TODO offer a "borrow" variant
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

    std::vector<Extent> const& exts() const { return arr; }

    bool has_end_of_segment() { return inline_present; }

    void add_end_of_segment() { inline_present = true; }

    void add_extent(const Extent& ext) { arr.push_back(ext); }

    void clear_extents() { arr.clear(); }

    uint32_t ext_cnt() const {
        // TODO check cast
        return uint32_t(arr.size());
    }

    uint32_t length() const {
        // TODO overflow
        return ext_cnt() + (inline_present ? 1 : 0);
    }

    uint32_t full_blk_cnt() const {
        return std::accumulate(arr.cbegin(), arr.cend(), 0, [](uint32_t cnt, const Extent& ext) {
            return cnt + (ext.is_suballoc() ? 0 : ext.blk_cnt());
        });
    }

    uint32_t subblk_cnt() const {
        return std::accumulate(arr.cbegin(), arr.cend(), 0, [](uint32_t cnt, const Extent& ext) {
            return cnt + (ext.is_suballoc() ? ext.subblk_cnt() : 0);
        });
    }

    std::vector<char>& inline_data() {
        assert(inline_present);
        return raw;
    }

    uint8_t inline_data_sz() const {
        // TODO check cast
        return inline_present ? uint8_t(raw.size()) : 0;
    }

    static Segment load_struct_from(IOBase& io, uint32_t segm_len = uint32_t(-1)) {
        Segment segm;
        segm.read_struct_from(io, segm_len);
        return segm;
    }


    void read_struct_from(IOBase& io, uint32_t segm_len = uint32_t(-1));
    void write_struct_into(IOBase& io) const;


    static Segment load_struct_from(IOBase&& io, uint32_t segm_len = uint32_t(-1)) {
        return load_struct_from(io, segm_len);
    }

    void read_struct_from(IOBase&& io, uint32_t segm_len = uint32_t(-1)) { read_struct_from(io, segm_len); }
    void write_struct_into(IOBase&& io) const { write_struct_into(io); }


    // Return the size in bytes to represent the Segment structure in disk.
    // This includes the size of the inline data but it does not include the
    // space referenced by the extents' blocks
    uint32_t calc_struct_footprint_size() const;

    // Return the size in bytes of all the space usable (aka data).
    // This is the sum of the space referenced by the extents' blocks and
    // the inline data.
    uint32_t calc_data_space_size(uint8_t blk_sz_order) const;

    uint32_t estimate_on_avg_internal_frag_sz(uint8_t blk_sz_order) const;

    friend void PrintTo(const Segment& segm, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const Segment& segm);

private:
    void fail_if_bad_inline_sz() const;
};
