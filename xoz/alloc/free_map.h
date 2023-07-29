#pragma once
#include <cstdint>
#include <list>
#include <map>

#include "xoz/alloc/internals.h"
#include "xoz/ext/extent.h"

class FreeMap {
    using map_nr2cnt_t = xoz::alloc::internals::map_nr2cnt_t;
    using multimap_cnt2nr_t = xoz::alloc::internals::multimap_cnt2nr_t;

private:
    bool coalescing_enabled;
    uint16_t split_above_threshold;

    map_nr2cnt_t fr_by_nr;
    multimap_cnt2nr_t fr_by_cnt;

public:
    explicit FreeMap(bool coalescing_enabled = true, uint16_t split_above_threshold = 0);

    // Result of an allocation.
    struct alloc_result_t {
        Extent ext;
        bool success;
    };

    void provide(const std::list<Extent>& exts);
    void provide(const Extent& exts);
    std::list<Extent> release_all();

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
    std::list<Extent> release(bool mandatory);

    // Handy typedefs iterators: by block number
    // and by block count
    typedef xoz::alloc::internals::ConstExtentIterator<map_nr2cnt_t::const_iterator, false> const_iterator_by_blk_nr_t;
    typedef xoz::alloc::internals::ConstExtentIterator<map_nr2cnt_t::const_reverse_iterator, false>
            const_reverse_iterator_by_blk_nr_t;
    typedef xoz::alloc::internals::ConstExtentIterator<multimap_cnt2nr_t::const_iterator, false>
            const_iterator_by_blk_cnt_t;

    // Iterators over the free chunks returned as Extent objects.
    //
    // The iteration follow one of the 2 possible order:
    //  - by block number
    //  - by block count
    //
    // All the iterators are constant as the caller must not
    // modify the internals of the free map.
    inline const_iterator_by_blk_nr_t cbegin_by_blk_nr() const { return const_iterator_by_blk_nr_t(fr_by_nr.cbegin()); }

    inline const_iterator_by_blk_nr_t cend_by_blk_nr() const { return const_iterator_by_blk_nr_t(fr_by_nr.cend()); }

    inline const_reverse_iterator_by_blk_nr_t crbegin_by_blk_nr() const {
        return const_reverse_iterator_by_blk_nr_t(fr_by_nr.crbegin());
    }

    inline const_reverse_iterator_by_blk_nr_t crend_by_blk_nr() const {
        return const_reverse_iterator_by_blk_nr_t(fr_by_nr.crend());
    }

    inline const_iterator_by_blk_cnt_t cbegin_by_blk_cnt() const {
        return const_iterator_by_blk_cnt_t(fr_by_cnt.cbegin());
    }

    inline const_iterator_by_blk_cnt_t cend_by_blk_cnt() const { return const_iterator_by_blk_cnt_t(fr_by_cnt.cend()); }

private:
    // Erase from the multimap fr_by_cnt the chunk pointed by target_it
    // (coming from the fr_by_nr map)
    //
    // This erase operation does a O(log(n)) lookup on fr_by_cnt but because
    // there may be multiple chunks with the same block count, there is
    // a O(n) linear search to delete the one pointed by target_it
    multimap_cnt2nr_t::iterator erase_from_fr_by_cnt(map_nr2cnt_t::iterator& target_it);

    void fail_if_overlap(const Extent& ext) const;
};
