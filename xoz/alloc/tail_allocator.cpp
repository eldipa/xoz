
#include "xoz/alloc/tail_allocator.h"

#include <cassert>
#include <cstdint>

#include "xoz/alloc/internals.h"
#include "xoz/blk/block_array.h"
#include "xoz/ext/extent.h"
#include "xoz/file/file.h"


namespace xoz::alloc::internals {
TailAllocator::TailAllocator(): blkarr(nullptr) {}

void TailAllocator::manage_block_array(BlockArray& blkarr) { this->blkarr = &blkarr; }

struct TailAllocator::alloc_result_t TailAllocator::alloc(uint16_t blk_cnt) {
    fail_if_block_array_not_initialized();
    fail_alloc_if_empty(blk_cnt, false);

    auto blk_nr = blkarr->grow_by_blocks(blk_cnt);
    return {.ext = Extent(blk_nr, blk_cnt, false), .success = true};
}

bool TailAllocator::dealloc(const Extent& ext) {
    fail_if_block_array_not_initialized();
    if (is_at_the_end(ext)) {
        blkarr->shrink_by_blocks(ext.blk_cnt());
        return true;
    }

    return false;
}

bool TailAllocator::dealloc(const uint32_t blk_nr, const uint16_t blk_cnt) {
    return dealloc(Extent(blk_nr, blk_cnt, false));
}

void TailAllocator::release() {
    fail_if_block_array_not_initialized();
    blkarr->release_blocks();
}

void TailAllocator::reset() {
    fail_if_block_array_not_initialized();
    if (blkarr->blk_cnt()) {
        blkarr->shrink_by_blocks(blkarr->blk_cnt());
    }
    release();
}

bool TailAllocator::is_at_the_end(const Extent& ext) const {
    fail_if_block_array_not_initialized();
    fail_if_suballoc_or_zero_cnt(ext);
    blkarr->fail_if_out_of_boundaries(ext, "Detected on TailAllocator::dealloc");

    // Knowing that the extent is within the boundaries
    // of the xoz file, checking the extent's past_end_blk_nr
    // is enough to know that the extent is exactly at the
    // end of the xoz file (aka, the tail).
    return (ext.past_end_blk_nr() == blkarr->past_end_blk_nr());
}

void TailAllocator::fail_if_block_array_not_initialized() const {
    if (not blkarr) {
        throw std::runtime_error("Block array not initialized (managed). Missed call to manage_block_array?");
    }
}
}  // namespace xoz::alloc::internals
