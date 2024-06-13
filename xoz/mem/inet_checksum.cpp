#include "xoz/mem/inet_checksum.h"

#include "xoz/io/iobase.h"

uint32_t inet_checksum(IOBase& io, const uint32_t begin, const uint32_t end) {
    assert(begin <= end);

    const uint32_t sz = end - begin;
    assert(sz % 2 == 0);

    [[maybe_unused]] uint16_t word_cnt = assert_u16(sz >> 1);

    io.seek_rd(begin);

    // NOTE: we could work with uint64_t and then fold to uint16_t back and get some speed
    // we could skip some fold_inet_checksum calls too
    uint32_t checksum = 0;
    uint8_t buf[64];
    for (unsigned batch = 0; batch < sz / sizeof(buf); ++batch) {
        io.readall(reinterpret_cast<char*>(buf), sizeof(buf));
        for (uint32_t i = 0; i < sizeof(buf); i += 2) {
            checksum += uint16_t((buf[i + 1] << 8) | buf[i]);
        }
        // We may be checksuming a lot of data, so it is safe to do
        // a fold so we don't overflow uint32_t
        checksum = fold_inet_checksum(checksum);
    }

    const uint32_t remain = sz % sizeof(buf);
    io.readall(reinterpret_cast<char*>(buf), remain);
    for (uint32_t i = 0; i < remain; i += 2) {
        checksum += u16_from_le(*reinterpret_cast<uint16_t*>(&buf[i]));
    }
    checksum = fold_inet_checksum(checksum);

    assert(io.tell_rd() == end);
    return checksum;
}
