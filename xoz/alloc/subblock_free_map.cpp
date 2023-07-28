#include "xoz/alloc/subblock_free_map.h"

#include <cassert>
#include <utility>

#include "xoz/exceptions.h"

using namespace xoz::alloc::internals;  // NOLINT

SubBlockFreeMap::SubBlockFreeMap() {}

void SubBlockFreeMap::provide(const std::list<Extent>& exts) {
    for (auto& ext: exts) {
        fail_if_not_subblk_or_zero_cnt(ext);
        fail_if_blk_nr_already_seen(ext);

        fr_by_nr.insert({ext.blk_nr(), ext});

        uint8_t bin = ext.subblk_cnt() - 1;
        exts_bin[bin].push_back(ext);
    }

    assert(fr_by_nr.size() == count_entries_in_bins());
}

void SubBlockFreeMap::clear() {
    fr_by_nr.clear();

    for (uint8_t bin = 0; bin < Extent::SUBBLK_CNT_PER_BLK; ++bin) {
        exts_bin[bin].clear();
    }

    assert(fr_by_nr.size() == count_entries_in_bins());
}

struct SubBlockFreeMap::alloc_result_t SubBlockFreeMap::alloc(uint8_t subblk_cnt) {
    fail_alloc_if_empty(subblk_cnt, true);

    if (subblk_cnt > Extent::SUBBLK_CNT_PER_BLK) {
        throw std::runtime_error((F() << "subblock count out of range: given " << subblk_cnt << " but max is "
                                      << Extent::SUBBLK_CNT_PER_BLK << " subblocks")
                                         .str());
    }

    Extent free_ext(0, 0, true);

    // Find the first block empty enough to hold
    // the subblocks requested:
    // best strategy followed by first strategy
    //
    // Once found, remove it from its bin. The allocation
    // will change its size so we will have to move it
    // anyways
    //
    // Note: if we cannot find a suitable block in the best bin
    // we try the next ones. We could implement something
    // like FreeMap::split_above_threshold to avoid using blocks
    // that would endup having too few subblocks free but
    // it is not clear that it will reduce the fragmentation.
    //
    // If the "best" strategy fails, just proceed with the "first"
    // strategy.
    uint8_t bin = subblk_cnt - 1;
    for (; bin < Extent::SUBBLK_CNT_PER_BLK; ++bin) {
        if (exts_bin[bin].size() > 0) {
            free_ext = exts_bin[bin].back();
            exts_bin[bin].pop_back();
            break;
        }
    }

    // Too bad, nothing was found.
    // Return empty extent and signal failure
    if (free_ext.subblk_cnt() == 0) {
        assert(fr_by_nr.size() == count_entries_in_bins());
        return {
                .ext = free_ext,
                .success = false,
        };
    }

    // How much is free exactly?
    uint8_t free_subblk_cnt = free_ext.subblk_cnt();
    uint16_t free_bitmask = free_ext.blk_bitmap();

    uint16_t allocated_bitmask = 0;

    // Alloc from MSB to LSB
    for (int i = Extent::SUBBLK_CNT_PER_BLK - 1; i >= 0 and subblk_cnt > 0; --i) {
        uint16_t bitsel = (1 << i);
        // If set, it means free to use:
        //  - remove it from free_bitmask
        //  - add it to allocated_bitmask
        if (free_bitmask & bitsel) {
            allocated_bitmask |= bitsel;
            free_bitmask &= (uint16_t)(~bitsel);

            assert(free_subblk_cnt > 0);

            --subblk_cnt;
            --free_subblk_cnt;
        }
    }

    assert(subblk_cnt == 0);

    // Free chunks allocated. Save them.
    Extent ext(free_ext.blk_nr(), allocated_bitmask, true);
    free_ext.set_bitmap(free_bitmask);
    assert(free_ext.subblk_cnt() == free_subblk_cnt);

    assert((ext.blk_bitmap() & (~free_ext.blk_bitmap())) == ext.blk_bitmap());
    if (free_subblk_cnt == 0) {
        // block fully allocated
        // it was a perfect match
        //
        // Already removed from the bin, remove it
        // from the fr_by_nr too
        auto nr_it = fr_by_nr.find(free_ext.blk_nr());
        assert(nr_it != fr_by_nr.end());
        fr_by_nr.erase(nr_it);

    } else {
        // Update and readd the extent to its new bin
        uint8_t new_bin = free_ext.subblk_cnt() - 1;
        exts_bin[new_bin].push_back(free_ext);

        // Update it in the by-blk-nr map
        auto nr_it = fr_by_nr.find(free_ext.blk_nr());
        assert(nr_it != fr_by_nr.end());

        nr_it->second = free_ext;
    }

    assert(fr_by_nr.size() == count_entries_in_bins());
    return {
            .ext = ext,
            .success = true,
    };
}

void SubBlockFreeMap::dealloc(const Extent& ext) {
    auto end_it = fr_by_nr.end();

    fail_if_not_subblk_or_zero_cnt(ext);

    // See if there is a partially used block with
    // that block number (meaning that that block
    // is being partially used and partially free and
    // the given ext is going to free even more)
    //
    // If not found, assume that ext is freeing a possibly
    // partial *new* block that was once fully allocated
    // (and therefore not present in the map/bin)
    Extent free_ext(0, 0, true);

    auto free_it = fr_by_nr.find(ext.blk_nr());
    bool found_in_nr_map = false;

    if (free_it != end_it) {
        free_ext = free_it->second;
        found_in_nr_map = true;

        // What it was allocated (ext.blk_bitmap()) should
        // be marked as not-free (~free_ext.blk_bitmap())
        // otherwise it is an error (double free)
        if ((ext.blk_bitmap() & (~free_ext.blk_bitmap())) != ext.blk_bitmap()) {
            throw ExtentOverlapError("already freed", free_ext, "to be freed", ext,
                                     (F() << "possible double free detected").str());
        }

        // Search the not-updated-yet free extent and remove it
        // from its bin
        uint8_t bin = free_ext.subblk_cnt() - 1;
        bool found_in_bin = false;
        for (auto it = exts_bin[bin].begin(); it != exts_bin[bin].end(); ++it) {
            if (it->blk_nr() == free_ext.blk_nr()) {
                // Found. Remove it.
                exts_bin[bin].erase(it);
                found_in_bin = true;
                break;
            }
        }

        assert(found_in_bin);

    } else {
        // If ext is not in fr_by_nr it must not be in any bins either
        for (uint8_t bin = 0; bin < Extent::SUBBLK_CNT_PER_BLK; ++bin) {
            for (auto it = exts_bin[bin].begin(); it != exts_bin[bin].end(); ++it) {
                assert(it->blk_nr() != ext.blk_nr());
            }
        }
    }

    // Update the free extent with the deallocated ext
    free_ext.set_bitmap(free_ext.blk_bitmap() | ext.blk_bitmap());
    free_ext.move_to(ext.blk_nr());

    // Add it to its new bin
    uint8_t bin = free_ext.subblk_cnt() - 1;  // TODO bin == 15 should be freed, return dealloc_result?
    exts_bin[bin].push_back(free_ext);

    // Update the map indexed by blk_nr with the new bitmap
    if (found_in_nr_map) {
        free_it->second = free_ext;
    } else {
        fr_by_nr.insert({free_ext.blk_nr(), free_ext});
    }

    assert(fr_by_nr.size() == count_entries_in_bins());
}

std::list<Extent> SubBlockFreeMap::release([[maybe_unused]] bool mandatory) {
    std::list<Extent> ret;
    exts_bin[15].swap(ret);
    for (auto const& ext : ret) {
        fr_by_nr.erase(ext.blk_nr());
    }

    assert(fr_by_nr.size() == count_entries_in_bins());
    return ret;
}

size_t SubBlockFreeMap::count_entries_in_bins() const {
    size_t accum = 0;
    for (uint8_t bin = 0; bin < Extent::SUBBLK_CNT_PER_BLK; ++bin) {
        accum += exts_bin[bin].size();
    }

    return accum;
}

void SubBlockFreeMap::fail_if_not_subblk_or_zero_cnt(const Extent& ext) const {
    if (not ext.is_suballoc() or ext.blk_bitmap() == 0) {
        throw std::runtime_error(
                (F() << "cannot dealloc "
                     << ((not ext.is_suballoc()) ? "extent that it is not for suballocation" : "0 subblocks"))
                        .str());
    }
}

void SubBlockFreeMap::fail_if_blk_nr_already_seen(const Extent& ext) const {
    auto it = fr_by_nr.find(ext.blk_nr());
    if (it != fr_by_nr.end()) {
        throw ExtentOverlapError("already freed", it->second, "to be freed", ext,
                                 (F() << "both have the same block number (bitmap ignored in the check)").str());
    }
}
