#pragma once
#include <map>
#include <cstdint>
#include <list>
#include "xoz/ext/extent.h"

#include "xoz/alloc/internals.h"

class SubBlockFreeMap {
    using map_nr2ext_t = xoz::alloc::internals::map_nr2ext_t;

    private:
        std::list<Extent> exts_bin[Extent::SUBBLK_CNT_PER_BLK];

        map_nr2ext_t fr_by_nr;

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

        // Handy typedef
        typedef xoz::alloc::internals::ConstExtentIterator<map_nr2ext_t, true> const_iterator_by_blk_nr_t;

        // Iterators over the free chunks returned as Extent objects.
        // By block number only order
        //
        // All the iterators are constant as the caller must not
        // modify the internals of the free map.
        inline const_iterator_by_blk_nr_t cbegin_by_blk_nr() const {
            return xoz::alloc::internals::ConstExtentIterator<map_nr2ext_t, true>(fr_by_nr.cbegin());
        }

        inline const_iterator_by_blk_nr_t cend_by_blk_nr() const {
            return xoz::alloc::internals::ConstExtentIterator<map_nr2ext_t, true>(fr_by_nr.cend());
        }

    private:

        size_t count_entries_in_bins() const;

        void fail_if_not_subblk_or_zero_cnt(const Extent& ext) const;
        void fail_if_blk_nr_already_seen(const Extent& ext) const;
};

