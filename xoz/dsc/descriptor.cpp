#include "xoz/dsc/descriptor.h"

#include <cassert>
#include <cstdint>
#include <utility>

#include "xoz/dsc/default.h"
#include "xoz/dsc/internals.h"
#include "xoz/exceptions.h"
#include "xoz/io/iorestricted.h"
#include "xoz/mem/bits.h"
#include "xoz/repo/id_manager.h"

namespace {
std::map<uint16_t, descriptor_create_fn> _priv_descriptors_map;

bool _priv_descriptor_mapping_initialized = false;

// Given its type returns a function to create such descriptor.
// If not suitable function is found, return a function to create
// a default descriptor that has the minimum logic to work
// (this enables XOZ to be forward compatible)
descriptor_create_fn descriptor_create_lookup(uint16_t type) {
    auto it = _priv_descriptors_map.find(type);
    if (it == _priv_descriptors_map.end()) {
        return DefaultDescriptor::create;
    }

    return it->second;

    assert(false);
}

void throw_if_descriptor_mapping_not_initialized() {
    if (not _priv_descriptor_mapping_initialized) {
        throw std::runtime_error("Descriptor mapping is not initialized.");
    }
}
}  // namespace

void initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& descriptors_map) {

    if (_priv_descriptor_mapping_initialized) {
        throw std::runtime_error("Descriptor mapping is already initialized.");
    }

    for (auto [type, fn]: descriptors_map) {
        if (!fn) {
            throw std::runtime_error((F() << "Descriptor mapping for type " << type << " is null.").str());
        }
    }

    _priv_descriptors_map = descriptors_map;

    _priv_descriptor_mapping_initialized = true;
}

void deinitialize_descriptor_mapping() {

    // Note: we don't check if the mapping was initialized or not,
    // we just do the clearing and leave the mapping in a known state
    _priv_descriptors_map.clear();

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

std::unique_ptr<Descriptor> Descriptor::load_struct_from(IOBase& io, IDManager& idmgr) {
    throw_if_descriptor_mapping_not_initialized();
    uint16_t firstfield = io.read_u16_from_le();

    bool own_edata = read_bitsfield_from_u16<bool>(firstfield, MASK_OWN_EDATA_FLAG);
    uint8_t lo_dsize = read_bitsfield_from_u16<uint8_t>(firstfield, MASK_LO_DSIZE);
    bool has_id = read_bitsfield_from_u16<bool>(firstfield, MASK_HAS_ID_FLAG);
    uint16_t type = read_bitsfield_from_u16<uint16_t>(firstfield, MASK_TYPE);

    uint32_t id = 0;
    uint8_t hi_dsize = 0;

    if (has_id) {
        uint32_t idfield = io.read_u32_from_le();

        hi_dsize = read_bitsfield_from_u32<uint8_t>(idfield, MASK_HI_DSIZE);
        id = read_bitsfield_from_u32<uint32_t>(idfield, MASK_ID);
    }

    uint8_t dsize = uint8_t((uint8_t(hi_dsize << 5) | lo_dsize) << 1);  // in bytes

    uint32_t lo_esize = 0, hi_esize = 0;
    if (own_edata) {
        uint16_t sizefield = io.read_u16_from_le();

        bool large = read_bitsfield_from_u16<bool>(sizefield, MASK_LARGE_FLAG);
        lo_esize = read_bitsfield_from_u16<uint32_t>(sizefield, MASK_LO_ESIZE);

        if (large) {
            uint16_t largefield = io.read_u16_from_le();
            hi_esize = read_bitsfield_from_u16<uint32_t>(largefield, MASK_HI_ESIZE);
        }
    }

    uint32_t esize = (hi_esize << 15) | lo_esize;  // in bytes

    Segment segm;
    struct Descriptor::header_t hdr = {
            .own_edata = own_edata, .type = type, .id = 0, .dsize = dsize, .esize = esize, .segm = segm};

    // ID of zero is not an error, it just means that the descriptor will have a temporal id
    // assigned in runtime.
    if (has_id and id == 0) {
        if (dsize >= (32 << 1)) {
            // Ok, the has_id was set to true because the descriptor has a large dsize
            // so it was required have the hi_dsize bit set and to fill the remaining
            // slot, the id field was set to 0.
            //
            // In this context, the id should be a temporal one and not an error
            assert(hi_dsize != 0);
            id = idmgr.request_temporal_id();
        } else {
            // No ok. The has_id was set but it was not because the hi_dsize was required
            // so it should because the descriptor has a persistent id but such cannot
            // be zero.
            //
            // This is an error
            assert(hi_dsize == 0);
            throw InconsistentXOZ(F() << "Descriptor id is zero, detected with partially loaded " << hdr);
        }
    } else if (not has_id) {
        assert(id == 0);
        id = idmgr.request_temporal_id();
    }

    assert(id != 0);
    hdr.id = id;

    if (own_edata) {
        hdr.segm = Segment::load_struct_from(io);
    }

    /* TODO
    const auto segm_sz = hdr.segm.calc_data_space_size(???);
    if (segm_sz < hdr.esize) {
        throw InconsistentXOZ(F() << "Descriptor claims at least " << hdr.esize << " bytes of external data but it has
    allocated only " << segm_sz << ": " << hdr);
    }
    */

    // alternative-type (for types that require full 16 bits)
    if (type == ALTERNATIVE_TYPE_VAL) {
        type = io.read_u16_from_le();
    }

    if (dsize > io.remain_rd()) {
        throw NotEnoughRoom(dsize, io.remain_rd(), F() << "No enough room for reading descriptor's data of " << hdr);
    }

    descriptor_create_fn fn = descriptor_create_lookup(type);
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

    // check that we can represent the decriptor type with 9 bits
    if (hdr.type >= 512) {
        throw WouldEndUpInconsistentXOZ(F()
                                        << "Descriptor type is larger than the maximum representable (512) in " << hdr);
    }

    if (hdr.id == 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor id is zero in " << hdr);
    }

    // TODO test
    if (not hdr.segm.is_empty_space() and not hdr.own_edata) {
        throw WouldEndUpInconsistentXOZ(
                F() << "Descriptor does not claim to be owner of external data but it has allocated a segment "
                    << hdr.segm << "; " << hdr);
    }

    // TODO test
    if (hdr.esize and not hdr.own_edata) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor claims at least " << hdr.esize
                                            << " bytes of external data but it is not an owner; " << hdr);
    }

    /* TODO
    const auto segm_sz = hdr.segm.calc_data_space_size(????);
    if (segm_sz < hdr.esize) {
        assert(hdr.own_edata);
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor claims at least " << hdr.esize << " bytes of external data
    but it has allocated only " << segm_sz << ": " << hdr);
    }
    */

    // If the id is persistent we must store it. It may not be persistent but we may require
    // store hi_dsize so in that case we still need the idfield (but with an id of 0)
    bool has_id = is_id_persistent(hdr.id) or hdr.dsize >= (32 << 1);

    uint16_t firstfield = 0;

    write_bitsfield_into_u16(firstfield, hdr.own_edata, MASK_OWN_EDATA_FLAG);
    write_bitsfield_into_u16(firstfield, (hdr.dsize >> 1), MASK_LO_DSIZE);
    write_bitsfield_into_u16(firstfield, has_id, MASK_HAS_ID_FLAG);

    if (hdr.type < 0x1ff) {
        write_bitsfield_into_u16(firstfield, hdr.type, MASK_TYPE);
    } else {
        write_bitsfield_into_u16(firstfield, 0x1ff, MASK_TYPE);
    }

    // Write the first field
    io.write_u16_to_le(firstfield);

    // Write the second, if present
    chk_dsize_fit_or_fail(has_id, hdr);
    if (has_id) {
        uint32_t idfield = 0;
        bool hi_dsize_msb = hdr.dsize >> (1 + 5);  // discard 5 lower bits of dsize
        write_bitsfield_into_u32(idfield, hi_dsize_msb, MASK_HI_DSIZE);

        if (is_id_temporal(hdr.id)) {
            // for temporal ids we are not required to have an idfield unless
            // we have to write hi_dsize_msb too.
            // so if we are here, hi_dsize_msb must be 1.
            assert(hi_dsize_msb);
            write_bitsfield_into_u32(idfield, 0, MASK_ID);
        } else {
            write_bitsfield_into_u32(idfield, hdr.id, MASK_ID);
        }

        io.write_u32_to_le(idfield);
    }


    if (hdr.own_edata) {
        uint16_t sizefield = 0;

        // Write the sizefield and optionally the largefield
        if (hdr.esize < (1 << 15)) {
            write_bitsfield_into_u16(sizefield, false, MASK_LARGE_FLAG);
            write_bitsfield_into_u16(sizefield, hdr.esize, MASK_LO_ESIZE);

            io.write_u16_to_le(sizefield);
        } else {
            if (hdr.esize >= uint32_t(0x80000000)) {  // TODO test
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor external size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            write_bitsfield_into_u16(sizefield, true, MASK_LARGE_FLAG);
            write_bitsfield_into_u16(sizefield, hdr.esize, MASK_LO_ESIZE);

            uint16_t largefield = 0;
            write_bitsfield_into_u16(largefield, hdr.esize >> 15, MASK_HI_ESIZE);

            io.write_u16_to_le(sizefield);
            io.write_u16_to_le(largefield);
        }


        // Write the segment
        hdr.segm.write_struct_into(io);
    }

    // alternative type
    // note: a type of exactly ALTERNATIVE_TYPE_VAL is valid and it requires
    // to store it in the alt_type field, hence the '>='
    if (hdr.type >= ALTERNATIVE_TYPE_VAL) {
        io.write_u16_to_le(hdr.type);
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
    bool has_id = is_id_persistent(hdr.id) or hdr.dsize >= (32 << 1);
    chk_dsize_fit_or_fail(has_id, hdr);
    if (has_id) {
        struct_sz += 4;
    }


    if (hdr.own_edata) {
        if (hdr.esize < (1 << 15)) {
            // sizefield
            struct_sz += 2;
        } else {
            if (hdr.esize >= uint32_t(0x80000000)) {  // TODO test
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor external size is larger than the maximum representable ("
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

    (*out) << "descriptor {"
           << "id: " << hdr.id << ", type: " << hdr.type << ", dsize: " << (unsigned)hdr.dsize;

    if (hdr.own_edata) {
        (*out) << ", esize: " << hdr.esize << ", owns: " << hdr.segm.calc_data_space_size(9 /*TODO*/) << "}"
               << " " << hdr.segm;
    } else {
        (*out) << "}";
    }

    out->flags(ioflags);
}

std::ostream& operator<<(std::ostream& out, const Descriptor& dsc) {
    PrintTo(dsc, &out);
    return out;
}

void PrintTo(const Descriptor& dsc, std::ostream* out) { PrintTo(dsc.hdr, out); }

void Descriptor::chk_dsize_fit_or_fail(bool has_id, const struct Descriptor::header_t& hdr) {
    if (has_id) {
        if (hdr.dsize >= (64 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is larger than the maximum representable ("
                                                << (64 << 1) << ") in " << hdr);
        }
    } else {
        if (hdr.dsize >= (32 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor dsize is larger than the maximum representable ("
                                                << (32 << 1) << ") in " << hdr);
        }
    }
}
