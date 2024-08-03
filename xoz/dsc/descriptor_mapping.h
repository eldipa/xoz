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
    const static uint16_t TYPE_RESERVED_THRESHOLD = 4;

    const static uint16_t DSET_TYPE = 0x0001;
    const static uint16_t DSET_SUBCLASS_MIN_TYPE = 0x01e0;
    const static uint16_t DSET_SUBCLASS_MAX_TYPE = 0x01e0 + 2048;

private:
    std::map<uint16_t, descriptor_create_fn> mapping;
};
}  // namespace xoz
