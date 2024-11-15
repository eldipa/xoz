#include "xoz/dsc/descriptor_mapping.h"

#include "xoz/dsc/descriptor_set.h"
#include "xoz/dsc/id_mapping.h"
#include "xoz/dsc/opaque.h"
#include "xoz/dsc/private.h"
#include "xoz/err/exceptions.h"
#include "xoz/log/format_string.h"

namespace xoz {
DescriptorMapping::DescriptorMapping(const std::map<uint16_t, descriptor_create_fn>& descriptors_map,
                                     bool override_reserved) {
    for (auto [type, fn]: descriptors_map) {
        if (!fn) {
            throw std::runtime_error((F() << "Descriptor mapping for type " << type << " is null.").str());
        }

        if ((RESERVED_CORE_MIN_TYPE <= type and type <= RESERVED_CORE_MAX_TYPE) or
            (RESERVED_METADATA_MIN_TYPE <= type and type <= RESERVED_METADATA_MAX_TYPE) or type == RESERVED_LAST_TYPE or
            type == RESERVED_ZERO_TYPE) {
            if (not override_reserved) {
                throw std::runtime_error((F() << "Descriptor mapping for type " << type
                                              << " is reserved for internal use and cannot be overridden.")
                                                 .str());
            }
        }
    }

    mapping = descriptors_map;
}

descriptor_create_fn DescriptorMapping::descriptor_create_lookup(uint16_t type) const {

    // Is the descriptor defined by the user?
    auto it = mapping.find(type);
    if (it == mapping.end()) {
        // Is the descriptor one of the defined by xoz?
        if (RESERVED_CORE_MIN_TYPE <= type and type <= RESERVED_CORE_MAX_TYPE) {
            switch (type) {
                case RESERVED_ZERO_TYPE:
                    throw std::runtime_error((F() << "Descriptor mapping for type " << type
                                                  << " is reserved and should not be present or used.")
                                                     .str());
                case DescriptorSet::TYPE:
                    return DescriptorSet::create;
                default:
                    return PrivateDescriptor::create;
            }
        }

        if (RESERVED_METADATA_MIN_TYPE <= type and type <= RESERVED_METADATA_MAX_TYPE) {
            switch (type) {
                case IDMappingDescriptor::TYPE:
                    return IDMappingDescriptor::create;
                default:
                    return PrivateDescriptor::create;
            }
        }

        // No definition for the given type, fallback to a default generic implementation
        if (DSET_SUBCLASS_MIN_TYPE <= type and type <= DSET_SUBCLASS_MAX_TYPE) {
            return DescriptorSet::create;
        } else {
            return OpaqueDescriptor::create;
        }
    }

    return it->second;
}
}  // namespace xoz
