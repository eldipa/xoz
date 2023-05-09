#pragma once

#include "xoz/extent.h"
#include "xoz/repo.h"
#include <list>
#include <cstdint>
#include <iostream>

class BlockAllocator {
    private:
        // A statically allocated array of 16 std::list
        //
        // Each nth bin (std::list) will contain the extent of free
        // blocks of lengths L where 2**n <= L < 2**n+1
        //
        // We support extent of up to 2**16 consecutive blocks
        // of length so at most we will have a bin of order 16
        //
        std::list<Extent> bins[16];

        // Just validate the assumption made above: extent's length/count
        // are encoded as 2 bytes (2**16).
        static_assert(sizeof(((Extent*)0)->blk_cnt) == 2);

        Repository& repo;

    public:
        BlockAllocator(Repository& repo);

        Extent alloc(uint16_t blk_cnt);
        void free(const Extent& ext);

        std::ostream& print_stats(std::ostream& out) const;
};
