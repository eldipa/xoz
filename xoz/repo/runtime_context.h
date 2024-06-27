#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include "xoz/dsc/descriptor.h"
#include "xoz/repo/id_manager.h"

class BlockArray;

// Signature that a function must honor to be used as a descriptor-create function
// It takes a descriptor (common) header and it must return a dynamic allocated
// subclass of Descriptor as a pointer to the base class Descriptor.
//
// Once created the read_struct_specifics_from is invoked to complete the initialization
// of the subclass descriptor
typedef std::unique_ptr<Descriptor> (*descriptor_create_fn)(const struct Descriptor::header_t& hdr,
                                                            BlockArray& ed_blkarr, RuntimeContext& rctx);

class RuntimeContext: public IDManager {
public:
    RuntimeContext();

    void initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& descriptors_map);
    void deinitialize_descriptor_mapping();

public:
    // Given its type returns a function to create such descriptor.
    // If not suitable function is found, return a function to create
    // a default descriptor that has the minimum logic to work
    // (this enables XOZ to be forward compatible)
    descriptor_create_fn descriptor_create_lookup(uint16_t type) const;
    void throw_if_descriptor_mapping_not_initialized() const;

private:
    std::map<uint16_t, descriptor_create_fn> mapping;
    bool initialized;
};
