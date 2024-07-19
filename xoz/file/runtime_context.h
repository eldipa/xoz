#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include "xoz/dsc/descriptor_mapping.h"
#include "xoz/file/id_manager.h"

class RuntimeContext: public IDManager, public DescriptorMapping {
public:
    explicit RuntimeContext(const DescriptorMapping& dmap): IDManager(), DescriptorMapping(dmap) {}
    explicit RuntimeContext(const std::map<uint16_t, descriptor_create_fn>& descriptors_map,
                            bool override_reserved = false):
            IDManager(), DescriptorMapping(descriptors_map, override_reserved) {}
};
