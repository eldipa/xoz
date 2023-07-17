#pragma once
#include <map>
#include <cstdint>
#include <list>
#include <utility>
#include "xoz/ext/extent.h"

class FreeList {
    private:
        bool coalescing_enabled;
        uint16_t split_above_threshold;

        typedef std::pair<uint32_t, uint16_t> nr_blk_cnt_pair;
        typedef std::pair<uint16_t, uint32_t> blk_cnt_nr_pair;

        typedef std::map<uint32_t, uint16_t> nr_blk_cnt_map;
        typedef std::multimap<uint16_t, uint32_t> blk_cnt_nr_multimap;

        nr_blk_cnt_map fr_by_nr;
        blk_cnt_nr_multimap fr_by_cnt;

    public:
        FreeList(bool coalescing_enabled = true, uint16_t split_above_threshold = 0);

        // Result of an allocation.
        struct alloc_result_t {
            Extent ext;
            bool success;
        };


        void initialize_from_extents(const std::list<Extent>& exts);
        void clear();

        // Finds the best free chunk that can hold at least
        // <blk_cnt> blocks
        //
        // If success is True, the allocation took place and
        // ext is the extent allocated.
        //
        // If success is False, the allocated didn't take place
        // and ext is the extent that "could" be allocated
        // if ext.blk_cnt blocks would be requested.
        //
        // In this case ext.blk_nr is undefined.
        //
        // If success is False and ext.blk_cnt is 0 that may signal
        // that there is no free chunks *or* any allocation of a smaller
        // size would require fragment the free chunks.
        //
        // See the test case AllocCoalescedDoesntSplitButCloseSuboptimalHint
        // for more about this.
        struct alloc_result_t alloc(uint16_t blk_cnt);

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
                        cached = Extent(blk_nr_of(it), blk_cnt_of(it), false);
                        is_cache_synced = true;
                    }
                }
        };


        // Handy typedef for the 2 kinds of iterators: by block number
        // and by block count
        typedef _ConstExtentIterator<nr_blk_cnt_map> const_iterator_by_blk_nr;
        typedef _ConstExtentIterator<blk_cnt_nr_multimap> const_iterator_by_blk_cnt;

        // Iterators over the free chunks returned as Extent objects.
        //
        // The iteration follow one of the 2 possible order:
        //  - by block number
        //  - by block count
        //
        // All the iterators are constant as the caller must not
        // modify the internals of the free list.
        inline const_iterator_by_blk_nr cbegin_by_blk_nr() const {
            return _ConstExtentIterator<nr_blk_cnt_map>(fr_by_nr.cbegin());
        }

        inline const_iterator_by_blk_nr cend_by_blk_nr() const {
            return _ConstExtentIterator<nr_blk_cnt_map>(fr_by_nr.cend());
        }

        inline const_iterator_by_blk_cnt cbegin_by_blk_cnt() const {
            return _ConstExtentIterator<blk_cnt_nr_multimap>(fr_by_cnt.cbegin());
        }

        inline const_iterator_by_blk_cnt cend_by_blk_cnt() const {
            return _ConstExtentIterator<blk_cnt_nr_multimap>(fr_by_cnt.cend());
        }

    private:

        // Erase from the multimap fr_by_cnt the chunk pointed by target_it
        // (coming from the fr_by_nr map)
        //
        // This erase operation does a O(log(n)) lookup on fr_by_cnt but because
        // there may be multiple chunks with the same block count, there is
        // a O(n) linear search to delete the one pointed by target_it
        blk_cnt_nr_multimap::iterator erase_from_fr_by_cnt(nr_blk_cnt_map::iterator& target_it);

        // Accessors to fr_by_nr map iterators' fields with blk_nr as the key
        // and blk_cnt as the value of the map
        static inline const uint32_t& blk_nr_of(const FreeList::nr_blk_cnt_map::const_iterator& it) {
            return it->first;
        }

        static inline uint16_t& blk_cnt_of(FreeList::nr_blk_cnt_map::iterator& it) {
            return it->second;
        }

        static inline const uint16_t& blk_cnt_of(const FreeList::nr_blk_cnt_map::const_iterator& it) {
            return it->second;
        }


        // Accessors to fr_by_cnt multimap iterators' fields with blk_cnt as the key
        // and blk_nr as the value of the map
        static inline const uint16_t& blk_cnt_of(const FreeList::blk_cnt_nr_multimap::const_iterator& it) {
            return it->first;
        }

        static inline uint32_t& blk_nr_of(FreeList::blk_cnt_nr_multimap::iterator& it) {
            return it->second;
        }

        static inline const uint32_t& blk_nr_of(const FreeList::blk_cnt_nr_multimap::const_iterator& it) {
            return it->second;
        }

};
