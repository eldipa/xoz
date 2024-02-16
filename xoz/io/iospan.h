#pragma once

#include <cstdint>
#include <span>

#include "xoz/io/iobase.h"

/*
 * Read/write bytes into/from a span of bytes. This class
 * offers and more ergonomic interface to work with these
 * streams of bytes (see IOBase).
 *
 * This is mostly for testing where the underlying data (span of bytes)
 * is in memory and not meant to be persistent.
 * */
class IOSpan final: public IOBase {
private:
    const std::span<char> dataspan;

public:
    explicit IOSpan(std::span<char> dataspan);
    explicit IOSpan(std::span<uint8_t> dataspan);
    IOSpan(char* data, uint32_t sz);
    IOSpan(uint8_t* data, uint32_t sz);

private:
    /*
     * The given buffer must have enough space to hold max_data_sz bytes The operation
     * will read/write up to max_data_sz bytes but it may less.
     *
     * The count of bytes read/written is returned.
     *
     * */
    uint32_t rw_operation(const bool is_read_op, char* data, const uint32_t data_sz) override final;
};
