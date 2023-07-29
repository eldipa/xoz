#include "xoz/alloc/free_map.h"

#include <cassert>
#include <utility>

#include "xoz/exceptions.h"

using namespace xoz::alloc::internals;  // NOLINT

FreeMap::FreeMap(bool coalescing_enabled, uint16_t split_above_threshold):
        coalescing_enabled(coalescing_enabled), split_above_threshold(split_above_threshold) {}

void FreeMap::provide(const std::list<Extent>& exts) {
    for (auto& ext: exts) {
        dealloc(ext);
    }

    assert(fr_by_nr.size() == fr_by_cnt.size());
}

void FreeMap::provide(const Extent& ext) {
    dealloc(ext);
    assert(fr_by_nr.size() == fr_by_cnt.size());
}

void FreeMap::reset() {
    fr_by_nr.clear();
    fr_by_cnt.clear();

    assert(fr_by_nr.size() == fr_by_cnt.size());
}

void FreeMap::release(const std::list<Extent>& exts) {
    for (const auto& ext : exts) {
        // fr_by_nr may change in each iteration so keep an
        // update end() iter.
        auto end_it = fr_by_nr.end();

        auto ours_it = fr_by_nr.find(ext.blk_nr());
        if (ours_it == end_it or blk_cnt_of(ours_it) != ext.blk_cnt()) {
            throw "no such extent";
        }

        erase_from_fr_by_cnt(ours_it);
        fr_by_nr.erase(ours_it);
    }
}

struct FreeMap::alloc_result_t FreeMap::alloc(uint16_t blk_cnt) {
    fail_alloc_if_empty(blk_cnt, false);

    auto end_it = fr_by_cnt.end();
    auto usable_it = fr_by_cnt.lower_bound(blk_cnt);

    // By definition, if usable_it is (at best) a free chunk of
    // exact blk_cnt blocks, the previous element is the closest
    auto closest_it = end_it;
    if (usable_it != end_it) {
        if (usable_it != fr_by_cnt.begin()) {
            closest_it = --usable_it;
            ++usable_it;
        }
    } else {
        // Try to use the largest (last) chunk.
        if (fr_by_cnt.size() > 0) {
            closest_it = --fr_by_cnt.end();
        }
    }

    // It is expected that usable_it to be of exact blk_cnt blocks,
    // a perfect fit/match.
    //
    // If not usable_it will point to free chunks larger than blk_cnt
    // however if split_above_threshold is non-zero, the first
    // free chunks found by iteration usable_it will be rejected
    // because even if they are strictly larger than blk_cnt, they
    // cannot be used because they are below the split threshold.
    //
    // In this case we do another lower_bound() call to skip those
    // chunks and iterate from there.
    if (usable_it != end_it and blk_cnt_of(usable_it) != blk_cnt) {
        uint16_t blk_cnt_remain = blk_cnt_of(usable_it) - blk_cnt;

        if (blk_cnt_remain <= split_above_threshold) {
            uint16_t next_blk_cnt = blk_cnt;
            next_blk_cnt += split_above_threshold;
            ++next_blk_cnt;

            if (next_blk_cnt <= blk_cnt) {
                // overflow, don't do anything and assume that
                // there are no more usable free chunks to iterate
                usable_it = end_it;
            } else {
                usable_it = fr_by_cnt.lower_bound(next_blk_cnt);
            }
        }
    }

    assert(fr_by_nr.size() == fr_by_cnt.size());

    if (usable_it == end_it) {
        // We cannot use any of the free chunks
        // so we return the closest free chunk block count that
        // it may be usable if the caller request that block count
        // or less
        uint16_t closest_blk_cnt = 0;
        if (closest_it != end_it) {
            closest_blk_cnt = blk_cnt_of(closest_it);
        }

        Extent ext(0, closest_blk_cnt, false);
        return {
                .ext = ext,
                .success = false,
        };
    }

    // Free chunk found. Use it
    Extent ext(blk_nr_of(usable_it), blk_cnt, false);

    // NOTE: from here and on, end_it and any other iterator
    // will be invalid as the following code will mutate fr_by_cnt
    if (blk_cnt_of(usable_it) == blk_cnt) {
        // perfect match, remove the free chunk entirely
        fr_by_nr.erase(blk_nr_of(usable_it));
        fr_by_cnt.erase(usable_it);

    } else if (blk_cnt_of(usable_it) > blk_cnt) {
        // not a perfect match, split the free chunk is required
        uint16_t blk_cnt_remain = blk_cnt_of(usable_it) - blk_cnt;
        uint32_t new_fr_nr = blk_nr_of(usable_it) + blk_cnt;

        assert(blk_cnt_remain > split_above_threshold);

        // We do the lookup (find) separately from the erase() call
        // so the erase() call returns us an iterator that points
        // to the element *after* the deleted one.
        //
        // In the context of fr_by_nr this will point to a free chunk
        // with a greater block number.
        //
        // We use it as a hint to speed up the insertion. The insert()
        // requires an iterator just after the element to be inserted
        // to get a O(1) performance.
        //
        // This takes advantage that fr_by_nr is sorted by block number
        // and that the added chunk's blk_nr is strictly less than
        // hint's blk_nr and *no* other chunk exist in between.
        //
        // The expected total cost is O(log(n))
        const auto hint_it = fr_by_nr.erase(fr_by_nr.find(blk_nr_of(usable_it)));
        fr_by_nr.insert(hint_it, pair_nr2cnt_t(new_fr_nr, blk_cnt_remain));

        // Update the fr_by_cnt. We cannot use the same "hint" trick
        // than before because blk_cnt_remain may or may not be near
        // of usable_it->first (aka blk_cnt_of(usable_it))
        //
        // We have to pay O(log(n)) twice additionally
        fr_by_cnt.erase(usable_it);  // erase first, so usable_it is still valid
        fr_by_cnt.insert({blk_cnt_remain, new_fr_nr});
    }

    assert(fr_by_nr.size() == fr_by_cnt.size());
    return {
            .ext = ext,
            .success = true,
    };
}

void FreeMap::dealloc(const Extent& ext) {
    auto end_it = fr_by_nr.end();

    // TODO check that ext is in the range of the repository?
    fail_if_suballoc_or_zero_cnt(ext);
    fail_if_overlap(ext);

    if (not coalescing_enabled) {
        fr_by_nr.insert({ext.blk_nr(), ext.blk_cnt()});
        fr_by_cnt.insert({ext.blk_cnt(), ext.blk_nr()});
        assert(fr_by_nr.size() == fr_by_cnt.size());
        return;
    }

    bool coalesced_with_next = false;
    bool coalesced_with_prev = false;

    auto next_fr_it = fr_by_nr.upper_bound(ext.blk_nr());

    Extent coalesced = ext;

    if (next_fr_it != end_it and coalesced.past_end_blk_nr() == blk_nr_of(next_fr_it)) {
        coalesced.expand_by(blk_cnt_of(next_fr_it));
        coalesced_with_next = true;  // then, next_fr_it must be removed
    }

    if (next_fr_it != fr_by_nr.begin()) {
        auto prev_fr_it = --next_fr_it;
        ++next_fr_it;

        if ((blk_nr_of(prev_fr_it) + blk_cnt_of(prev_fr_it)) == coalesced.blk_nr()) {
            // Update prev free chunk in-place, after the coalesced extent was
            // coalesced with the next free chunk (if possible)
            //
            // This implies a change in the block count so we must remove the chunk
            // from the map that tracks it by block count.
            erase_from_fr_by_cnt(prev_fr_it);

            // Update in-place
            blk_cnt_of(prev_fr_it) += coalesced.blk_cnt();

            // Re insert the previously deleted entry from the map tracking
            // by block count, now with the updated count.
            fr_by_cnt.insert({blk_cnt_of(prev_fr_it), blk_nr_of(prev_fr_it)});

            coalesced_with_prev = true;  // then, prev_fr_it must *not* be removed
        }
    }

    // Remove the 'next' chunk coalesced from both maps
    if (coalesced_with_next) {
        erase_from_fr_by_cnt(next_fr_it);
        fr_by_nr.erase(next_fr_it);
    }

    // Insert the deallocated chunk, possibly coalesced, in both maps.
    if (not coalesced_with_prev) {
        fr_by_nr.insert({coalesced.blk_nr(), coalesced.blk_cnt()});
        fr_by_cnt.insert({coalesced.blk_cnt(), coalesced.blk_nr()});
    }

    assert(fr_by_nr.size() == fr_by_cnt.size());
}

// Erase from the multimap fr_by_cnt the chunk pointed by target_it
// (coming from the fr_by_nr map)
//
// This erase operation does a O(log(n)) lookup on fr_by_cnt but because
// there may be multiple chunks with the same block count, there is
// a O(n) linear search to delete the one pointed by target_it
FreeMap::multimap_cnt2nr_t::iterator FreeMap::erase_from_fr_by_cnt(FreeMap::map_nr2cnt_t::iterator& target_it) {
    for (auto it = fr_by_cnt.lower_bound(blk_cnt_of(target_it)); it != fr_by_cnt.end(); ++it) {
        if (blk_nr_of(it) == blk_nr_of(target_it)) {
            assert(blk_cnt_of(it) == blk_cnt_of(target_it));
            return fr_by_cnt.erase(it);
        }
    }

    return fr_by_cnt.end();
}

void FreeMap::fail_if_overlap(const Extent& ext) const {
    if (fr_by_nr.size() == 0) {
        return;
    }

    map_nr2cnt_t::const_iterator to_chk[2];

    int i = 0;

    auto it = fr_by_nr.lower_bound(ext.blk_nr());
    if (it != fr_by_nr.end()) {
        to_chk[i] = it;  // ext.blk_nr <= blk_nr_of(it)
        ++i;
    }

    if (it != fr_by_nr.begin()) {
        to_chk[i] = --it;  // blk_nr_of(it) < ext.blk_nr
        ++i;
    }

    for (; i > 0;) {
        it = to_chk[--i];
        try {
            Extent::distance_in_blks(Extent(blk_nr_of(it), blk_cnt_of(it), false), ext);
        } catch (const ExtentOverlapError& err) {
            throw ExtentOverlapError("already freed", Extent(blk_nr_of(it), blk_cnt_of(it), false), "to be freed", ext,
                                     (F() << "possible double free detected").str());
        }
    }
}
