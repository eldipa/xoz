#pragma once

#include <cstdint>

#include "xoz/dsc/descriptor.h"

namespace xoz::dsc::internals {

//
// Classes meant to see the inners of Descriptor instances.
// Used only for some internal operations within File and DescriptorSet classes
// and for testing.
//
// Do not use in general.
//

class DescriptorInnerSpyForInternal {
public:
    explicit DescriptorInnerSpyForInternal(const Descriptor& dsc): dsc(dsc) {}
    // Return the size in bytes to represent the Descriptor structure in disk
    // *including* the descriptor internal data (see calc_internal_data_space_size)
    inline uint32_t /* F S */ calc_struct_footprint_size() const { return dsc.calc_struct_footprint_size(); }

    inline uint16_t type() const { return dsc.hdr.type; }

    /*
     * Does the descriptor owns content?
     * */
    inline bool /* F S */ does_own_content() const { return dsc.count_incompressible_cparts() > 0; }

protected:
    const Descriptor& dsc;
};

class DescriptorInnerSpyForTesting: public DescriptorInnerSpyForInternal {
public:
    explicit DescriptorInnerSpyForTesting(const Descriptor& dsc): DescriptorInnerSpyForInternal(dsc) {}

    // Return the size in bytes of that referenced by the segment and
    // that represent the content of the descriptor (not the descriptor's internal data).
    //
    // The size may be larger than calc_declared_hdr_csize()
    // (the csize field in the content part in the descriptor header) if the descriptor has more space allocated
    // than the declared in csize.
    // In this sense, calc_segm_data_space_size() is the
    // total usable space while hdr.csize (or calc_declared_hdr_csize) is the used space.
    //
    // For non-owner descriptors returns always 0
    inline uint32_t calc_segm_data_space_size(uint32_t part_num) const {
        if (dsc.hdr.cparts.size() > 0) {
            return dsc.hdr.cparts.at(part_num).segm.calc_data_space_size();
        } else {
            return 0;
        }
    }

    // Return the content size including any future content.
    // If the descriptor does not own content, return 0
    inline uint32_t calc_declared_hdr_csize(uint32_t part_num) const {
        if (dsc.hdr.cparts.size() > 0) {
            return dsc.hdr.cparts.at(part_num).csize;
        } else {
            return 0;
        }
    }

    // Return the size in bytes that this descriptor has for internal data.
    // Such internal data space can be used by a Descriptor subclass
    // to retrieve / store specifics fields.
    // For the perspective of this method, such interpretation is transparent
    // and the whole space is seen as a single consecutive chunk of space
    // whose size is returned.
    inline uint32_t calc_internal_data_space_size() const { return dsc.hdr.isize; }
};
}  // namespace xoz::dsc::internals
