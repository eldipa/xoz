#include "xoz/alloc/free_map.h"

#include <cassert>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/log/trace.h"
#include "xoz/mem/integer_ops.h"

#define TRACE TRACE_ON(0x01)

#define TRACE_LINE TRACE << "\t\t\t\t"

namespace xoz::alloc::internals {
FreeMap::FreeMap(bool coalescing_enabled, uint16_t split_above_threshold):
        coalescing_enabled(coalescing_enabled), split_above_threshold(split_above_threshold) {}

void FreeMap::provide(const std::list<Extent>& exts) {
    TRACE_LINE << "v--- provide " << exts.size() << " exts" << TRACE_ENDL;
    for (auto& ext: exts) {
        dealloc(ext);
    }
    TRACE_LINE << "^---" << TRACE_ENDL;

    assert(fr_by_nr.size() == fr_by_cnt.size());
}

void FreeMap::provide(const Extent& ext) {
    TRACE_LINE << "v--- provide 1 ext" << TRACE_ENDL;
    dealloc(ext);
    TRACE_LINE << "^---" << TRACE_ENDL;
    assert(fr_by_nr.size() == fr_by_cnt.size());
}

void FreeMap::reset() {
    TRACE_LINE << "|reset" << TRACE_ENDL;
    fr_by_nr.clear();
    fr_by_cnt.clear();

    assert(fr_by_nr.size() == fr_by_cnt.size());
}

void FreeMap::release(const std::list<Extent>& exts) {
    TRACE_LINE << "v--- release " << exts.size() << " exts" << TRACE_ENDL;
    for (const auto& ext: exts) {
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
    TRACE_LINE << "^---" << TRACE_ENDL;
}

struct FreeMap::alloc_result_t FreeMap::alloc(const uint16_t blk_cnt) {
    fail_alloc_if_empty(blk_cnt, false);
    TRACE_LINE << "|" << TRACE_FLUSH;

    auto end_it = fr_by_cnt.end();
    auto usable_it = fr_by_cnt.lower_bound(blk_cnt);

    // By definition, if usable_it is (at best) a free chunk of
    // exact blk_cnt blocks, the previous element is the closest
    auto closest_it = end_it;
    if (usable_it != end_it) {
        if (usable_it != fr_by_cnt.begin()) {
            closest_it = --usable_it;
            ++usable_it;

            TRACE << "clos usbl " << TRACE_FLUSH;
        } else {
            TRACE << "     usbl " << TRACE_FLUSH;
        }
    } else {
        // Try to use the largest (last) chunk.
        if (fr_by_cnt.size() > 0) {
            closest_it = --fr_by_cnt.end();
            TRACE << "clos      " << TRACE_FLUSH;
        } else {
            TRACE << "          " << TRACE_FLUSH;
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
                TRACE << "nospl,end   " << TRACE_FLUSH;
            } else {
                usable_it = fr_by_cnt.lower_bound(next_blk_cnt);
                TRACE << "nospl,other " << TRACE_FLUSH;
            }
        } else {
            TRACE << "splt        " << TRACE_FLUSH;
        }
    } else if (usable_it == end_it) {
        TRACE << "nousbl      " << TRACE_FLUSH;
    } else {
        TRACE << "            " << TRACE_FLUSH; /* perfect match */
    }

    assert(fr_by_nr.size() == fr_by_cnt.size());

    TRACE << " /  " << std::setw(5) << blk_cnt << " blks req -> " << TRACE_FLUSH;
    if (usable_it == end_it) {
        // We cannot use any of the free chunks
        // so we return the closest free chunk block count that
        // it may be usable if the caller request that block count
        // or less
        uint16_t closest_blk_cnt = 0;
        if (closest_it != end_it) {
            closest_blk_cnt = blk_cnt_of(closest_it);
        }

        TRACE << "fail: closest " << closest_blk_cnt << TRACE_ENDL;
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
        TRACE << "perfect: " << Extent(blk_nr_of(usable_it), blk_cnt_of(usable_it), false) << TRACE_ENDL;

        // perfect match, remove the free chunk entirely
        fr_by_nr.erase(blk_nr_of(usable_it));
        fr_by_cnt.erase(usable_it);

    } else if (blk_cnt_of(usable_it) > blk_cnt) {
        TRACE << "split: " << Extent(blk_nr_of(usable_it), blk_cnt_of(usable_it), false) << TRACE_FLUSH;

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
        TRACE << " -> " << Extent(new_fr_nr, blk_cnt_remain, false) << TRACE_ENDL;
    } else {
        assert(0);
    }

    assert(fr_by_nr.size() == fr_by_cnt.size());
    return {
            .ext = ext,
            .success = true,
    };
}

void FreeMap::dealloc(const Extent& ext) {
    TRACE_LINE << "|" << TRACE_FLUSH;
    auto end_it = fr_by_nr.end();

    // TODO check that ext is in the range of the xoz file?
    fail_if_suballoc_or_zero_cnt(ext);
    fail_if_overlap(ext);

    if (not coalescing_enabled) {
        fr_by_nr.insert({ext.blk_nr(), ext.blk_cnt()});
        fr_by_cnt.insert({ext.blk_cnt(), ext.blk_nr()});
        assert(fr_by_nr.size() == fr_by_cnt.size());

        TRACE << "nocoal:" << ext << TRACE_ENDL;
        return;
    }

    bool coalesced_with_next = false;
    bool coalesced_with_prev = false;

    auto next_fr_it = fr_by_nr.upper_bound(ext.blk_nr());

    Extent coalesced = ext;

    if (next_fr_it != end_it and coalesced.past_end_blk_nr() == blk_nr_of(next_fr_it)) {
        if (not test_u16_add(blk_cnt_of(next_fr_it), coalesced.blk_cnt())) {
            coalesced.expand_by(blk_cnt_of(next_fr_it));
            coalesced_with_next = true;  // then, next_fr_it must be removed
            TRACE << "next:" << Extent(blk_nr_of(next_fr_it), blk_cnt_of(next_fr_it), false) << "  " << TRACE_FLUSH;
        }
    }

    if (next_fr_it != fr_by_nr.begin()) {
        auto prev_fr_it = --next_fr_it;
        ++next_fr_it;

        if ((blk_nr_of(prev_fr_it) + blk_cnt_of(prev_fr_it)) == coalesced.blk_nr()) {
            if (not test_u16_add(blk_cnt_of(prev_fr_it), coalesced.blk_cnt())) {
                // trace *before* modifying prev_fr_it
                TRACE << "prev:" << Extent(blk_nr_of(prev_fr_it), blk_cnt_of(prev_fr_it), false) << "  " << TRACE_FLUSH;

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

                // For tracing purposes, update the coalesced Extent to track its
                // new location and size
                coalesced = Extent(blk_nr_of(prev_fr_it), blk_cnt_of(prev_fr_it), false);
            }
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

        if (coalesced_with_next) {
            TRACE << "coal:" << ext << " -> " << coalesced << TRACE_ENDL;
        } else {
            TRACE << "nocoal:" << coalesced << TRACE_ENDL;
        }
    } else {
        TRACE << "coal:" << ext << " -> " << coalesced << TRACE_ENDL;
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
            Extent::fail_if_overlap(Extent(blk_nr_of(it), blk_cnt_of(it), false), ext);
        } catch (const ExtentOverlapError& err) {
            throw ExtentOverlapError("already freed", Extent(blk_nr_of(it), blk_cnt_of(it), false), "to be freed", ext,
                                     (F() << "possible double free detected").str());
        }
    }
}
}  // namespace xoz::alloc::internals
