#pragma once

#include <set>

#include "xoz/err/exceptions.h"

class IDManager {
public:
    IDManager() { reset(); }

    uint32_t request_temporal_id() { return next_temporal_id++; }
    uint32_t request_persistent_id() {
        uint32_t id = 1;
        if (persistent_ids.size() > 0) {
            id = (*persistent_ids.rbegin()) + 1;
        }

        register_persistent_id(id);
        return id;
    }

    // This makes sense only very special cases or for testing.
    void reset(uint32_t init = 0x80000000) {
        assert(init >= 0x80000000);
        next_temporal_id = init;
    }

    bool register_persistent_id(uint32_t id) {
        if (is_temporal(id)) {
            throw std::runtime_error("Temporal ids cannot be registered.");
        }

        auto [_, ok] = persistent_ids.insert(id);
        return ok;
    }

    static bool is_temporal(uint32_t id) { return id & 0x80000000; }

    static bool is_persistent(uint32_t id) { return not is_temporal(id); }

private:
    uint32_t next_temporal_id;

    std::set<uint32_t> persistent_ids;
};
