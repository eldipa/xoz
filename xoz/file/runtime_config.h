#pragma once
#include "xoz/blk/segment_block_array_flags.h"
#include "xoz/dsc/descriptor_set_flags.h"

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

        /*
         * When a descriptor is erased or the set is cleared or destroyed,
         * check if the user has shared pointers to that descriptor or
         * descriptors of the cleared/destroyed set.
         *
         * Those "external" shared pointers will be pointing to Descriptor
         * objects in an undefined state.
         *
         * These flags define what to do in this case.
         * */
        const uint32_t on_external_ref_action;
    } dset;
};

constexpr static struct runtime_config_t DefaultRuntimeConfig = {
        .dset = {.sg_blkarr_flags = SG_BLKARR_REALLOC_ON_GROW, .on_external_ref_action = DSET_ON_EXTERNAL_REF_PASS}};

}  // namespace xoz
