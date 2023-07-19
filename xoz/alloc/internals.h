#pragma once

#include <map>
#include <cstdint>
#include <list>
#include "xoz/ext/extent.h"

namespace xoz::alloc::internals {

    typedef std::pair<uint32_t, uint16_t> pair_nr2cnt_t;
    typedef std::map<uint32_t, uint16_t> map_nr2cnt_t;

    typedef std::pair<uint16_t, uint32_t> pair_cnt2nr_t;
    typedef std::multimap<uint16_t, uint32_t> multimap_cnt2nr_t;

    typedef std::pair<uint32_t, Extent> pair_nr2ext_t;
    typedef std::map<uint32_t, Extent> map_nr2ext_t;

    // Accessors to fr_by_nr map iterators' fields with blk_nr as the key
    // and blk_cnt as the value of the map
    inline const uint32_t& blk_nr_of(const map_nr2cnt_t::const_iterator& it) {
        return it->first;
    }

    inline uint16_t& blk_cnt_of(map_nr2cnt_t::iterator& it) {
        return it->second;
    }

    inline const uint16_t& blk_cnt_of(const map_nr2cnt_t::const_iterator& it) {
        return it->second;
    }


    // Accessors to fr_by_cnt multimap iterators' fields with blk_cnt as the key
    // and blk_nr as the value of the map
    inline const uint16_t& blk_cnt_of(const multimap_cnt2nr_t::const_iterator& it) {
        return it->first;
    }

    inline uint32_t& blk_nr_of(multimap_cnt2nr_t::iterator& it) {
        return it->second;
    }

    inline const uint32_t& blk_nr_of(const multimap_cnt2nr_t::const_iterator& it) {
        return it->second;
    }

    // Accessors to fr_by_nr map iterators' fields with blk_nr as the key
    // and Extent as the value of the map
    inline const uint32_t& blk_nr_of(const map_nr2ext_t::const_iterator& it) {
        return it->first;
    }

    inline uint16_t blk_bitmap_of(map_nr2ext_t::iterator& it) {
        return it->second.blk_bitmap();
    }

    inline uint16_t blk_bitmap_of(const map_nr2ext_t::const_iterator& it) {
        return it->second.blk_bitmap();
    }
}
