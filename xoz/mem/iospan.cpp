#include "xoz/mem/iospan.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

IOSpan::IOSpan(std::span<char> dataspan): IOBase((uint32_t)dataspan.size()), dataspan(dataspan) {
    if (dataspan.size() > uint32_t(-1)) {
        throw "";
    }
}

uint32_t IOSpan::rw_operation(const bool is_read_op, char* data, const uint32_t data_sz) {
    uint32_t avail_sz = is_read_op ? remain_rd() : remain_wr();
    uint32_t rw_total_sz = std::min(data_sz, avail_sz);

    if (is_read_op) {
        memcpy(data, dataspan.data() + rd, rw_total_sz);
        rd += rw_total_sz;
    } else {
        memcpy(dataspan.data() + wr, data, rw_total_sz);
        wr += rw_total_sz;
    }

    return rw_total_sz;
}
