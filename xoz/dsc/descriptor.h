#pragma once
#include <cstdint>

#include "xoz/mem/iobase.h"

class Descriptor {

private:
    struct header_t {
        bool is_obj;
        uint16_t type;

        uint32_t obj_id;

        uint8_t dsize;  // in bytes
        uint32_t size;  // in bytes
    };

    struct header_t read_struct_header(IOBase& iobase);
    void write_struct_header(IOBase& iobase, const struct header_t& hdr);
};
