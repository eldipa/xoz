#include "xoz/dsc/descriptor.h"

#include <cassert>
#include <cstdint>

#include "xoz/dsc/default.h"
#include "xoz/dsc/internals.h"
#include "xoz/mem/bits.h"
#include "xoz/segm/iosegment.h"

namespace {
std::map<uint16_t, descriptor_create_fn> _priv_non_obj_descriptors;
std::map<uint16_t, descriptor_create_fn> _priv_obj_descriptors;

bool _priv_descriptor_mapping_initialized = false;

// Given if the descriptor is or not an object descriptor and give its type
// return a function to create such descriptors.
// If not suitable function is found, return a function to create
// a default descriptor that has the minimum logic to work
// (this enables XOZ to be forward compatible)
descriptor_create_fn descriptor_create_lookup(bool is_obj, uint16_t type) {
    if (is_obj) {
        auto it = _priv_obj_descriptors.find(type);
        if (it == _priv_obj_descriptors.end()) {
            return DefaultDescriptor::create;
        }

        return it->second;
    } else {
        auto it = _priv_non_obj_descriptors.find(type);
        if (it == _priv_non_obj_descriptors.end()) {
            return DefaultDescriptor::create;
        }

        return it->second;
    }

    assert(false);
}

void throw_if_descriptor_mapping_not_initialized() {
    if (not _priv_descriptor_mapping_initialized) {
        throw "";
    }
}
}  // namespace

void initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& non_obj_descriptors,
                                   const std::map<uint16_t, descriptor_create_fn>& obj_descriptors) {

    if (_priv_descriptor_mapping_initialized) {
        throw "";
    }

    _priv_non_obj_descriptors = non_obj_descriptors;
    _priv_obj_descriptors = obj_descriptors;

    _priv_descriptor_mapping_initialized = true;
}

void deinitialize_descriptor_mapping() {

    // Note: we don't check if the mapping was initialized or not,
    // we just do the clearing and leave the mapping in a known state

    _priv_non_obj_descriptors.clear();
    _priv_obj_descriptors.clear();

    _priv_descriptor_mapping_initialized = false;
}


std::unique_ptr<Descriptor> Descriptor::load_struct_from(IOBase& io) {
    throw_if_descriptor_mapping_not_initialized();
    uint16_t firstfield = io.read_u16_from_le();

    bool is_obj = read_bitsfield_from_u16<bool>(firstfield, MASK_IS_OBJ_FLAG);
    bool has_id = read_bitsfield_from_u16<bool>(firstfield, MASK_HAS_ID_FLAG);

    uint8_t lo_dsize = read_bitsfield_from_u16<uint8_t>(firstfield, MASK_LO_DSIZE);

    uint16_t type = read_bitsfield_from_u16<uint16_t>(firstfield, MASK_TYPE);

    uint32_t obj_id = 0;
    uint8_t hi_dsize = 0;
    if (is_obj or has_id) {
        uint32_t idfield = io.read_u32_from_le();

        hi_dsize = read_bitsfield_from_u32<uint8_t>(idfield, MASK_HI_DSIZE);
        obj_id = read_bitsfield_from_u32<uint32_t>(idfield, MASK_OBJ_ID);
    }

    if (is_obj) {
        type = uint16_t(uint16_t(has_id) << 9) | type;
    }

    uint8_t dsize = uint8_t((uint8_t(hi_dsize << 5) | lo_dsize) << 1);

    uint32_t lo_size = 0, hi_size = 0;
    if (is_obj) {
        uint16_t sizefield = io.read_u16_from_le();

        bool large = read_bitsfield_from_u16<bool>(sizefield, MASK_LARGE_FLAG);
        lo_size = read_bitsfield_from_u16<uint32_t>(sizefield, MASK_OBJ_LO_SIZE);

        if (large) {
            uint16_t largefield = io.read_u16_from_le();
            hi_size = read_bitsfield_from_u16<uint32_t>(largefield, MASK_OBJ_HI_SIZE);
        }
    }

    uint32_t size = (hi_size << 16) | lo_size;

    Segment segm;
    if (is_obj) {
        segm = Segment::load_struct_from(io);
    }

    struct Descriptor::header_t hdr = {
            .is_obj = is_obj, .type = type, .obj_id = obj_id, .dsize = dsize, .size = size, .segm = segm};

    descriptor_create_fn fn = descriptor_create_lookup(is_obj, type);
    std::unique_ptr<Descriptor> dsc = fn(hdr);

    if (!dsc) {
        throw "";
    }

    dsc->read_struct_specifics_from(io);
    return dsc;
}


void Descriptor::write_struct_into(IOBase& io) {
    throw_if_descriptor_mapping_not_initialized();
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
    io.write_u16_to_le(firstfield);

    // Write the second, if present
    if (has_id) {
        assert(hdr.dsize <= (64 << 1));
        bool hi_dsize_msb = hdr.dsize >> (1 + 5);  // discard 5 lower bits of dsize
        write_bitsfield_into_u32(idfield, hi_dsize_msb, MASK_HI_DSIZE);

        io.write_u32_to_le(idfield);
    } else {
        assert(hdr.dsize <= (32 << 1));
    }


    if (hdr.is_obj) {
        uint16_t sizefield = 0;

        if (hdr.size < (1 << 15)) {
            write_bitsfield_into_u16(sizefield, false, MASK_LO_DSIZE);
            write_bitsfield_into_u16(sizefield, hdr.size, MASK_OBJ_LO_SIZE);

            io.write_u16_to_le(sizefield);
        } else {
            assert(hdr.size < uint32_t(1) << 31);

            write_bitsfield_into_u16(sizefield, true, MASK_LO_DSIZE);
            write_bitsfield_into_u16(sizefield, hdr.size, MASK_OBJ_LO_SIZE);

            uint16_t largefield = 0;
            write_bitsfield_into_u16(largefield, hdr.size >> 16, MASK_OBJ_HI_SIZE);

            io.write_u16_to_le(sizefield);
            io.write_u16_to_le(largefield);
        }


        hdr.segm.write_struct_into(io);
    }

    write_struct_specifics_into(io);
}
