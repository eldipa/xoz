#pragma once
#include "xoz/blk/segment_block_array_flags.h"

namespace xoz {
struct runtime_config_t {
    /*
     * Configuration used by DescriptorSet
     * */
    struct {

        /*
         * A DescriptorSet has a SegmentBlockArray object that manages
         * the storage of the descriptors (their struct and idata, not their content).
         *
         * These flags fine tune its behavior.
         * */
        const uint32_t sg_blkarr_flags;
    } dset;
};

constexpr static struct runtime_config_t DefaultRuntimeConfig = {
        .dset = {.sg_blkarr_flags = SG_BLKARR_REALLOC_ON_GROW}};

}  // namespace xoz
