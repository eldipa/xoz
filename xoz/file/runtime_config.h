#pragma once

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
}  // namespace xoz
