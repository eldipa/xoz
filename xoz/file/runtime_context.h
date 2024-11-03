#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include "xoz/dsc/descriptor_mapping.h"
#include "xoz/file/id_manager.h"
#include "xoz/file/index.h"
#include "xoz/file/runtime_config.h"

namespace xoz {
class RuntimeContext {
public:
    IDManager idmgr;
    DescriptorMapping dmap;
    Index index;

    const struct runtime_config_t runcfg;

    explicit RuntimeContext(const DescriptorMapping& dmap,
                            const struct runtime_config_t& runcfg = DefaultRuntimeConfig):
            dmap(dmap), index(idmgr), runcfg(runcfg) {}
    explicit RuntimeContext(const std::map<uint16_t, descriptor_create_fn>& descriptors_map,
                            bool override_reserved = false,
                            const struct runtime_config_t& runcfg = DefaultRuntimeConfig):
            dmap(descriptors_map, override_reserved), index(idmgr), runcfg(runcfg) {}
};
}  // namespace xoz
