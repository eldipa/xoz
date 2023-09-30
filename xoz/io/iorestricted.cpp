#include "xoz/io/iorestricted.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

IORestricted::IORestricted(IOBase& io, bool is_read_mode, uint32_t sz):
        IOBase(std::min(sz, (is_read_mode ? io.remain_rd() : io.remain_wr()))), io(io), is_read_mode(is_read_mode) {}

uint32_t IORestricted::rw_operation(const bool is_read_op, char* data, const uint32_t data_sz) {
    if (is_read_op != this->is_read_mode) {
        throw "";
    }

    // Restrict the available size for rw based on (*this) offsets and sizes
    // and then pass it to the subio
    uint32_t avail_sz = is_read_op ? remain_rd() : remain_wr();
    uint32_t rw_total_sz = std::min(data_sz, avail_sz);

    return io.rw_operation(is_read_op, data, rw_total_sz);
}

ReadOnly::ReadOnly(IOBase& io, uint32_t sz): IORestricted(io, true, sz) {}

WriteOnly::WriteOnly(IOBase& io, uint32_t sz): IORestricted(io, false, sz) {}
