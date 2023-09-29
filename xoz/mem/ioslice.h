#pragma once

#include <cstdint>
#include <span>

#include "xoz/mem/iobase.h"

/*
 * Wrap an IOBase object to restrict the operation (read/write) and its available size
 * (a slice of the wrapped io).
 *
 * The wrapper will modify the wrapped object and it will assume that the wrapped object
 * will *not* be modified outside while this wrapper exists.
 *
 * So the caller must not modify the wrapped object and it must assume that during and after
 * the existence of the IOX instance, the wrapped io will change.
 * */
class IOSlice final: public IOBase {
private:
    IOBase& io;
    const bool is_read_mode;

public:
    /*
     * Create a slice of the given io for read-only (if is_read_mode is true) or for
     * write only (if is_read_mode is false).
     *
     * The slice will cover sz bytes from the current (rd/wr) position of the given io
     * truncated to the current remain_rd/remain_wr of io.
     * */
    explicit IOSlice(IOBase& io, bool is_read_mode, uint32_t sz);

private:
    uint32_t rw_operation(const bool is_read_op, char* data, const uint32_t data_sz) override final;
};
