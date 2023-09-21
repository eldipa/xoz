#include "xoz/dsc/descriptor.h"

#include <cstdint>

#include "xoz/dsc/internals.h"
#include "xoz/mem/bits.h"
#include "xoz/segm/iosegment.h"

struct Descriptor::header_t Descriptor::read_struct_header(IOBase& iobase) {
    uint16_t firstfield = iobase.read_u16_from_le();

    bool is_obj = read_bitsfield_from_u16<bool>(firstfield, MASK_IS_OBJ_FLAG);
    bool has_id = read_bitsfield_from_u16<bool>(firstfield, MASK_HAS_ID_FLAG);

    uint8_t lo_dsize = read_bitsfield_from_u16<uint8_t>(firstfield, MASK_LO_DSIZE);

    uint16_t type = read_bitsfield_from_u16<uint16_t>(firstfield, MASK_TYPE);

    uint32_t obj_id = 0;
    uint8_t hi_dsize = 0;
    if (is_obj or has_id) {
        uint32_t idfield = iobase.read_u32_from_le();

        hi_dsize = read_bitsfield_from_u32<uint8_t>(idfield, MASK_HI_DSIZE);
        obj_id = read_bitsfield_from_u32<uint32_t>(idfield, MASK_OBJ_ID);
    }

    if (is_obj) {
        type = uint16_t(uint16_t(has_id) << 9) | type;
    }

    uint8_t dsize = uint8_t((uint8_t(hi_dsize << 5) | lo_dsize) << 1);

    uint32_t lo_size = 0, hi_size = 0;
    if (is_obj) {
        uint16_t sizefield = iobase.read_u16_from_le();

        bool large = read_bitsfield_from_u16<bool>(sizefield, MASK_LARGE_FLAG);
        lo_size = read_bitsfield_from_u16<uint32_t>(sizefield, MASK_OBJ_LO_SIZE);

        if (large) {
            uint16_t largefield = iobase.read_u16_from_le();
            hi_size = read_bitsfield_from_u16<uint32_t>(largefield, MASK_OBJ_HI_SIZE);
        }
    }

    uint32_t size = (hi_size << 16) | lo_size;

    struct Descriptor::header_t ret = {.is_obj = is_obj, .type = type, .obj_id = obj_id, .dsize = dsize, .size = size};

    return ret;
}


void Descriptor::write_struct_header(IOBase& iobase, const struct Descriptor::header_t& hdr) {
    assert(hdr.dsize % 2 == 0);

    bool has_id = false;

    uint16_t firstfield = 0;
    uint32_t idfield = 0;

    assert(hdr.dsize % 2 == 0);

    write_bitsfield_into_u16(firstfield, hdr.is_obj, MASK_IS_OBJ_FLAG);
    write_bitsfield_into_u16(firstfield, (hdr.dsize >> 1), MASK_LO_DSIZE);
    write_bitsfield_into_u16(firstfield, hdr.type, MASK_TYPE);


    if (hdr.is_obj) {
        has_id = true;
        assert(hdr.type <= 1024);  // we have 1 more bit on top of the 9 bits for the type

        bool type_msb = hdr.type >> 9;                                     // discard 9 lower bits
        write_bitsfield_into_u16(firstfield, type_msb, MASK_HAS_ID_FLAG);  // 1 type's MSB as has_id

        write_bitsfield_into_u32(idfield, hdr.obj_id, MASK_OBJ_ID);

    } else {
        // non-object descriptor

        assert(hdr.type < 512);  // we only have 9 bits for non-object descriptors' types

        has_id = hdr.obj_id != 0;  // we may or may not have an object id
        write_bitsfield_into_u16(firstfield, has_id, MASK_HAS_ID_FLAG);

        if (has_id) {
            write_bitsfield_into_u32(idfield, hdr.obj_id, MASK_OBJ_ID);
        }
    }

    // Write the first field
    iobase.write_u16_to_le(firstfield);

    // Write the second, if present
    if (has_id) {
        assert(hdr.dsize <= (64 << 1));
        bool hi_dsize_msb = hdr.dsize >> (1 + 5);  // discard 5 lower bits of dsize
        write_bitsfield_into_u32(idfield, hi_dsize_msb, MASK_HI_DSIZE);

        iobase.write_u32_to_le(idfield);
    } else {
        assert(hdr.dsize <= (32 << 1));
    }


    if (hdr.is_obj) {
        uint16_t sizefield = 0;

        if (hdr.size < (1 << 15)) {
            write_bitsfield_into_u16(sizefield, false, MASK_LO_DSIZE);
            write_bitsfield_into_u16(sizefield, hdr.size, MASK_OBJ_LO_SIZE);

            iobase.write_u16_to_le(sizefield);
        } else {
            assert(hdr.size < uint32_t(1) << 31);

            write_bitsfield_into_u16(sizefield, true, MASK_LO_DSIZE);
            write_bitsfield_into_u16(sizefield, hdr.size, MASK_OBJ_LO_SIZE);

            uint16_t largefield = 0;
            write_bitsfield_into_u16(largefield, hdr.size >> 16, MASK_OBJ_HI_SIZE);

            iobase.write_u16_to_le(sizefield);
            iobase.write_u16_to_le(largefield);
        }
    }
}