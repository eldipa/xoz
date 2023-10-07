#include "xoz/dsc/descriptor.h"

#include <cassert>
#include <cstdint>
#include <utility>

#include "xoz/dsc/default.h"
#include "xoz/dsc/internals.h"
#include "xoz/exceptions.h"
#include "xoz/io/iorestricted.h"
#include "xoz/mem/bits.h"

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
        throw std::runtime_error("Descriptor mapping is not initialized.");
    }
}
}  // namespace

void initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& non_obj_descriptors,
                                   const std::map<uint16_t, descriptor_create_fn>& obj_descriptors) {

    if (_priv_descriptor_mapping_initialized) {
        throw std::runtime_error("Descriptor mapping is already initialized.");
    }

    for (auto [type, fn]: non_obj_descriptors) {
        if (!fn) {
            throw std::runtime_error(
                    (F() << "Descriptor mapping for non-object descriptor type " << type << " is null.").str());
        }
    }

    for (auto [type, fn]: obj_descriptors) {
        if (!fn) {
            throw std::runtime_error(
                    (F() << "Descriptor mapping for object descriptor type " << type << " is null.").str());
        }
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


namespace {
void chk_rw_specifics(bool is_read_op, IOBase& io, uint32_t data_begin, uint32_t subclass_end, uint32_t data_sz) {
    uint32_t data_end = data_begin + data_sz;  // descriptor truly end
    F errmsg;

    // Case 1 and 2 should never happen if the called used ReadOnly and WriteOnly wrappers
    // to restrict the size of the io.
    if (data_begin > subclass_end) {
        errmsg = std::move(F() << "The descriptor subclass moved the " << (is_read_op ? "read " : "write ")
                               << "pointer backwards and left it at position " << subclass_end
                               << " that it is before the begin of the data section at position " << data_begin << ".");
        goto fail;
    }

    if (subclass_end - data_begin > data_sz) {
        errmsg = std::move(F() << "The descriptor subclass overflowed the " << (is_read_op ? "read " : "write ")
                               << "pointer by " << subclass_end - data_begin - data_sz
                               << " bytes (total available: " << data_sz << " bytes) "
                               << "and left it at position " << subclass_end
                               << " that it is beyond the end of the data section at position " << data_end << ".");
        goto fail;
    }

    // This is the only case that may happen.
    if (subclass_end - data_begin < data_sz) {
        errmsg = std::move(F() << "The descriptor subclass underflowed the " << (is_read_op ? "read " : "write ")
                               << "pointer and processed " << subclass_end - data_begin << " bytes (left "
                               << data_sz - (subclass_end - data_begin) << " bytes unprocessed of " << data_sz
                               << " bytes available) "
                               << "and left it at position " << subclass_end
                               << " that it is before the end of the data section at position " << data_end << ".");
        goto fail;
    }

    return;

fail:
    if (is_read_op) {
        io.seek_rd(data_end);
        throw InconsistentXOZ(errmsg);
    } else {
        io.seek_wr(data_end);
        throw WouldEndUpInconsistentXOZ(errmsg);
    }
}
}  // namespace

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

    uint32_t size = (hi_size << 15) | lo_size;

    Segment segm;
    struct Descriptor::header_t hdr = {
            .is_obj = is_obj, .type = type, .obj_id = obj_id, .dsize = dsize, .size = size, .segm = segm};

    if (is_obj) {
        if (obj_id == 0) {
            throw InconsistentXOZ(F() << "Object id of an object-descriptor is zero, detected with partially loaded "
                                      << hdr);
        }
        hdr.segm = Segment::load_struct_from(io);
    }

    if (dsize > io.remain_rd()) {
        throw NotEnoughRoom(dsize, io.remain_rd(), F() << "No enough room for reading descriptor's data of " << hdr);
    }

    descriptor_create_fn fn = descriptor_create_lookup(is_obj, type);
    std::unique_ptr<Descriptor> dsc = fn(hdr);

    if (!dsc) {
        throw std::runtime_error((F() << "Subclass create for " << hdr << " returned a null pointer").str());
    }

    uint32_t data_begin = io.tell_rd();
    dsc->read_struct_specifics_from(ReadOnly(io, dsize));
    uint32_t subclass_end = io.tell_rd();

    chk_rw_specifics(true, io, data_begin, subclass_end, hdr.dsize);
    return dsc;
}


void Descriptor::write_struct_into(IOBase& io) {
    throw_if_descriptor_mapping_not_initialized();
    if (hdr.dsize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is not multiple of 2 in " << hdr);
    }

    bool has_id = false;

    uint16_t firstfield = 0;
    uint32_t idfield = 0;

    write_bitsfield_into_u16(firstfield, hdr.is_obj, MASK_IS_OBJ_FLAG);
    write_bitsfield_into_u16(firstfield, (hdr.dsize >> 1), MASK_LO_DSIZE);
    write_bitsfield_into_u16(firstfield, hdr.type, MASK_TYPE);


    if (hdr.is_obj) {
        has_id = true;

        // we have 1 more bit on top of the 9 bits for the type
        if (hdr.type >= 1024) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor type is larger than the maximum representable (1024) in "
                                                << hdr);
        }

        if (hdr.obj_id == 0) {
            throw WouldEndUpInconsistentXOZ(F() << "Object id for object-descriptor is zero in " << hdr);
        }

        bool type_msb = hdr.type >> 9;                                     // discard 9 lower bits
        write_bitsfield_into_u16(firstfield, type_msb, MASK_HAS_ID_FLAG);  // 1 type's MSB as has_id

        write_bitsfield_into_u32(idfield, hdr.obj_id, MASK_OBJ_ID);

    } else {
        // non-object descriptor

        // we only have 9 bits for non-object descriptors' types
        if (hdr.type >= 512) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor type is larger than the maximum representable (512) in "
                                                << hdr);
        }

        has_id = hdr.obj_id != 0 or hdr.dsize >= (32 << 1);  // we may or may not have an object id
        write_bitsfield_into_u16(firstfield, has_id, MASK_HAS_ID_FLAG);

        if (has_id) {
            write_bitsfield_into_u32(idfield, hdr.obj_id, MASK_OBJ_ID);
        }
    }

    // Write the first field
    io.write_u16_to_le(firstfield);

    // Write the second, if present
    if (has_id) {
        if (hdr.dsize >= (64 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is larger than the maximum representable ("
                                                << (64 << 1) << ") in " << hdr);
        }

        bool hi_dsize_msb = hdr.dsize >> (1 + 5);  // discard 5 lower bits of dsize
        write_bitsfield_into_u32(idfield, hi_dsize_msb, MASK_HI_DSIZE);

        io.write_u32_to_le(idfield);
    } else {
        if (hdr.dsize >= (32 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is larger than the maximum representable ("
                                                << (32 << 1) << ") in " << hdr);
        }
    }


    if (hdr.is_obj) {
        uint16_t sizefield = 0;

        if (hdr.size < (1 << 15)) {
            write_bitsfield_into_u16(sizefield, false, MASK_LARGE_FLAG);
            write_bitsfield_into_u16(sizefield, hdr.size, MASK_OBJ_LO_SIZE);

            io.write_u16_to_le(sizefield);
        } else {
            if (hdr.size >= uint32_t(0x80000000)) {
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor object size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            write_bitsfield_into_u16(sizefield, true, MASK_LARGE_FLAG);
            write_bitsfield_into_u16(sizefield, hdr.size, MASK_OBJ_LO_SIZE);

            uint16_t largefield = 0;
            write_bitsfield_into_u16(largefield, hdr.size >> 15, MASK_OBJ_HI_SIZE);

            io.write_u16_to_le(sizefield);
            io.write_u16_to_le(largefield);
        }


        hdr.segm.write_struct_into(io);
    }

    if (hdr.dsize > io.remain_wr()) {
        throw NotEnoughRoom(hdr.dsize, io.remain_wr(),
                            F() << "No enough room for writing descriptor's data of " << hdr);
    }

    uint32_t data_begin = io.tell_wr();
    write_struct_specifics_into(WriteOnly(io, hdr.dsize));
    uint32_t subclass_end = io.tell_wr();

    chk_rw_specifics(false, io, data_begin, subclass_end, hdr.dsize);
}

uint32_t Descriptor::calc_struct_footprint_size() const {
    if (hdr.dsize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is not multiple of 2 in " << hdr);
    }

    uint32_t struct_sz = 0;

    // Write the first field
    struct_sz += 2;

    // Write the idfield if present
    bool has_id = hdr.is_obj or (hdr.obj_id != 0) or hdr.dsize >= (32 << 1);  // NOLINT
    if (has_id) {
        if (hdr.dsize >= (64 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is larger than the maximum representable ("
                                                << (64 << 1) << ") in " << hdr);
        }
        struct_sz += 4;
    } else {
        if (hdr.dsize >= (32 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is larger than the maximum representable ("
                                                << (32 << 1) << ") in " << hdr);
        }
    }


    if (hdr.is_obj) {
        if (hdr.size < (1 << 15)) {
            // sizefield
            struct_sz += 2;
        } else {
            if (hdr.size >= uint32_t(0x80000000)) {
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor object size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            // sizefield and largefield
            struct_sz += 2;
            struct_sz += 2;
        }


        // segment
        struct_sz += hdr.segm.calc_struct_footprint_size();
    }

    struct_sz += hdr.dsize;  // hdr.dsize is in bytes too

    return struct_sz;
}

std::ostream& operator<<(std::ostream& out, const struct Descriptor::header_t& hdr) {
    PrintTo(hdr, &out);
    return out;
}

void PrintTo(const struct Descriptor::header_t& hdr, std::ostream* out) {
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << (hdr.is_obj ? "object " : "non-object ") << "descriptor {"
           << "obj-id: " << hdr.obj_id << ", type: " << hdr.type << ", dsize: " << (unsigned)hdr.dsize;

    if (hdr.is_obj) {
        (*out) << ", size: " << hdr.size << "}"
               << " " << hdr.segm;
    } else {
        (*out) << "}";
    }

    out->flags(ioflags);
}
