#include "xoz/balloc.h"
#include "xoz/arch.h"

#include <iomanip>
#include <cassert>

BlockAllocator::BlockAllocator(Repository& repo) :
    repo(repo),
    gp(repo.params()),
    free_blk_cnt(0),
    internal_fragmentation(0),
    highest_blk_nr(0) {}


// TODO we are not handling other strategies
// nor handling the possibility to split the request
// in sub requests.
std::list<Extent> BlockAllocator::alloc(const BlockRequest& req) {
    assert (req.blk_cnt > 0);
    assert (req.obj_size > 0);
    assert (req.obj_size <= (uint64_t)(req.blk_cnt << gp.blk_sz_order));
    assert (req.obj_size >  (uint64_t)((req.blk_cnt-1) << gp.blk_sz_order));

    // Currently we don't support either groups nor neighbors
    assert (req.group == 0);
    assert (req.max_neighbor_depth == 0);

    std::list<Extent> allocd;

    unsigned bin_nr = u16_log2_floor(req.blk_cnt);

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
            if (ext.blk_cnt == req.blk_cnt) {
                allocd.push_back(bin.back());
                bin.pop_back();

                // Best fit found
                break;

            } else {
                // The extent is too large, split it
                allocd.push_back({
                    .blk_nr = ext.blk_nr + (ext.blk_cnt - req.blk_cnt),
                    .blk_cnt = req.blk_cnt
                });

                // Modify in-place the extent in the bin, do not
                // pop it.
                ext.blk_cnt -= req.blk_cnt;

                // Near
                break;
            }
        }
    }

    if (!allocd.empty()) {
        // Currently we support contiguous allocations only.
        // So or we found and fulfilled the request in one shot
        // or we didn't
        //
        // If the list is non-empty then we should be in the first case
        assert (allocd.size() == 1);
        assert (allocd.back().blk_cnt == req.blk_cnt);

    } else {

        // If we reached here it is because we couldn't find any suitable
        // extent to fulfill the request.
        //
        // Request the repository more free blocks
        //
        // TODO we are not checking overflow in repo.alloc_blocks
        // not returning an error.
        uint32_t new_first_blk_nr = repo.alloc_blocks(req.blk_cnt);
        highest_blk_nr = new_first_blk_nr + req.blk_cnt - 1;

        allocd.push_back({
                .blk_nr = new_first_blk_nr,
                .blk_cnt = req.blk_cnt
                });

        free_blk_cnt += req.blk_cnt;
    }


    // Keep some stats
    free_blk_cnt -= req.blk_cnt;

    // TODO we are not tracking which blocks are underuse so we cannot
    // know how much frag mem is freed on call free()
    internal_fragmentation += (req.blk_cnt << gp.blk_sz_order) - req.obj_size;

    return allocd;
}

// TODO how to prevent double free?
// and 2 overlapping free?
// and a free that frees beyond the range?
void BlockAllocator::free(const Extent& ext) {
    assert (ext.blk_nr > 0);
    assert (ext.blk_cnt > 0);

    unsigned bin_nr = u16_log2_floor(ext.blk_cnt);
    bins[bin_nr].push_back(ext);

    free_blk_cnt += ext.blk_cnt;
}

// TODO: this is a really ugly O(n^2) algorithm with n being the number
// of extents.
// We could improve it to ~O(n) if the bins are kind-of sorted but
// it may not worth it.
void BlockAllocator::try_release() {
    // The first free block "next to be allocated" is the one immediately
    // beyond the repository's current limit.
    uint32_t first_blk_nr = highest_blk_nr + 1;

    uint32_t b = 0;
    do {
        // The first_blk_nr - 1 ensures that the extent that we are looking
        // for is immediately *before* the current one so we are only
        // keep iterating as long as we keep finding consecutive free blocks
        b = remove_extent_ending_in(first_blk_nr - 1);
        if (b > 0) {
            assert (first_blk_nr > b);
            first_blk_nr = b; // lower the blk number
        }
    } while (b);

    // Calculate how many consecutive free blocks are at the end
    // of the repository and update the highest_blk_nr allocated block
    uint32_t highest_free_blk_cnt = highest_blk_nr - first_blk_nr + 1;
    highest_blk_nr -= highest_free_blk_cnt;

    if (highest_free_blk_cnt) {
        repo.free_blocks(highest_free_blk_cnt);

        assert (free_blk_cnt >= highest_free_blk_cnt);
        free_blk_cnt -= highest_free_blk_cnt;
    }
}

uint32_t BlockAllocator::remove_extent_ending_in(uint32_t end_blk_nr) {
    for (unsigned i = 0; i < sizeof(bins)/sizeof(bins[0]); ++i) {
        for (auto it = bins[i].begin(); it != bins[i].end(); ++it) {
            const auto& ext = *it;
            if (ext.blk_nr + ext.blk_cnt - 1 == end_blk_nr) {
                uint32_t blk_nr = ext.blk_nr; // copy so we can call erase()
                bins[i].erase(it);

                return blk_nr;
            }
        }
    }

    // Return blk nr 0 to signal "not found"
    return 0;
}

std::ostream& BlockAllocator::print_stats(std::ostream& out) const {
    out << "Block Allocator:\n";
    out << "Free: "
        << (free_blk_cnt << gp.blk_sz_order)
        << " bytes, "
        << free_blk_cnt
        << " blocks.\n";

    out << "Internal fragmentation: "
        << internal_fragmentation
        << " bytes.\n";

    for (unsigned i = 0; i < sizeof(bins)/sizeof(bins[0]); ++i) {
        out << "Bin " << std::hex << i << std::dec << ": ";
        out << bins[i].size() << " extents, "
            << ((bins[i].size() << i) << gp.blk_sz_order)
            << " bytes\n";
    }
    return out;
}

