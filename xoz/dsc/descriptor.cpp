#include "xoz/dsc/descriptor.h"

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <utility>

#include "xoz/blk/block_array.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/dsc/internals.h"
#include "xoz/dsc/opaque.h"
#include "xoz/err/exceptions.h"
#include "xoz/file/runtime_context.h"
#include "xoz/log/format_string.h"
#include "xoz/mem/asserts.h"
#include "xoz/mem/inet_checksum.h"
#include "xoz/mem/integer_ops.h"

namespace {
const uint32_t RESIZE_CONTENT_MEM_COPY_THRESHOLD_SZ = 1 << 20;  // 1 MB
}

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

/*
 * Check that we have a DescriptorSet or one of its subclasses when the hdr.type
 * suggests that or we have something that is *not* a set when the hdr.type says so.
 * */
void Descriptor::chk_dset_type(bool is_read_op, const Descriptor* const dsc, const struct Descriptor::header_t& hdr,
                               const RuntimeContext& rctx) {

    const bool should_be_dset = (DescriptorSet::TYPE == hdr.type) or (rctx.dmap.DSET_SUBCLASS_MIN_TYPE <= hdr.type and
                                                                      hdr.type <= rctx.dmap.DSET_SUBCLASS_MAX_TYPE);
    const bool is_descriptor_set = dsc->is_descriptor_set();

    F errmsg;

    if (should_be_dset and not is_descriptor_set) {
        errmsg = std::move(
                F() << "Subclass create for " << hdr
                    << " returned a descriptor that is neither a DescriptorSet nor a subclass but such was expected.");
        goto fail;
    }

    if (not should_be_dset and is_descriptor_set) {
        errmsg = std::move(
                F()
                << "Subclass create for " << hdr
                << " returned a descriptor that is either a DescriptorSet or a subclass but such was not expected.");
        goto fail;
    }

    return;

fail:
    if (is_read_op) {
        throw InconsistentXOZ(errmsg);
    } else {
        throw WouldEndUpInconsistentXOZ(errmsg);
    }
}

struct Descriptor::header_t Descriptor::load_header_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr,
                                                         bool& ex_type_used, uint32_t* checksum) {
    uint32_t local_checksum = 0;

    // Make the io read-only during the execution of this method
    [[maybe_unused]] auto guard = io.auto_restore_limits();
    io.limit_to_read_only();

    uint16_t firstfield = io.read_u16_from_le();

    local_checksum += firstfield;

    bool own_content = assert_read_bits_from_u16(bool, firstfield, MASK_OWN_CONTENT_FLAG);
    uint8_t lo_isize = assert_read_bits_from_u16(uint8_t, firstfield, MASK_LO_ISIZE);
    bool has_id = assert_read_bits_from_u16(bool, firstfield, MASK_HAS_ID_FLAG);
    uint16_t type = assert_read_bits_from_u16(uint16_t, firstfield, MASK_TYPE);

    uint32_t id = 0;
    uint8_t hi_isize = 0;

    if (has_id) {
        uint32_t idfield = io.read_u32_from_le();

        local_checksum += inet_checksum(idfield);

        hi_isize = assert_read_bits_from_u32(uint8_t, idfield, MASK_HI_ISIZE);
        id = assert_read_bits_from_u32(uint32_t, idfield, MASK_ID);
    }

    uint8_t isize = uint8_t((uint8_t(hi_isize << 5) | lo_isize) << 1);  // in bytes

    uint32_t lo_csize = 0, hi_csize = 0;
    if (own_content) {
        uint16_t sizefield = io.read_u16_from_le();

        local_checksum += sizefield;

        bool large = assert_read_bits_from_u16(bool, sizefield, MASK_LARGE_FLAG);
        lo_csize = assert_read_bits_from_u16(uint32_t, sizefield, MASK_LO_CSIZE);

        if (large) {
            uint16_t largefield = io.read_u16_from_le();

            local_checksum += largefield;

            hi_csize = assert_read_bits_from_u16(uint32_t, largefield, MASK_HI_CSIZE);
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
            id = rctx.idmgr.request_temporal_id();
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
        auto ok = rctx.idmgr.register_persistent_id(id);  // TODO test
        if (not ok) {
            throw InconsistentXOZ(F() << "Descriptor persistent id " << id
                                      << " already registered, a duplicated descriptor found somewhere else; " << hdr);
        }

    } else if (not has_id) {
        assert(id == 0);
        id = rctx.idmgr.request_temporal_id();
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

    // checksum descriptor subclass specific fields including future_idata
    local_checksum += inet_checksum(io, idata_begin_pos, dsc_end_pos);

    if (checksum) {
        *checksum = inet_add(*checksum, inet_to_u16(local_checksum));
    }

    io.seek_rd(idata_begin_pos);
    return hdr;
}

std::unique_ptr<Descriptor> Descriptor::load_struct_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr) {

    // Make the io read-only during the execution of this method
    [[maybe_unused]] auto guard = io.auto_restore_limits();
    io.limit_to_read_only();

    uint32_t dsc_begin_pos = io.tell_rd();

    uint32_t checksum = 0;
    bool ex_type_used = false;
    struct Descriptor::header_t hdr = load_header_from(io, rctx, cblkarr, ex_type_used, &checksum);

    descriptor_create_fn fn = rctx.dmap.descriptor_create_lookup(hdr.type);
    std::unique_ptr<Descriptor> dsc = fn(hdr, cblkarr, rctx);

    if (!dsc) {
        throw std::runtime_error((F() << "Subclass create for " << hdr << " returned a null pointer").str());
    }

    chk_dset_type(true, dsc.get(), hdr, rctx);
    dsc->checksum = inet_to_u16(checksum);

    uint32_t idata_begin_pos = io.tell_rd();
    {
        [[maybe_unused]] auto alloc_guard = cblkarr.allocator().block_all_alloc_dealloc_guard();
        [[maybe_unused]] auto limit_guard = io.auto_restore_limits();
        io.limit_rd(idata_begin_pos, hdr.isize);

        dsc->read_struct_specifics_from(io);
        dsc->read_future_idata(io);

        xoz_assert("load future idata odd size", dsc->future_idata_size() % 2 == 0);
    }
    uint32_t dsc_end_pos = io.tell_rd();

    chk_rw_specifics_on_idata(true, io, idata_begin_pos, dsc_end_pos, hdr.isize);
    chk_struct_footprint(true, io, dsc_begin_pos, dsc_end_pos, dsc.get(), ex_type_used);

    // note: as the check in chk_rw_specifics_on_idata and the following assert
    // we can be 100% sure that load_header_from checksummed all the data field from
    // idata_begin_pos to idata_begin_pos+hdr.isize.
    assert(dsc_end_pos == idata_begin_pos + hdr.isize);

    dsc->update_sizes_of_header(true);

    return dsc;
}


void Descriptor::write_struct_into(IOBase& io, [[maybe_unused]] RuntimeContext& rctx) {
    uint32_t dsc_begin_pos = io.tell_wr();

    if (hdr.isize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor isize is not multiple of 2 in " << hdr);
    }

    // TODO
    if (not does_hdr_isize_fit(hdr.isize)) {
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

    if (not does_hdr_csize_fit(hdr.csize)) {
        throw WouldEndUpInconsistentXOZ(F() << "Descriptor csize is larger than allowed " << hdr);
    }

    chk_dset_type(false, this, hdr, rctx);

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

    assert_write_bits_into_u16(firstfield, hdr.own_content, MASK_OWN_CONTENT_FLAG);
    assert_write_bits_into_u16(firstfield, assert_u16(hdr.isize >> 1), MASK_LO_ISIZE);
    assert_write_bits_into_u16(firstfield, has_id, MASK_HAS_ID_FLAG);

    if (hdr.type < EXTENDED_TYPE_VAL_THRESHOLD) {
        assert_write_bits_into_u16(firstfield, hdr.type, MASK_TYPE);
    } else {
        assert_write_bits_into_u16(firstfield, EXTENDED_TYPE_VAL_THRESHOLD, MASK_TYPE);
    }

    // Write the first field
    io.write_u16_to_le(firstfield);
    checksum += firstfield;

    // Write the second, if present
    chk_hdr_isize_fit_or_fail(has_id, hdr);
    if (has_id) {
        uint32_t idfield = 0;
        bool hi_dsize_msb = hdr.isize >> (1 + 5);  // discard 5 lower bits of isize
        assert_write_bits_into_u32(idfield, hi_dsize_msb, MASK_HI_ISIZE);

        if (is_id_temporal(hdr.id)) {
            // for temporal ids we are not required to have an idfield unless
            // we have to write hi_dsize_msb too.
            // so if we are here, hi_dsize_msb must be 1.
            assert(hi_dsize_msb);
            assert_write_bits_into_u32(idfield, uint32_t(0), MASK_ID);
        } else {
            assert_write_bits_into_u32(idfield, hdr.id, MASK_ID);
        }

        io.write_u32_to_le(idfield);
        checksum += inet_checksum(idfield);
    }


    if (hdr.own_content) {
        uint16_t sizefield = 0;

        // Write the sizefield and optionally the largefield
        if (hdr.csize < (1 << 15)) {
            assert_write_bits_into_u16(sizefield, false, MASK_LARGE_FLAG);
            assert_write_bits_into_u16(sizefield, hdr.csize, MASK_LO_CSIZE);

            io.write_u16_to_le(sizefield);
            checksum += sizefield;
        } else {
            if (hdr.csize >= uint32_t(0x80000000)) {  // TODO test
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor content size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            assert_write_bits_into_u16(sizefield, true, MASK_LARGE_FLAG);
            assert_write_bits_into_u16(sizefield, hdr.csize, MASK_LO_CSIZE);

            uint16_t largefield = 0;
            assert_write_bits_into_u16(largefield, assert_u16(hdr.csize >> 15), MASK_HI_CSIZE);

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
    {
        [[maybe_unused]] auto limit_guard = io.auto_restore_limits();
        io.limit_wr(idata_begin_pos, hdr.isize);

        write_struct_specifics_into(io);
        write_future_idata(io);
    }
    uint32_t dsc_end_pos = io.tell_wr();

    chk_rw_specifics_on_idata(false, io, idata_begin_pos, dsc_end_pos, hdr.isize);
    chk_struct_footprint(false, io, dsc_begin_pos, dsc_end_pos, this, ex_type_used);
    assert(dsc_end_pos == idata_begin_pos + hdr.isize);

    // checksum descriptor subclass specific fields including future_idata
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
    chk_hdr_isize_fit_or_fail(has_id, hdr);
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

void Descriptor::chk_hdr_isize_fit_or_fail(bool has_id, const struct Descriptor::header_t& hdr) {
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

bool Descriptor::is_descriptor_set() const { return cast<const DescriptorSet>(true) != nullptr; }

void Descriptor::read_future_idata(IOBase& io) {
    future_idata.clear();
    io.readall(future_idata);
}

void Descriptor::write_future_idata(IOBase& io) const { io.writeall(future_idata); }

uint8_t Descriptor::future_idata_size() const { return assert_u8(future_idata.size()); }

void Descriptor::update_header() {
    bool own_content = update_content_segment(hdr.segm);

    hdr.own_content = own_content;
    if (not own_content) {
        hdr.segm = Segment::EmptySegment(cblkarr.blk_sz_order());
    }
    hdr.segm.add_end_of_segment();

    // Order is important: update segment, then update sizes
    update_sizes_of_header(false);
}

void Descriptor::update_sizes_of_header(bool called_from_load) {
    uint64_t present_isize = assert_u8_sub_nonneg(hdr.isize, future_idata_size());
    uint64_t present_csize = assert_u32_sub_nonneg(hdr.csize, future_content_size);

    update_sizes(present_isize, present_csize);

    // If update_sizes_of_header is being called from the loading of the descriptor,
    // this is the first call and the only moment where we can know how much content
    // belongs to the current version of the subclass descriptor and how much
    // to the 'future' version.
    if (called_from_load) {
        // We can deduce future_content_size but not future_idata_size. The reason is that
        // the subclass can add fields changing the future_idata_size at any time
        // however it cannot change the content because allocation is disabled during
        // the loading of the descriptor (hopefully)
        future_content_size = assert_u32_sub_nonneg(hdr.csize, assert_u32(present_csize));
    }

    // Sanity checks
    if (not is_u64_add_ok(present_isize, future_idata_size())) {
        throw WouldEndUpInconsistentXOZ(F()
                                        << "Updated isize for present version overflows with isize for future version; "
                                        << "they are respectively " << present_isize << " and " << future_idata_size());
    }

    if (not is_u64_add_ok(present_csize, future_content_size)) {
        throw WouldEndUpInconsistentXOZ(F()
                                        << "Updated csize for present version overflows with csize for future version; "
                                        << "they are respectively " << present_csize << " and " << future_content_size);
    }

    if (present_isize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Updated isize for present version (" << present_isize << ") "
                                            << "is an odd number (it must be even).");
    }

    uint64_t hdr_isize = assert_u64_add_nowrap(present_isize, future_idata_size());
    uint64_t hdr_csize = assert_u64_add_nowrap(present_csize, future_content_size);

    if (not does_hdr_isize_fit(hdr_isize)) {
        throw WouldEndUpInconsistentXOZ(F() << "Updated isize for present version (" << present_isize << ") "
                                            << "plus isize for future version (" << future_idata_size() << ") "
                                            << "does not fit in the header");
    }

    if (not does_hdr_csize_fit(hdr_csize)) {
        throw WouldEndUpInconsistentXOZ(F() << "Updated csize for present version (" << present_csize << ") "
                                            << "plus csize for future version (" << future_content_size << ") "
                                            << "does not fit in the header");
    }

    xoz_assert("odd hdr isize", hdr_isize % 2 == 0);

    hdr.isize = assert_u8(hdr_isize);
    hdr.csize = assert_u32(hdr_csize);
}

bool Descriptor::update_content_segment([[maybe_unused]] Segment& segm) { return hdr.own_content; }

void Descriptor::resize_content(uint32_t content_new_sz) {
    // No previous content and nothing to grow, then skip (no change)
    if (not hdr.own_content and content_new_sz == 0) {
        xoz_assert("invariant", future_content_size == 0);
        xoz_assert("invariant", hdr.csize == 0);
        return;
    }

    if (not does_present_csize_fit(content_new_sz)) {
        throw WouldEndUpInconsistentXOZ(F() << "The new content size (" << content_new_sz << ") "
                                            << "plus the size from the future version (" << future_content_size << ") "
                                            << "does not fit in the header.");
    }

    // Caller wants some space for the (new) content
    if (not hdr.own_content) {
        xoz_assert("invariant", future_content_size == 0);
        xoz_assert("invariant", hdr.csize == 0);

        hdr.segm = cblkarr.allocator().alloc(content_new_sz);
        hdr.segm.add_end_of_segment();
        hdr.own_content = true;

        xoz_assert("allocated less than requested", hdr.segm.calc_data_space_size() >= content_new_sz);

        // Save the caller's content_new_sz, not the real size of the segment.
        // This reflects correctly what the descriptor owns, the rest (padding)
        // is for "performance" reasons of the allocator.
        // We need to be *very* strict in the value of csize because we will use
        // it to know if there is future content or not and we *don't* want
        // to treat padding as future content (hence future_content_size = 0).
        hdr.csize = content_new_sz;
        return;
    }

    // We own some content but the caller does not want it
    // and we don't have any future data to preserve so we dealloc
    if (content_new_sz == 0 and future_content_size == 0) {
        xoz_assert("invariant", hdr.own_content);

        cblkarr.allocator().dealloc(hdr.segm);
        hdr.segm.remove_inline_data();
        hdr.segm.clear();
        hdr.own_content = false;

        hdr.csize = 0;
        future_content_size = 0;
        return;
    }

    uint32_t csize_new = content_new_sz + future_content_size;
    if (hdr.csize < csize_new) {
        // The content is expanding.
        //
        // Perform the realloc
        cblkarr.allocator().realloc(hdr.segm, csize_new);

        // Copy from the io content the future content at the end of the io
        auto content_io = get_allocated_content_io();
        content_io.seek_rd(assert_u32_sub_nonneg(hdr.csize, future_content_size), IOBase::Seekdir::beg);
        content_io.seek_wr(future_content_size, IOBase::Seekdir::end);
        content_io.copy_into_self(future_content_size);

    } else if (hdr.csize > csize_new) {
        // The content is shrinking, we need to copy outside the future content,
        // do the realloc (shrink) and copy the future back.
        //
        // We can copy the future content into memory iff is small enough,
        // otherwise we go to disk.
        if (future_content_size < RESIZE_CONTENT_MEM_COPY_THRESHOLD_SZ) {
            // The future_content size is small enough, copy it into memory
            std::vector<char> future_data;
            future_data.reserve(future_content_size);
            {
                auto content_io = get_allocated_content_io();
                content_io.seek_rd(future_content_size, IOBase::Seekdir::end);
                content_io.readall(future_data);
            }

            // Perform the realloc
            cblkarr.allocator().realloc(hdr.segm, csize_new);

            // Copy back the future data
            {
                auto content_io = get_allocated_content_io();  // hdr.segm changed, get a new io
                content_io.seek_wr(future_content_size, IOBase::Seekdir::end);
                content_io.writeall(future_data);
            }

        } else {
            // The future_content size is too large, copy it into disk
            auto future_sg = cblkarr.allocator().alloc(future_content_size);
            auto future_io = IOSegment(cblkarr, future_sg);
            {
                auto content_io = get_allocated_content_io();
                content_io.seek_rd(future_content_size, IOBase::Seekdir::end);
                content_io.copy_into(future_io, future_content_size);
            }

            // Perform the realloc
            cblkarr.allocator().realloc(hdr.segm, csize_new);

            // Copy back the future data
            {
                auto content_io = get_allocated_content_io();  // hdr.segm changed, get a new io
                content_io.seek_wr(future_content_size, IOBase::Seekdir::end);
                future_io.seek_rd(0);
                future_io.copy_into(content_io, future_content_size);
            }
        }
    } /* else { neither shrink nor expand, leave it as is } */
    hdr.segm.add_end_of_segment();

    xoz_assert("allocated less than requested", hdr.segm.calc_data_space_size() >= csize_new);
    hdr.csize = csize_new;
}

IOSegment Descriptor::get_allocated_content_io() {
    if (not hdr.own_content) {
        // Descriptors without a content don't fail on get_content_io and instead
        // return an empty io.
        // This is to simplify the case where the descriptor *has* a non-empty segment
        // but it is full of future data so the descriptor "does not have" content.
        auto sg = Segment::EmptySegment(cblkarr.blk_sz_order());
        auto io = IOSegment(cblkarr, sg);

        io.limit_rd(0, 0);
        io.limit_wr(0, 0);
        return io;
    }

    return IOSegment(cblkarr, hdr.segm);
}

IOSegment Descriptor::get_content_io() {
    // Hide from the caller the future_content
    //
    // Note: if get_content_io() is called from read_struct_specifics_from,
    // by that time future_content_size is not set yet and it will default to 0
    auto present_csize = assert_u32_sub_nonneg(hdr.csize, future_content_size);

    auto io = get_allocated_content_io();

    io.limit_rd(0, present_csize);
    io.limit_wr(0, present_csize);
    return io;
}

bool Descriptor::does_present_isize_fit(uint64_t present_isize) const {
    if (not is_u64_add_ok(present_isize, future_idata_size())) {
        return false;
    }

    uint64_t hdr_isize = assert_u64_add_nowrap(present_isize, future_idata_size());

    return does_hdr_isize_fit(hdr_isize) and hdr_isize % 2 == 0;
}

bool Descriptor::does_present_csize_fit(uint64_t present_csize) const {
    if (not is_u64_add_ok(present_csize, future_content_size)) {
        return false;
    }

    uint64_t hdr_csize = assert_u64_add_nowrap(present_csize, future_content_size);

    return does_hdr_csize_fit(hdr_csize);
}

struct Descriptor::header_t Descriptor::create_header(const uint16_t type, const BlockArray& cblkarr) {
    struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = type,
            .id = 0x0,
            .isize = 0,
            .csize = 0,
            .segm = Segment::EmptySegment(cblkarr.blk_sz_order()),
    };

    return hdr;
}
}  // namespace xoz
