#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/descriptor_set.h"

namespace xoz {
class IDManager;

class Index {
public:
    explicit Index(const IDManager& idmgr);
    void init_index(DescriptorSet& root, const std::map<std::string, uint32_t>& id_by_name);

    /*
     * find() searches for the descriptor in the entire xoz file
     * given an id or a name
     *
     * If no descriptor is found (either the name is not mapped to an id
     * or the id does not belong to any descriptor), this method throws.
     * */
    std::shared_ptr<Descriptor> find(const std::string& name);
    std::shared_ptr<Descriptor> find(uint32_t id);

    template <typename T>
    std::shared_ptr<T> find(uint32_t id, bool ret_null = false) {
        auto ptr = std::dynamic_pointer_cast<T>(this->find(id));
        if (!ptr and not ret_null) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }

        return ptr;
    }

    template <typename T>
    std::shared_ptr<T> find(const std::string& name, bool ret_null = false) {
        auto ptr = std::dynamic_pointer_cast<T>(this->find(name));
        if (!ptr and not ret_null) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }

        return ptr;
    }

    void add_name(const std::string& name, const std::shared_ptr<Descriptor>& dsc, bool override_if_exists = false);
    void add_name(const std::string& name, uint32_t id, bool override_if_exists = false);
    void delete_name(const std::string& name);
    bool contains(const std::string& name) const;

private:
    void fail_if_bad_values(const std::string& name, uint32_t id) const;
    void fail_if_not_initialized() const;

private:
    DescriptorSet* root;
    std::map<std::string, uint32_t> id_by_name;
    const IDManager& idmgr;

    std::map<uint32_t, std::shared_ptr<Descriptor>> dsc_by_id_cache;
};
}  // namespace xoz
