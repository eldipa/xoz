#pragma once

class IDManager {
public:
    IDManager() { reset(); }

    uint32_t request_temporal_id() { return next_temporal_id++; }

    // This makes sense only very special cases or for testing.
    void reset(uint32_t init = 0x80000000) {
        assert(init >= 0x80000000);
        next_temporal_id = init;
    }

private:
    uint32_t next_temporal_id;
};
