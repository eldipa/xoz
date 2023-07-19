#pragma once

#include <map>
#include <cstdint>
#include <list>
#include "xoz/ext/extent.h"

namespace xoz::alloc::internals {

    typedef std::pair<uint32_t, uint16_t> nr_blk_cnt_pair;
    typedef std::map<uint32_t, uint16_t> nr_blk_cnt_map;

    typedef std::pair<uint16_t, uint32_t> blk_cnt_nr_pair;
    typedef std::multimap<uint16_t, uint32_t> blk_cnt_nr_multimap;

    typedef std::map<uint32_t, Extent> nr2ext_map;

    // Accessors to fr_by_nr map iterators' fields with blk_nr as the key
    // and blk_cnt as the value of the map
    inline const uint32_t& blk_nr_of(const nr_blk_cnt_map::const_iterator& it) {
        return it->first;
    }

    inline uint16_t& blk_cnt_of(nr_blk_cnt_map::iterator& it) {
        return it->second;
    }

    inline const uint16_t& blk_cnt_of(const nr_blk_cnt_map::const_iterator& it) {
        return it->second;
    }


    // Accessors to fr_by_cnt multimap iterators' fields with blk_cnt as the key
    // and blk_nr as the value of the map
    inline const uint16_t& blk_cnt_of(const blk_cnt_nr_multimap::const_iterator& it) {
        return it->first;
    }

    inline uint32_t& blk_nr_of(blk_cnt_nr_multimap::iterator& it) {
        return it->second;
    }

    inline const uint32_t& blk_nr_of(const blk_cnt_nr_multimap::const_iterator& it) {
        return it->second;
    }

    // Accessors to fr_by_nr map iterators' fields with blk_nr as the key
    // and Extent as the value of the map
    inline const uint32_t& blk_nr_of(const nr2ext_map::const_iterator& it) {
        return it->first;
    }

    inline uint16_t blk_bitmap_of(nr2ext_map::iterator& it) {
        return it->second.blk_bitmap();
    }

    inline uint16_t blk_bitmap_of(const nr2ext_map::const_iterator& it) {
        return it->second.blk_bitmap();
    }
}
