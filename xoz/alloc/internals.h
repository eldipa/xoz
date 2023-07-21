#pragma once

#include <map>
#include <cstdint>
#include <list>
#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"

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

    // Extent iterator: a adapter iterator class over std::map/std::multimap
    // const_iterator that yields Extent objects either normal Extent
    // and Extent for suballocation
    template<typename Map, bool IsExtentSubAllocated>
    class ConstExtentIterator {
        private:
            typename Map::const_iterator it;

            // To avoid creating an Extent object every time that
            // the operators * and -> are called, we cache the
            // Extent created from the current iterator value once
            // and we yield then const references and const pointer to.
            //
            // On each iterator movement (aka ++it) the cache becomes
            // invalid and is_cache_synced will be false until another
            // call to operator * and -> is made.
            mutable Extent cached;
            mutable bool is_cache_synced;

        public:
            // Public traits interface saying
            //
            // - which values the iterator
            // points to (Extent, const Extent& and const Extent*);
            //
            // - which type can represent the difference between iterators
            // (the same that the original container's iterators use);
            //
            // - and in which category this iterator falls (Input Iterator).
            using value_type = Extent;

            using reference = Extent const&;
            using pointer   = Extent const*;

            using difference_type = typename Map::const_iterator::difference_type;

            using iterator_category = std::input_iterator_tag;

            ConstExtentIterator(typename Map::const_iterator const& it) : it(it), cached(0,0,false), is_cache_synced(false) {}

            ConstExtentIterator& operator++() {
                ++it;
                is_cache_synced = false;
                return *this;
            }

            ConstExtentIterator operator++(int) {
                ConstExtentIterator copy(*this);
                it++;
                is_cache_synced = false;
                return copy;
            }

            inline bool operator==(const ConstExtentIterator& other) const {
                return it == other.it;
            }

            inline bool operator!=(const ConstExtentIterator& other) const {
                return it != other.it;
            }

            inline const Extent& operator*() const {
                update_current_extent();
                return cached;
            }

            inline const Extent* operator->() const {
                update_current_extent();
                return &cached;
            }

        private:
            inline void update_current_extent() const {
                if (not is_cache_synced) {
                    if constexpr (IsExtentSubAllocated) {
                        cached = Extent(xoz::alloc::internals::blk_nr_of(it), xoz::alloc::internals::blk_bitmap_of(it), true);
                    } else {
                        cached = Extent(xoz::alloc::internals::blk_nr_of(it), xoz::alloc::internals::blk_cnt_of(it), false);
                    }
                    is_cache_synced = true;
                }
            }
    };

    // Raise an exception if the block count or subblock count is zero
    // (depending if is_suballoc is false or true)
    void fail_alloc_if_empty(const uint16_t cnt, const bool is_suballoc);

    // Raise an exception if the extent has zero blocks or if it
    // is for suballocation.
    //
    // This ensures a non-empty "full" extent.
    void fail_if_suballoc_or_zero_cnt(const Extent& ext);
}
