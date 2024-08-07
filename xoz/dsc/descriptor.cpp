#include "xoz/dsc/descriptor.h"

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <utility>

#include "xoz/blk/block_array.h"
#include "xoz/dsc/default.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/dsc/internals.h"
#include "xoz/err/exceptions.h"
#include "xoz/file/runtime_context.h"
#include "xoz/log/format_string.h"
#include "xoz/mem/bits.h"
#include "xoz/mem/inet_checksum.h"


namespace xoz {
/*
 * Check the positions in the io that the (internal) data field begins (before calling descriptor subclass)
 * and ends (after calling the descriptor subclass) and compares the difference with the idata_sz (in bytes).
 *
 * If there is any anomaly, throw an error: InconsistentXOZ (if is_read_op) or WouldEndUpInconsistentXOZ (if not
 * is_read_op)
 * */
void Descriptor::chk_rw_specifics_on_idata(bool is_read_op, IOBase& io, uint32_t idata_begin, uint32_t subclass_end,
                                           uint32_t idata_sz) {
    uint32_t idata_end = idata_begin + idata_sz;  // descriptor truly end
    F errmsg;

    if (idata_begin > subclass_end) {
        errmsg =
                std::move(F() << "The descriptor subclass moved the " << (is_read_op ? "read " : "write ")
                              << "pointer backwards and left it at position " << subclass_end
                              << " that it is before the begin of the data section at position " << idata_begin << ".");
        goto fail;
    }

    if (subclass_end - idata_begin > idata_sz) {
        errmsg = std::move(F() << "The descriptor subclass overflowed the " << (is_read_op ? "read " : "write ")
                               << "pointer by " << subclass_end - idata_begin - idata_sz
                               << " bytes (total available: " << idata_sz << " bytes) "
                               << "and left it at position " << subclass_end
                               << " that it is beyond the end of the data section at position " << idata_end << ".");
        goto fail;
    }

    // This is the only case that may happen.
    if (subclass_end - idata_begin < idata_sz) {
        errmsg = std::move(F() << "The descriptor subclass underflowed the " << (is_read_op ? "read " : "write ")
                               << "pointer and processed " << subclass_end - idata_begin << " bytes (left "
                               << idata_sz - (subclass_end - idata_begin) << " bytes unprocessed of " << idata_sz
                               << " bytes available) "
                               << "and left it at position " << subclass_end
                               << " that it is before the end of the data section at position " << idata_end << ".");
        goto fail;
    }

    return;

fail:
    if (is_read_op) {
        io.seek_rd(idata_end);
        throw InconsistentXOZ(errmsg);
    } else {
        io.seek_wr(idata_end);
        throw WouldEndUpInconsistentXOZ(errmsg);
    }
}

/*
 * Check that what we read/write from/to the io is what the descriptor says that we will read/write
 * based on its own footprint calculation.
 * */
void Descriptor::chk_struct_footprint(bool is_read_op, IOBase& io, uint32_t dsc_begin, uint32_t dsc_end,
                                      const Descriptor* const dsc, bool ex_type_used) {
    uint32_t dsc_sz_based_io = dsc_end - dsc_begin;  // descriptor truly size based on what we read/write
    uint32_t calc_footprint =
            dsc->calc_struct_footprint_size();  // what the descriptor says that it should be read/write

    F errmsg;

    if (dsc_begin > dsc_end) {
        errmsg = std::move(F() << "The descriptor moved the " << (is_read_op ? "read " : "write ")
                               << "pointer backwards and left it at position " << dsc_end
                               << " that it is before the begin at position " << dsc_begin << ".");
        goto fail;
    }

    if (dsc_end % 2 != 0) {
        assert(dsc_begin % 2 == 0);
        errmsg = std::move(F() << "The descriptor moved the " << (is_read_op ? "read " : "write ")
                               << "pointer and left it misaligned at position " << dsc_end
                               << " where the begin of the operation was at an aligned position " << dsc_begin << ".");
        goto fail;
    }

    if (dsc_sz_based_io != calc_footprint) {
        if (ex_type_used and dsc_sz_based_io > calc_footprint and dsc_sz_based_io - calc_footprint == 2 and
            dsc->hdr.type < EXTENDED_TYPE_VAL_THRESHOLD and is_read_op) {
            // ok, this is an exception to the rule:
            //
            //  If during reading (is_read_op) we read an ex_type (ex_type_used) *but* the resulting type
            //  (dsc->hdr.type) is less than the threshold, it means that the descriptor can have a smaller footprint
            //  because the type can be stored without requiring the "extension type".
            //
            //  Hence, it should be expected that the calculated footprint (calc_footprint) is less than the
            //  data actually read (dsc_sz_based_io), in particular, it should 2 bytes off (dsc_sz_based_io -
            //  calc_footprint).

            // no error, false alarm
        } else {
            errmsg = std::move(F() << "Mismatch what the descriptor calculates its footprint (" << calc_footprint
                                   << " bytes) and what actually was " << (is_read_op ? "read " : "written ") << "("
                                   << dsc_sz_based_io << " bytes)");
            goto fail;
        }
    }

    return;

fail:
    if (is_read_op) {
        io.seek_rd(dsc_end);
        throw InconsistentXOZ(errmsg);
    } else {
        io.seek_wr(dsc_end);
        throw WouldEndUpInconsistentXOZ(errmsg);
    }
}

struct Descriptor::header_t Descriptor::load_header_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr,
                                                         bool& ex_type_used, uint32_t* checksum) {
    uint32_t local_checksum = 0;

    uint16_t firstfield = io.read_u16_from_le();

    local_checksum += firstfield;

    bool own_content = read_bitsfield_from_u16<bool>(firstfield, MASK_OWN_CONTENT_FLAG);
    uint8_t lo_isize = read_bitsfield_from_u16<uint8_t>(firstfield, MASK_LO_ISIZE);
    bool has_id = read_bitsfield_from_u16<bool>(firstfield, MASK_HAS_ID_FLAG);
    uint16_t type = read_bitsfield_from_u16<uint16_t>(firstfield, MASK_TYPE);

    uint32_t id = 0;
    uint8_t hi_isize = 0;

    if (has_id) {
        uint32_t idfield = io.read_u32_from_le();

        local_checksum += inet_checksum(idfield);

        hi_isize = read_bitsfield_from_u32<uint8_t>(idfield, MASK_HI_ISIZE);
        id = read_bitsfield_from_u32<uint32_t>(idfield, MASK_ID);
    }

    uint8_t isize = uint8_t((uint8_t(hi_isize << 5) | lo_isize) << 1);  // in bytes

    uint32_t lo_csize = 0, hi_csize = 0;
    if (own_content) {
        uint16_t sizefield = io.read_u16_from_le();

        local_checksum += sizefield;

        bool large = read_bitsfield_from_u16<bool>(sizefield, MASK_LARGE_FLAG);
        lo_csize = read_bitsfield_from_u16<uint32_t>(sizefield, MASK_LO_CSIZE);

        if (large) {
            uint16_t largefield = io.read_u16_from_le();

            local_checksum += largefield;

            hi_csize = read_bitsfield_from_u16<uint32_t>(largefield, MASK_HI_CSIZE);
        }
    }

    uint32_t csize = (hi_csize << 15) | lo_csize;  // in bytes

    Segment segm(cblkarr.blk_sz_order());
    struct Descriptor::header_t hdr = {
            .own_content = own_content, .type = type, .id = 0, .isize = isize, .csize = csize, .segm = segm};

    // ID of zero is not an error, it just means that the descriptor will have a temporal id
    // assigned in runtime.
    if (has_id and id == 0) {
        if (isize >= (32 << 1)) {
            // Ok, the has_id was set to true because the descriptor has a large isize
            // so it was required have the hi_isize bit set and to fill the remaining
            // slot, the id field was set to 0.
            //
            // In this context, the id should be a temporal one and not an error
            assert(hi_isize != 0);
            id = rctx.request_temporal_id();
        } else {
            // No ok. The has_id was set but it was not because the hi_isize was required
            // so it should because the descriptor has a persistent id but such cannot
            // be zero.
            //
            // This is an error
            assert(hi_isize == 0);
            throw InconsistentXOZ(F() << "Descriptor id is zero, detected with partially loaded " << hdr);
        }
    } else if (has_id and id != 0) {
        auto ok = rctx.register_persistent_id(id);  // TODO test
        if (not ok) {
            throw InconsistentXOZ(
                    F() << "Descriptor persistent id already registered, a duplicated descriptor found somewhere else; "
                        << hdr);
        }

    } else if (not has_id) {
        assert(id == 0);
        id = rctx.request_temporal_id();
    }

    assert(id != 0);
    hdr.id = id;

    if (own_content) {
        hdr.segm = Segment::load_struct_from(io, cblkarr.blk_sz_order(), Segment::EndMode::AnyEnd, uint32_t(-1),
                                             &local_checksum);
    }

    /* TODO
    const auto segm_sz = hdr.segm.calc_data_space_size(???);
    if (segm_sz < hdr.csize) {
        throw InconsistentXOZ(F() << "Descriptor claims at least " << hdr.csize << " bytes of content but it has
    allocated only " << segm_sz << ": " << hdr);
    }
    */

    // extended-type (for types that require full 16 bits)
    ex_type_used = false;
    if (type == EXTENDED_TYPE_VAL_THRESHOLD) {
        type = io.read_u16_from_le();

        local_checksum += type;

        hdr.type = type;
        ex_type_used = true;
    }

    if (isize > io.remain_rd()) {
        throw NotEnoughRoom(isize, io.remain_rd(),
                            F() << "No enough room for reading descriptor's internal data of " << hdr);
    }

    uint32_t idata_begin_pos = io.tell_rd();
    uint32_t dsc_end_pos = idata_begin_pos + hdr.isize;

    if (io.remain_rd() < hdr.isize) {
        throw InconsistentXOZ("");  // TODO
    }

    local_checksum += inet_checksum(io, idata_begin_pos, dsc_end_pos);

    if (checksum) {
        *checksum = inet_add(*checksum, inet_to_u16(local_checksum));
    }

    io.seek_rd(idata_begin_pos);
    return hdr;
}

std::unique_ptr<Descriptor> Descriptor::load_struct_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr) {

    uint32_t dsc_begin_pos = io.tell_rd();

    uint32_t checksum = 0;
    bool ex_type_used = false;
    struct Descriptor::header_t hdr = load_header_from(io, rctx, cblkarr, ex_type_used, &checksum);

    descriptor_create_fn fn = rctx.descriptor_create_lookup(hdr.type);
    std::unique_ptr<Descriptor> dsc = fn(hdr, cblkarr, rctx);

    if (!dsc) {
        throw std::runtime_error((F() << "Subclass create for " << hdr << " returned a null pointer").str());
    }

    uint32_t idata_begin_pos = io.tell_rd();
    {
        auto guard = cblkarr.allocator().block_all_alloc_dealloc_guard();  // cppcheck-suppress unreadVariable
        dsc->read_struct_specifics_from(io);
    }
    uint32_t dsc_end_pos = io.tell_rd();

    chk_rw_specifics_on_idata(true, io, idata_begin_pos, dsc_end_pos, hdr.isize);
    chk_struct_footprint(true, io, dsc_begin_pos, dsc_end_pos, dsc.get(), ex_type_used);

    // note: as the check in chk_rw_specifics_on_idata and the following assert
    // we can be 100% sure that load_header_from checksummed all the data field from
    // idata_begin_pos to idata_begin_pos+hdr.isize.
    assert(dsc_end_pos == idata_begin_pos + hdr.isize);
    dsc->checksum = inet_to_u16(checksum);

    return dsc;
}


void Descriptor::write_struct_into(IOBase& io, [[maybe_unused]] RuntimeContext& rctx) {
    uint32_t dsc_begin_pos = io.tell_wr();

    if (hdr.isize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor isize is not multiple of 2 in " << hdr);
    }

    // TODO
    if (is_isize_greater_than_allowed(hdr.isize)) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor isize is larger than allowed " << hdr);
    }

    if (hdr.id == 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor id is zero in " << hdr);
    }

    // TODO test
    if (not hdr.segm.is_empty_space() and not hdr.own_content) {
        throw WouldEndUpInconsistentXOZ(
                F() << "Descriptor does not claim to be owner of content but it has allocated a segment " << hdr.segm
                    << "; " << hdr);
    }

    // TODO test
    if (hdr.own_content and not hdr.segm.has_end_of_segment()) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor claims to be owner of content but its segment " << hdr.segm
                                            << "  has no explicit end; " << hdr);
    }

    // TODO test
    if (hdr.csize and not hdr.own_content) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor claims at least " << hdr.csize
                                            << " bytes of content but it is not an owner; " << hdr);
    }

    if (is_csize_greater_than_allowed(hdr.csize)) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor csize is larger than allowed " << hdr);
    }

    uint32_t checksum = 0;

    /* TODO
    const auto segm_sz = hdr.segm.calc_data_space_size();
    if (segm_sz < hdr.csize) {
        assert(hdr.own_content);
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor claims at least " << hdr.csize << " bytes of content
    but it has allocated only " << segm_sz << ": " << hdr);
    }
    */

    // If the id is persistent we must store it. It may not be persistent but we may require
    // store hi_isize so in that case we still need the idfield (but with an id of 0)
    bool has_id = is_id_persistent(hdr.id) or hdr.isize >= (32 << 1);

    uint16_t firstfield = 0;

    write_bitsfield_into_u16(firstfield, hdr.own_content, MASK_OWN_CONTENT_FLAG);
    write_bitsfield_into_u16(firstfield, assert_u16(hdr.isize >> 1), MASK_LO_ISIZE);
    write_bitsfield_into_u16(firstfield, has_id, MASK_HAS_ID_FLAG);

    if (hdr.type < EXTENDED_TYPE_VAL_THRESHOLD) {
        write_bitsfield_into_u16(firstfield, hdr.type, MASK_TYPE);
    } else {
        write_bitsfield_into_u16(firstfield, EXTENDED_TYPE_VAL_THRESHOLD, MASK_TYPE);
    }

    // Write the first field
    io.write_u16_to_le(firstfield);
    checksum += firstfield;

    // Write the second, if present
    chk_isize_fit_or_fail(has_id, hdr);
    if (has_id) {
        uint32_t idfield = 0;
        bool hi_dsize_msb = hdr.isize >> (1 + 5);  // discard 5 lower bits of isize
        write_bitsfield_into_u32(idfield, hi_dsize_msb, MASK_HI_ISIZE);

        if (is_id_temporal(hdr.id)) {
            // for temporal ids we are not required to have an idfield unless
            // we have to write hi_dsize_msb too.
            // so if we are here, hi_dsize_msb must be 1.
            assert(hi_dsize_msb);
            write_bitsfield_into_u32(idfield, uint32_t(0), MASK_ID);
        } else {
            write_bitsfield_into_u32(idfield, hdr.id, MASK_ID);
        }

        io.write_u32_to_le(idfield);
        checksum += inet_checksum(idfield);
    }


    if (hdr.own_content) {
        uint16_t sizefield = 0;

        // Write the sizefield and optionally the largefield
        if (hdr.csize < (1 << 15)) {
            write_bitsfield_into_u16(sizefield, false, MASK_LARGE_FLAG);
            write_bitsfield_into_u16(sizefield, hdr.csize, MASK_LO_CSIZE);

            io.write_u16_to_le(sizefield);
            checksum += sizefield;
        } else {
            if (hdr.csize >= uint32_t(0x80000000)) {  // TODO test
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor content size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            write_bitsfield_into_u16(sizefield, true, MASK_LARGE_FLAG);
            write_bitsfield_into_u16(sizefield, hdr.csize, MASK_LO_CSIZE);

            uint16_t largefield = 0;
            write_bitsfield_into_u16(largefield, assert_u16(hdr.csize >> 15), MASK_HI_CSIZE);

            io.write_u16_to_le(sizefield);
            io.write_u16_to_le(largefield);
            checksum += sizefield;
            checksum += largefield;
        }


        // Write the segment
        hdr.segm.write_struct_into(io, &checksum);
    }

    // extended-type
    // note: a type of exactly EXTENDED_TYPE_VAL_THRESHOLD is valid and it requires
    // to store it in the ex_type field, hence the '>='
    bool ex_type_used = false;
    if (hdr.type >= EXTENDED_TYPE_VAL_THRESHOLD) {
        io.write_u16_to_le(hdr.type);
        checksum += hdr.type;
        ex_type_used = true;
    }

    if (hdr.isize > io.remain_wr()) {
        throw NotEnoughRoom(hdr.isize, io.remain_wr(),
                            F() << "No enough room for writing descriptor's internal data of " << hdr);
    }

    uint32_t idata_begin_pos = io.tell_wr();
    write_struct_specifics_into(io);
    uint32_t dsc_end_pos = io.tell_wr();

    chk_rw_specifics_on_idata(false, io, idata_begin_pos, dsc_end_pos, hdr.isize);
    chk_struct_footprint(false, io, dsc_begin_pos, dsc_end_pos, this, ex_type_used);
    assert(dsc_end_pos == idata_begin_pos + hdr.isize);

    checksum += inet_checksum(io, idata_begin_pos, dsc_end_pos);
    this->checksum = inet_to_u16(checksum);
}

uint32_t Descriptor::calc_struct_footprint_size() const {
    if (hdr.isize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor isize is not multiple of 2 in " << hdr);
    }

    uint32_t struct_sz = 0;

    // Write the first field
    struct_sz += 2;

    // Write the idfield if present
    bool has_id = is_id_persistent(hdr.id) or hdr.isize >= (32 << 1);
    chk_isize_fit_or_fail(has_id, hdr);
    if (has_id) {
        struct_sz += 4;
    }


    if (hdr.own_content) {
        if (hdr.csize < (1 << 15)) {
            // sizefield
            struct_sz += 2;
        } else {
            if (hdr.csize >= uint32_t(0x80000000)) {  // TODO test
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor content size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            // sizefield and largefield
            struct_sz += 2;
            struct_sz += 2;
        }

        // segment
        struct_sz += hdr.segm.calc_struct_footprint_size();
    }

    if (hdr.type >= EXTENDED_TYPE_VAL_THRESHOLD) {
        // ex_type field
        struct_sz += 2;
    }

    struct_sz += hdr.isize;  // hdr.isize is in bytes too

    return struct_sz;
}

std::ostream& operator<<(std::ostream& out, const struct Descriptor::header_t& hdr) {
    PrintTo(hdr, &out);
    return out;
}

void PrintTo(const struct Descriptor::header_t& hdr, std::ostream* out) {
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << "descriptor {"
           << "id: " << xoz::log::hex(hdr.id) << ", type: " << hdr.type << ", isize: " << uint32_t(hdr.isize);

    if (hdr.own_content) {
        (*out) << ", csize: " << hdr.csize << ", owns: " << hdr.segm.calc_data_space_size() << "}"
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

void Descriptor::chk_isize_fit_or_fail(bool has_id, const struct Descriptor::header_t& hdr) {
    if (has_id) {
        if (hdr.isize >= (64 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor isize is larger than the maximum representable ("
                                                << (64 << 1) << ") in " << hdr);
        }
    } else {
        if (hdr.isize >= (32 << 1)) {
            throw WouldEndUpInconsistentXOZ(F() << "Descriptor isize is larger than the maximum representable ("
                                                << (32 << 1) << ") in " << hdr);
        }
    }
}

uint32_t Descriptor::calc_content_space_size() const { return hdr.own_content ? hdr.segm.calc_data_space_size() : 0; }

void Descriptor::destroy() {
    if (hdr.own_content) {
        cblkarr.allocator().dealloc(hdr.segm);
    }
}

void Descriptor::notify_descriptor_changed() {
    if (owner_raw_ptr != nullptr) {
        owner_raw_ptr->mark_as_modified(this->id());
    }
}
}  // namespace xoz
