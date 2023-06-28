#pragma once

#include <cstdint>
#include <vector>
#include <iostream>

#include "xoz/ext/extent.h"

class Segment {
    private:
    std::vector<Extent> arr;

    bool inline_present;
    std::vector<uint8_t> raw;

    public:
    Segment() : inline_present(false) {}

    static Segment createEmpty() {
        Segment segm;
        segm.inline_present = true;
        return segm;
    }

    // TODO offer a "borrow" variant
    void set_inline_data(const std::vector<uint8_t>& data) {
        inline_present = true;
        raw = data;
    }

    void add_extent(const Extent& ext) {
        arr.push_back(ext);
    }

    void clear_extents() {
        arr.clear();
    }

    std::vector<uint8_t>& inline_data() {
        return raw;
    }

    static Segment load_segment(std::istream& fp, uint64_t segm_sz, uint64_t endpos) {
        Segment segm;
        segm.load(fp, segm_sz, endpos);
        return segm;
    }

    void load(std::istream& fp, uint64_t segm_sz, uint64_t endpos);
    void write(std::ostream& fp, uint64_t endpos) const;

    uint32_t calc_footprint_disk_size() const;
    uint32_t calc_usable_space_size(uint8_t blk_sz_order) const;

    private:
    void fail_if_invalid_empty() const;
    void fail_if_bad_inline_sz() const;
};

