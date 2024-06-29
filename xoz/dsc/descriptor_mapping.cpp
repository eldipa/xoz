#include "xoz/dsc/descriptor_mapping.h"

#include "xoz/dsc/default.h"
#include "xoz/dsc/dset_holder.h"
#include "xoz/err/exceptions.h"
#include "xoz/log/format_string.h"

DescriptorMapping::DescriptorMapping(const std::map<uint16_t, descriptor_create_fn>& descriptors_map,
                                     bool override_reserved) {
    for (auto [type, fn]: descriptors_map) {
        if (!fn) {
            throw std::runtime_error((F() << "Descriptor mapping for type " << type << " is null.").str());
        }

        if (not override_reserved and type < TYPE_RESERVED_THRESHOLD) {
            throw std::runtime_error((F() << "Descriptor mapping for type " << type
                                          << " is reserved for internal use and cannot be overridden.")
                                             .str());
        }
    }

    mapping = descriptors_map;
}

descriptor_create_fn DescriptorMapping::descriptor_create_lookup(uint16_t type) const {

    // Is the descriptor defined by the user?
    auto it = mapping.find(type);
    if (it == mapping.end()) {
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

        // No definition for the given type, fallback to a default generic implementation
        return DefaultDescriptor::create;
    }

    return it->second;
}
