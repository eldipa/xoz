#include "xoz/balloc.h"
#include "xoz/arch.h"

#include <iomanip>
#include <cassert>

BlockAllocator::BlockAllocator(Repository& repo) : repo(repo) {}


// TODO we are not handling other strategies
// nor handling the possibility to split the request
// in sub requests.
Extent BlockAllocator::alloc(uint16_t blk_cnt) {
    assert (blk_cnt > 0);

    unsigned bin_nr = u16_log2_floor(blk_cnt);

    // Search from the best fit bin to bins with larger extents
    for (auto ix = bin_nr; ix < sizeof(bins)/sizeof(bins[0]); ++ix) {
        auto& bin = bins[ix];

        // If we found a non-empty bin, use the "first" available
        // extent to fulfill the request.
        //
        // This is in did a "first-match" strategy
        if (!bin.empty()) {
            Extent& ext = bin.back();

            // Best fit?
            if (ext.blk_cnt == blk_cnt) {
                Extent ours = bin.back();
                bin.pop_back();

                return ours;

            } else {
                // The extent is too large, split it
                Extent ours = {
                    .blk_nr = ext.blk_nr + (ext.blk_cnt - blk_cnt),
                    .blk_cnt = blk_cnt
                };

                ext.blk_cnt -= blk_cnt;
                return ours;
            }
        }
    }

    // If we reached here it is because we couldn't find any suitable
    // extent to fulfill the request.
    //
    // Request the repository more free blocks
    //
    // TODO we are not checking overflow in repo.alloc_blocks
    // not returning an error.
    return Extent {
        .blk_nr = repo.alloc_blocks(blk_cnt),
        .blk_cnt = blk_cnt
    };
}

void BlockAllocator::free(const Extent& ext) {
    assert (ext.blk_nr > 0);
    assert (ext.blk_cnt > 0);

    unsigned bin_nr = u16_log2_floor(ext.blk_cnt);
    bins[bin_nr].push_back(ext);
}

std::ostream& BlockAllocator::print_stats(std::ostream& out) const {
    out << "Block Allocator:\n";
    for (unsigned ix = 0; ix < sizeof(bins)/sizeof(bins[0]); ++ix) {
        out << "Bin " << std::hex << ix << std::dec << ": ";
        out << bins[0].size() << " extents";
        out << "\n";
    }
    return out;
}

