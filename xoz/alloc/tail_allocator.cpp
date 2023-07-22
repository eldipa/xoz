
#include <cassert>
#include <cstdint>
#include "xoz/ext/extent.h"
#include "xoz/repo/repo.h"
#include "xoz/alloc/tail_allocator.h"
#include "xoz/alloc/internals.h"

using namespace xoz::alloc::internals;

TailAllocator::TailAllocator(Repository& repo) : repo(repo) {}

// Result of an allocation.
struct alloc_result_t {
    Extent ext;
    bool success;
};

struct TailAllocator::alloc_result_t TailAllocator::alloc(uint16_t blk_cnt) {
    fail_alloc_if_empty(blk_cnt, false);

    uint32_t blk_nr = repo.grow_by_blocks(blk_cnt);
    return {
        .ext = Extent(blk_nr, blk_cnt, false),
        .success = true
    };
}

bool TailAllocator::dealloc(const Extent& ext) {
    fail_if_suballoc_or_zero_cnt(ext);
    repo.fail_if_out_of_boundaries(
            ext,
            "Detected on TailAllocator::dealloc"
            );

    // Knowing that the extent is within the boundaries
    // of the repository, checking the extent's past_end_blk_nr
    // is enough to know that the extent is exactly at the
    // end of the repository (aka, the tail).
    if (ext.past_end_blk_nr() == repo.past_end_data_blk_nr()) {
        repo.shrink_by_blocks(ext.blk_cnt());
        return true;
    }

    return false;
}

bool TailAllocator::dealloc(const uint32_t blk_nr, const uint16_t blk_cnt) {
    return dealloc(Extent(blk_nr, blk_cnt, false));
}
