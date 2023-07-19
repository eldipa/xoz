#pragma once
#include <map>
#include <cstdint>
#include <list>
#include "xoz/ext/extent.h"

class SubBlockFreeMap {
    private:
        std::list<Extent> exts_bin[Extent::SUBBLK_CNT_PER_BLK];

        typedef std::map<uint32_t, Extent> nr2ext_map;
        nr2ext_map fr_by_nr;

    public:
        SubBlockFreeMap();

        // Result of an allocation.
        struct alloc_result_t {
            Extent ext;
            bool success;
        };


        void assign_as_freed(const std::list<Extent>& exts);
        void clear();

        struct alloc_result_t alloc(uint8_t subblk_cnt);

        void dealloc(const Extent& ext);


        // Extent iterator: a adapter iterator class over std::map/std::multimap
        // const_iterator that yields Extent objects.
        template<typename M>
        class _ConstExtentIterator {
            private:
                typename M::const_iterator it;

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

                using difference_type = typename M::const_iterator::difference_type;

                using iterator_category = std::input_iterator_tag;

                _ConstExtentIterator(typename M::const_iterator const& it) : it(it), cached(0,0,false), is_cache_synced(false) {}

                _ConstExtentIterator& operator++() {
                    ++it;
                    is_cache_synced = false;
                    return *this;
                }

                _ConstExtentIterator operator++(int) {
                    _ConstExtentIterator copy(*this);
                    it++;
                    is_cache_synced = false;
                    return copy;
                }

                inline bool operator==(const _ConstExtentIterator& other) const {
                    return it == other.it;
                }

                inline bool operator!=(const _ConstExtentIterator& other) const {
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
                        cached = Extent(blk_nr_of(it), blk_bitmap_of(it), true);
                        is_cache_synced = true;
                    }
                }
        };


        // Handy typedef
        typedef _ConstExtentIterator<nr2ext_map> const_iterator_by_blk_nr;

        // Iterators over the free chunks returned as Extent objects.
        // By block number only order
        //
        // All the iterators are constant as the caller must not
        // modify the internals of the free map.
        inline const_iterator_by_blk_nr cbegin_by_blk_nr() const {
            return _ConstExtentIterator<nr2ext_map>(fr_by_nr.cbegin());
        }

        inline const_iterator_by_blk_nr cend_by_blk_nr() const {
            return _ConstExtentIterator<nr2ext_map>(fr_by_nr.cend());
        }

    private:

        size_t count_entries_in_bins() const;

        // Accessors to fr_by_nr map iterators' fields with blk_nr as the key
        // and Extent as the value of the map
        static inline const uint32_t& blk_nr_of(const SubBlockFreeMap::nr2ext_map::const_iterator& it) {
            return it->first;
        }

        static inline uint16_t blk_bitmap_of(SubBlockFreeMap::nr2ext_map::iterator& it) {
            return it->second.blk_bitmap();
        }

        static inline uint16_t blk_bitmap_of(const SubBlockFreeMap::nr2ext_map::const_iterator& it) {
            return it->second.blk_bitmap();
        }

        void fail_if_not_subblk_or_zero_cnt(const Extent& ext) const;
        void fail_if_blk_nr_already_seen(const Extent& ext) const;
};

