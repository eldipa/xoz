#include "xoz/io/iospan.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "xoz/mem/bits.h"

IOSpan::IOSpan(std::span<char> dataspan): IOBase(assert_u32(dataspan.size())), dataspan(dataspan) {
    if (dataspan.size() > uint32_t(-1)) {
        throw "";  // TODO
    }
}

// TODO can we ensure that sizeof(char) == sizeof(uint8_t)  somehow? This char/uint8_t mixture
// comes from the upper layers using uint8_t and the C-file level using char.
IOSpan::IOSpan(std::span<uint8_t> dataspan):
        IOSpan(std::span<char>(reinterpret_cast<char*>(dataspan.data()), dataspan.size())) {}

IOSpan::IOSpan(char* data, uint32_t sz): IOSpan(std::span<char>(data, sz)) {}

IOSpan::IOSpan(uint8_t* data, uint32_t sz): IOSpan(std::span<char>(reinterpret_cast<char*>(data), sz)) {}

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
