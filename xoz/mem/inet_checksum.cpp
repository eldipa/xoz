#include "xoz/mem/inet_checksum.h"

#include "xoz/io/iobase.h"

uint32_t inet_checksum(IOBase& io, const uint32_t begin, const uint32_t end) {
    assert(begin <= end);

    const uint32_t sz = end - begin;
    assert(sz % 2 == 0);

    [[maybe_unused]] const uint16_t word_cnt = assert_u16(sz >> 1);

    io.seek_rd(begin);

    // NOTE: we could work with uint64_t and then fold to uint16_t back and get some speed
    uint32_t checksum = 0;
    uint8_t buf[64];
    for (unsigned batch = 0; batch < sz / sizeof(buf); ++batch) {
        io.readall((char*)buf, sizeof(buf));
        checksum += inet_checksum((uint16_t*)buf, sizeof(buf) >> 1);
    }

    io.readall((char*)buf, sz % sizeof(buf));
    checksum += inet_checksum((uint16_t*)buf, (sz % sizeof(buf)) >> 1);

    assert(io.tell_rd() == end);
    return checksum;
}
