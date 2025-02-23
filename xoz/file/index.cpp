#include "xoz/file/index.h"

#include "xoz/err/exceptions.h"
#include "xoz/file/id_manager.h"
#include "xoz/log/format_string.h"

namespace xoz {
Index::Index(const IDManager& idmgr): dset(nullptr), idmgr(idmgr) {}

void Index::init_index(DescriptorSet& dset, std::shared_ptr<IDMappingDescriptor>& idmap) {
    if (this->dset) {
        throw std::runtime_error("The index is already initialized");
    }

    if (!idmap) {
        throw std::runtime_error("The index cannot be initialized because the IDMappingDescriptor is null");
    }

    this->id_by_name = idmap->load();
    for (auto& [name, id]: id_by_name) {
        fail_if_bad_values(name, id, false);
    }

    this->dset = &dset;
}

std::shared_ptr<Descriptor> Index::find(const std::string& name) {
    fail_if_not_initialized();
    if (not id_by_name.contains(name)) {
        throw std::invalid_argument((F() << "No descriptor with name '" << name << "' was found.").str());
    }

    return find(id_by_name[name]);
}

std::shared_ptr<Descriptor> Index::find(uint32_t id) {
    fail_if_not_initialized();
    if (dsc_by_id_cache.contains(id)) {
        return dsc_by_id_cache[id];
    }

    std::shared_ptr<Descriptor> dsc;
    DescriptorSet::bottom_up_for_each_set(*dset, [&dsc, id](DescriptorSet* s, [[maybe_unused]] size_t l) {
        if (not s->contains(id)) {
            return false;
        }

        dsc = s->get(id);
        xoz_assert("Descriptor pointer found null in a set.", dsc);
        return true;
    });

    if (not dsc) {
        throw std::invalid_argument(
                (F() << "Descriptor " << xoz::log::hex(id) << " does not belong to any set.").str());
    }

    dsc_by_id_cache[id] = dsc;
    return dsc;
}

void Index::add_name(const std::string& name, const std::shared_ptr<Descriptor>& dsc, bool override_if_exists) {
    return _add_name(name, dsc, override_if_exists, false);
}

void Index::add_name(const std::string& name, uint32_t id, bool override_if_exists) {
    return _add_name(name, id, override_if_exists, false);
}

void Index::add_temporal_name(const std::string& name, const std::shared_ptr<Descriptor>& dsc,
                              bool override_if_exists) {
    return _add_name(name, dsc, override_if_exists, true);
}

void Index::add_temporal_name(const std::string& name, uint32_t id, bool override_if_exists) {
    return _add_name(name, id, override_if_exists, true);
}

void Index::_add_name(const std::string& name, const std::shared_ptr<Descriptor>& dsc, bool override_if_exists,
                      bool is_temporal_name) {
    fail_if_not_initialized();
    if (!dsc) {
        throw std::runtime_error((F() << "Descriptor is null so we cannot assign it the name '" << name << "'.").str());
    }

    uint32_t id = dsc->id();
    _add_name(name, id, override_if_exists, is_temporal_name);
}

void Index::_add_name(const std::string& name, uint32_t id, bool override_if_exists, bool is_temporal_name) {
    fail_if_not_initialized();
    fail_if_bad_values(name, id, is_temporal_name);

    if (id_by_name.contains(name) and not override_if_exists) {
        uint32_t other_id = id_by_name[name];
        if (other_id != id) {
            throw std::runtime_error((F() << "The name '" << name << "' is already in use by another descriptor ("
                                          << xoz::log::hex(other_id) << ") and cannot be assigned to descriptor "
                                          << xoz::log::hex(id) << ".")
                                             .str());
        }
    }

    id_by_name[name] = id;
}


void Index::delete_name(const std::string& name) {
    fail_if_not_initialized();
    if (not id_by_name.contains(name)) {
        throw std::runtime_error((F() << "The name '" << name << "' was not found.").str());
    }

    id_by_name.erase(name);
}

bool Index::contains(const std::string& name) const {
    fail_if_not_initialized();
    return id_by_name.contains(name);
}

void Index::flush(std::shared_ptr<IDMappingDescriptor>& idmap) { idmap->store(id_by_name); }

void Index::fail_if_bad_values(const std::string& name, uint32_t id, bool is_temporal_name) const {
    if (name.size() > 255) {
        throw std::runtime_error((F() << "The name '" << name << "' for descriptor " << xoz::log::hex(id)
                                      << " is too large (it has a size of " << name.size()
                                      << " greater than the maximum of 255.")
                                         .str());
    }

    if (name.size() == 0) {
        throw std::runtime_error((F() << "Name for the descriptor " << xoz::log::hex(id) << " cannot be empty.").str());
    }

    if (not idmgr.is_registered(id)) {
        throw std::runtime_error((F() << "The descriptor id " << xoz::log::hex(id)
                                      << " is not registered so we cannot assign it the name '" << name << "'.")
                                         .str());
    }

    if (name[0] == TempNamePrefix and not is_temporal_name) {
        throw std::runtime_error((F() << "The name '" << name << "' for descriptor " << xoz::log::hex(id)
                                      << " has the temporal marker '" << name[0] << "' "
                                      << "but it is not accepted in this context.")
                                         .str());
    }

    if (name[0] != TempNamePrefix and is_temporal_name) {
        throw std::runtime_error((F() << "The name '" << name << "' for descriptor " << xoz::log::hex(id)
                                      << " does not have the temporal marker '" << name[0] << "' "
                                      << "but it is expected.")
                                         .str());
    }
}

void Index::fail_if_not_initialized() const {
    if (not this->dset) {
        throw std::runtime_error("The index is not initialized yet.");
    }
}
}  // namespace xoz
