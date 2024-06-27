#include "xoz/repo/runtime_context.h"

#include "xoz/blk/block_array.h"
#include "xoz/dsc/default.h"
#include "xoz/dsc/dset_holder.h"
#include "xoz/err/exceptions.h"
#include "xoz/log/format_string.h"

RuntimeContext::RuntimeContext(): initialized(false) {}

void RuntimeContext::initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& descriptors_map) {
    if (initialized) {
        throw std::runtime_error("Descriptor mapping is already initialized.");
    }

    for (auto [type, fn]: descriptors_map) {
        if (!fn) {
            throw std::runtime_error((F() << "Descriptor mapping for type " << type << " is null.").str());
        }

        if (type < TYPE_RESERVED_THRESHOLD) {
            throw std::runtime_error((F() << "Descriptor mapping for type " << type
                                          << " is reserved for internal use and cannot be overridden.")
                                             .str());
        }
    }

    mapping = descriptors_map;
    initialized = true;
}

void RuntimeContext::deinitialize_descriptor_mapping() {
    // Note: we don't check if the mapping was initialized or not,
    // we just do the clearing and leave the mapping in a known state
    mapping.clear();
    initialized = false;
}


descriptor_create_fn RuntimeContext::descriptor_create_lookup(uint16_t type) const {
    // Is the descriptor one of the defined by xoz?
    switch (type) {
        case 0:
            throw std::runtime_error("Descriptor type 0 is reserved.");
        case 1:
            return DescriptorSetHolder::create;
        case 2:
            throw std::runtime_error("Descriptor type 2 is reserved.");
        case 3:
            throw std::runtime_error("Descriptor type 3 is reserved.");
    }
    static_assert(TYPE_RESERVED_THRESHOLD == 4);

    // Is the descriptor defined by the user?
    auto it = mapping.find(type);
    if (it == mapping.end()) {
        // No definition for the given type, fallback to a default generic implementation
        return DefaultDescriptor::create;
    }

    return it->second;
}


void RuntimeContext::throw_if_descriptor_mapping_not_initialized() const {
    if (not initialized) {
        throw std::runtime_error("Descriptor mapping is not initialized.");
    }
}
