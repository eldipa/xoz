#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include "xoz/dsc/descriptor.h"

namespace xoz {
class BlockArray;

// Signature that a function must honor to be used as a descriptor-create function
// It takes a descriptor (common) header and it must return a dynamic allocated
// subclass of Descriptor as a pointer to the base class Descriptor.
//
// Once created the read_struct_specifics_from is invoked to complete the initialization
// of the subclass descriptor
typedef std::unique_ptr<Descriptor> (*descriptor_create_fn)(const struct Descriptor::header_t& hdr,
                                                            BlockArray& ed_blkarr, RuntimeContext& rctx);

class DescriptorMapping {
public:
    explicit DescriptorMapping(const std::map<uint16_t, descriptor_create_fn>& descriptors_map,
                               bool override_reserved = false);
    DescriptorMapping(const DescriptorMapping&) = default;

public:
    // Given its type returns a function to create such descriptor.
    // If not suitable function is found, return a function to create
    // a default descriptor that has the minimum logic to work
    // (this enables XOZ to be forward compatible)
    descriptor_create_fn descriptor_create_lookup(uint16_t type) const;

public:
    /*
     * Reserved range of type numbers.
     * This are used internally by xoz library.
     * */
    const static uint16_t RESERVED_CORE_MIN_TYPE = 0x0000;
    const static uint16_t RESERVED_CORE_MAX_TYPE = 0x0000 + 4;

    const static uint16_t RESERVED_METADATA_MIN_TYPE = 0x01bf;
    const static uint16_t RESERVED_METADATA_MAX_TYPE = 0x01bf + 32;

    const static uint16_t RESERVED_ZERO_TYPE = 0x0000;
    const static uint16_t RESERVED_LAST_TYPE = 0xffff;

    /*
     * Any subclass that wants to be a subclass of DescriptorSet *must*
     * have a descriptor type within this range.
     * */
    const static uint16_t DSET_SUBCLASS_MIN_TYPE = 0x01e0;
    const static uint16_t DSET_SUBCLASS_MAX_TYPE = 0x01e0 + 2048;

private:
    std::map<uint16_t, descriptor_create_fn> mapping;
};
}  // namespace xoz
