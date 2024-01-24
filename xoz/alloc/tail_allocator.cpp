
#include "xoz/alloc/tail_allocator.h"

#include <cassert>
#include <cstdint>

#include "xoz/alloc/internals.h"
#include "xoz/ext/block_array.h"
#include "xoz/ext/extent.h"
#include "xoz/repo/repository.h"

using namespace xoz::alloc::internals;  // NOLINT

TailAllocator::TailAllocator(BlockArray& blkarr): blkarr(blkarr) {}

struct TailAllocator::alloc_result_t TailAllocator::alloc(uint16_t blk_cnt) {
    fail_alloc_if_empty(blk_cnt, false);

    uint32_t blk_nr = blkarr.grow_by_blocks(blk_cnt);
    return {.ext = Extent(blk_nr, blk_cnt, false), .success = true};
}

bool TailAllocator::dealloc(const Extent& ext) {
    if (is_at_the_end(ext)) {
        blkarr.shrink_by_blocks(ext.blk_cnt());
        return true;
    }

    return false;
}

bool TailAllocator::dealloc(const uint32_t blk_nr, const uint16_t blk_cnt) {
    return dealloc(Extent(blk_nr, blk_cnt, false));
}

bool TailAllocator::is_at_the_end(const Extent& ext) const {
    fail_if_suballoc_or_zero_cnt(ext);
    blkarr.fail_if_out_of_boundaries(ext, "Detected on TailAllocator::dealloc");

    // Knowing that the extent is within the boundaries
    // of the repository, checking the extent's past_end_blk_nr
    // is enough to know that the extent is exactly at the
    // end of the repository (aka, the tail).
    return (ext.past_end_blk_nr() == blkarr.past_end_blk_nr());
}
