#include "xoz/repo/runtime_context.h"

#include "xoz/blk/block_array.h"
#include "xoz/dsc/default.h"
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
    auto it = mapping.find(type);
    if (it == mapping.end()) {
        return DefaultDescriptor::create;
    }

    return it->second;
}


void RuntimeContext::throw_if_descriptor_mapping_not_initialized() const {
    if (not initialized) {
        throw std::runtime_error("Descriptor mapping is not initialized.");
    }
}
