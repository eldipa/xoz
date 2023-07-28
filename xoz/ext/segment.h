#pragma once

#include <cstdint>
#include <iostream>
#include <vector>

#include "xoz/ext/extent.h"

class Segment {
private:
    std::vector<Extent> arr;

    bool inline_present;
    std::vector<uint8_t> raw;

public:
    Segment(): inline_present(false) {}

    static Segment create_empty_zero_inline() {
        Segment segm;
        segm.inline_present = true;
        return segm;
    }

    // TODO offer a "borrow" variant
    void set_inline_data(const std::vector<uint8_t>& data) {
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

    std::vector<Extent> const& exts() const {
        return arr;
    }

    bool has_end_of_segment() { return inline_present; }

    void add_end_of_segment() { inline_present = true; }

    void add_extent(const Extent& ext) { arr.push_back(ext); }

    void clear_extents() { arr.clear(); }

    uint32_t ext_cnt() const {
        // TODO check cast
        return uint32_t(arr.size());
    }

    std::vector<uint8_t>& inline_data() { return raw; }

    static Segment read_segment(std::istream& fp, const uint64_t segm_sz) {
        Segment segm;
        segm.read(fp, segm_sz);
        return segm;
    }

    void read(std::istream& fp, const uint64_t segm_sz);
    void write(std::ostream& fp) const;

    uint32_t calc_footprint_disk_size() const;
    uint32_t calc_usable_space_size(uint8_t blk_sz_order) const;

private:
    void fail_if_bad_inline_sz() const;
};
