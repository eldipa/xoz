#pragma once
#include <cstdint>
#include <list>
#include <map>

#include "xoz/alloc/internals.h"
#include "xoz/ext/extent.h"

class SubBlockFreeMap {
    using map_nr2ext_t = xoz::alloc::internals::map_nr2ext_t;

private:
    std::list<Extent> exts_bin[Extent::SUBBLK_CNT_PER_BLK];

    map_nr2ext_t fr_by_nr;

    // Stats
    int32_t owned_subblk_cnt;
    int32_t allocated_subblk_cnt;

public:
    SubBlockFreeMap();

    // Result of an allocation.
    struct alloc_result_t {
        Extent ext;
        bool success;
    };

    void provide(const std::list<Extent>& exts);
    void provide(const Extent& ext);
    void release(const std::list<Extent>& exts);

    struct alloc_result_t alloc(uint8_t subblk_cnt);

    void dealloc(const Extent& ext);

    void reset();

    struct fr_stats_t {
        int32_t owned_subblk_cnt;
        int32_t allocated_subblk_cnt;
    };

    inline const struct fr_stats_t stats() const {
        return {.owned_subblk_cnt = owned_subblk_cnt, .allocated_subblk_cnt = allocated_subblk_cnt};
    }

    // Handy typedef
    typedef xoz::alloc::internals::ConstExtentIterator<map_nr2ext_t::const_iterator, true> const_iterator_by_blk_nr_t;
    typedef std::list<Extent>::const_iterator const_iterator_full_blk_t;

    // Iterators over the free chunks returned as Extent objects.
    // By block number only order
    //
    // All the iterators are constant as the caller must not
    // modify the internals of the free map.
    inline const_iterator_by_blk_nr_t cbegin_by_blk_nr() const { return const_iterator_by_blk_nr_t(fr_by_nr.cbegin()); }

    inline const_iterator_by_blk_nr_t cend_by_blk_nr() const { return const_iterator_by_blk_nr_t(fr_by_nr.cend()); }

    // Iterators over the full free blocks as Extent objects.
    inline const_iterator_full_blk_t cbegin_full_blk() const {
        return exts_bin[Extent::SUBBLK_CNT_PER_BLK - 1].cbegin();
    }

    inline const_iterator_full_blk_t cend_full_blk() const { return exts_bin[Extent::SUBBLK_CNT_PER_BLK - 1].cend(); }


private:
    size_t count_entries_in_bins() const;

    void fail_if_not_subblk_or_zero_cnt(const Extent& ext) const;
    void fail_if_blk_nr_already_seen(const Extent& ext) const;

    void _provide(const Extent& ext);
};
