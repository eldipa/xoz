#include "xoz/dsc/descriptor.h"

#include <algorithm>
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

}  // namespace

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

Descriptor::Descriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, uint16_t decl_cpart_cnt):
        hdr(hdr),
        decl_cpart_cnt(decl_cpart_cnt),
        ext(Extent::EmptyExtent()),
        cblkarr(cblkarr),
        owner_raw_ptr(nullptr),
        checksum(0) {

    const struct content_part_t example = {.future_csize = 0, .csize = 0, .segm = cblkarr.create_segment_with({})};

    // If declared more parts that the one in the header, increase the vector.
    // If declared less do nothing. We will show the first parts (up to decl_cpart_cnt)
    // and 'hide' but preserve any following part
    if (this->hdr.cparts.size() < decl_cpart_cnt) {
        this->hdr.cparts.resize(decl_cpart_cnt, example);
    }

    for (auto& cpart: this->hdr.cparts) {
        cpart.segm.add_end_of_segment();
    }

    // Update the decl_cpart_cnt counter in case that the initial cparts vector was larger
    // than expected. This could happen if we read from disk a version from the future
    // that the present Descriptor subclass expected less cparts.
    this->decl_cpart_cnt = assert_u16(this->hdr.cparts.size());
    chk_content_parts_consistency(false, this->hdr);
    chk_content_parts_count(false, this->hdr, this->decl_cpart_cnt);
    // assert(hdr.id != 0); // TODO ok? breaks too much tests, disable for now
}

Descriptor::Descriptor(const uint16_t type, BlockArray& cblkarr, uint16_t decl_cpart_cnt):
        Descriptor(create_header(type, cblkarr), cblkarr, decl_cpart_cnt) {}

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

    // Count content parts (if we own content at all)
    uint16_t content_part_cnt = 0;
    if (own_content) {
        content_part_cnt = io.read_u16_from_le();
        local_checksum += content_part_cnt;

        assert(content_part_cnt != 0xffff);  // TODO this is reserved

        content_part_cnt += 1;  // we always have at least 1 part
    }

    // Read the parts
    std::vector<struct content_part_t> cparts = reserve_content_part_vec(content_part_cnt);
    if (own_content) {
        local_checksum += inet_checksum(read_content_parts(io, cblkarr, cparts));
    }

    struct Descriptor::header_t hdr = {.type = type, .id = 0, .isize = isize, .cparts = cparts};

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

    chk_content_parts_consistency(false, hdr);
    // chk_content_parts_count(....); we can't here, see caller of load_header_from

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

std::vector<struct Descriptor::content_part_t> Descriptor::reserve_content_part_vec(uint16_t content_part_cnt) {
    return std::vector<content_part_t>(content_part_cnt);
}

uint32_t Descriptor::read_content_parts(IOBase& io, BlockArray& cblkarr, std::vector<struct content_part_t>& parts) {
    uint32_t local_checksum = 0;
    for (unsigned part_num = 0; part_num < parts.size(); ++part_num) {
        uint16_t sizefield = io.read_u16_from_le();

        local_checksum += sizefield;

        uint32_t lo_csize = 0, hi_csize = 0;
        bool large = assert_read_bits_from_u16(bool, sizefield, MASK_LARGE_FLAG);
        lo_csize = assert_read_bits_from_u16(uint32_t, sizefield, MASK_LO_CSIZE);

        if (large) {
            uint16_t largefield = io.read_u16_from_le();

            local_checksum += largefield;

            hi_csize = assert_read_bits_from_u16(uint32_t, largefield, MASK_HI_CSIZE);
        }

        parts[part_num].csize = (hi_csize << 15) | lo_csize;  // in bytes
        parts[part_num].segm = Segment::load_struct_from(io, cblkarr.blk_sz_order(), Segment::EndMode::AnyEnd,
                                                         uint32_t(-1), &local_checksum);
    }

    return local_checksum;
}

uint32_t Descriptor::write_content_parts(IOBase& io, const std::vector<struct content_part_t>& parts, int cparts_cnt) {
    assert(cparts_cnt > 0);
    assert(parts.size() >= unsigned(cparts_cnt));

    uint32_t local_checksum = 0;
    for (const auto& part: parts) {
        if (cparts_cnt <= 0) {
            break;
        }
        --cparts_cnt;

        uint16_t sizefield = 0;

        // Write the sizefield and optionally the largefield
        if (part.csize < (1 << 15)) {
            assert_write_bits_into_u16(sizefield, false, MASK_LARGE_FLAG);
            assert_write_bits_into_u16(sizefield, part.csize, MASK_LO_CSIZE);

            io.write_u16_to_le(sizefield);
            local_checksum += sizefield;
        } else {
            if (part.csize >= uint32_t(0x80000000)) {  // TODO test
                throw WouldEndUpInconsistentXOZ(F()
                                                << "Descriptor content size is larger than the maximum representable ("
                                                << uint32_t(0x80000000) << ") in " << hdr);
            }

            assert_write_bits_into_u16(sizefield, true, MASK_LARGE_FLAG);
            assert_write_bits_into_u16(sizefield, part.csize, MASK_LO_CSIZE);

            uint16_t largefield = 0;
            assert_write_bits_into_u16(largefield, assert_u16(part.csize >> 15), MASK_HI_CSIZE);

            io.write_u16_to_le(sizefield);
            io.write_u16_to_le(largefield);
            local_checksum += sizefield;
            local_checksum += largefield;
        }


        // Write the segment
        part.segm.write_struct_into(io, &local_checksum);
    }

    return local_checksum;
}

std::unique_ptr<Descriptor> Descriptor::load_struct_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr) {
    bool ex_type_used = false;
    uint32_t dsc_begin_pos = io.tell_rd();
    auto dsc = begin_load_dsc_from(io, rctx, cblkarr, dsc_begin_pos, ex_type_used);

    uint32_t idata_begin_pos = io.tell_rd();
    finish_load_dsc_from(io, rctx, cblkarr, *dsc, dsc_begin_pos, idata_begin_pos, ex_type_used);
    return dsc;
}

std::unique_ptr<Descriptor> Descriptor::begin_load_dsc_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr,
                                                            uint32_t dsc_begin_pos, bool& ex_type_used) {
    // Make the io read-only during the execution of this method
    [[maybe_unused]] auto guard = io.auto_restore_limits();
    io.limit_to_read_only();
    io.seek_rd(dsc_begin_pos);

    uint32_t checksum = 0;
    struct Descriptor::header_t hdr = load_header_from(io, rctx, cblkarr, ex_type_used, &checksum);

    descriptor_create_fn fn = rctx.dmap.descriptor_create_lookup(hdr.type);
    std::unique_ptr<Descriptor> dsc = fn(hdr, cblkarr, rctx);

    if (!dsc) {
        throw std::runtime_error((F() << "Subclass create for " << hdr << " returned a null pointer").str());
    }

    chk_dset_type(true, dsc.get(), hdr, rctx);
    chk_content_parts_count(false, dsc->hdr, dsc->decl_cpart_cnt);
    dsc->checksum = inet_to_u16(checksum);

    return dsc;
}

void Descriptor::finish_load_dsc_from(IOBase& io, [[maybe_unused]] RuntimeContext& rctx, BlockArray& cblkarr,
                                      Descriptor& dsc, uint32_t dsc_begin_pos, uint32_t idata_begin_pos,
                                      bool ex_type_used) {
    // Make the io read-only during the execution of this method
    [[maybe_unused]] auto guard = io.auto_restore_limits();
    io.limit_to_read_only();
    io.seek_rd(idata_begin_pos);

    {
        [[maybe_unused]] auto alloc_guard = cblkarr.allocator().block_all_alloc_dealloc_guard();
        [[maybe_unused]] auto limit_guard = io.auto_restore_limits();
        io.limit_rd(idata_begin_pos, dsc.hdr.isize);

        dsc.read_struct_specifics_from(io);
        dsc.read_future_idata(io);

        xoz_assert("load future idata odd size", dsc.future_idata_size() % 2 == 0);
    }
    uint32_t dsc_end_pos = io.tell_rd();

    chk_rw_specifics_on_idata(true, io, idata_begin_pos, dsc_end_pos, dsc.hdr.isize);
    chk_struct_footprint(true, io, dsc_begin_pos, dsc_end_pos, &dsc, ex_type_used);

    // note: as the check in chk_rw_specifics_on_idata and the following assert
    // we can be 100% sure that load_header_from checksummed all the data field from
    // idata_begin_pos to idata_begin_pos+dsc.hdr.isize.
    assert(dsc_end_pos == idata_begin_pos + dsc.hdr.isize);

    dsc.compute_future_content_parts_sizes();
    dsc.update_sizes_of_header();
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

    chk_content_parts_consistency(true, hdr);
    chk_content_parts_count(true, hdr, decl_cpart_cnt);
    chk_dset_type(false, this, hdr, rctx);

    uint32_t checksum = 0;

    // Parts at the end of the vector that are empty can be
    // stripped away to compress a little so they don't count.
    int cparts_cnt = count_incompressible_cparts();

    // If the id is persistent we must store it. It may not be persistent but we may require
    // store hi_isize so in that case we still need the idfield (but with an id of 0)
    bool has_id = is_id_persistent(hdr.id) or hdr.isize >= (32 << 1);

    uint16_t firstfield = 0;

    assert_write_bits_into_u16(firstfield, cparts_cnt > 0, MASK_OWN_CONTENT_FLAG);
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


    if (cparts_cnt > 0) {
        io.write_u16_to_le(assert_u16(cparts_cnt - 1));
        checksum += inet_checksum(assert_u16(cparts_cnt - 1));
        checksum += inet_checksum(write_content_parts(io, hdr.cparts, cparts_cnt));
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

    // Count how many content parts do we have.
    // Parts at the end of the vector that are empty can be
    // stripped away to compress a little so they don't count.
    int cparts_cnt = count_incompressible_cparts();

    if (cparts_cnt > 0) {
        // content_part_cnt
        struct_sz += 2;

        for (const auto& part: hdr.cparts) {
            if (cparts_cnt <= 0) {
                break;
            }
            --cparts_cnt;

            if (part.csize < (1 << 15)) {
                // sizefield
                struct_sz += 2;
            } else {
                // sizefield and largefield
                struct_sz += 2;
                struct_sz += 2;
            }

            // segment
            struct_sz += part.segm.calc_struct_footprint_size();
        }
    }

    if (hdr.type >= EXTENDED_TYPE_VAL_THRESHOLD) {
        // ex_type field
        struct_sz += 2;
    }

    struct_sz += hdr.isize;  // hdr.isize is in bytes too

    return struct_sz;
}

uint16_t Descriptor::count_incompressible_cparts() const { return count_incompressible_cparts(hdr); }

uint16_t Descriptor::count_incompressible_cparts(const struct header_t& hdr) {
    // Count how many content parts do we have.
    // Parts at the end of the vector that are empty can be
    // stripped away to compress a little so they don't count.
    uint16_t cparts_cnt = assert_u16(hdr.cparts.size());
    for (int i = cparts_cnt - 1; i >= 0; --i) {
        if (hdr.cparts[assert_u16(i)].csize > 0) {
            break;
        } else {
            --cparts_cnt;
        }
    }

    return cparts_cnt;
}

std::ostream& operator<<(std::ostream& out, const struct Descriptor::header_t& hdr) {
    PrintTo(hdr, &out);
    return out;
}

void PrintTo(const struct Descriptor::header_t& hdr, std::ostream* out) {
    std::ios_base::fmtflags ioflags = out->flags();

    (*out) << "descriptor {"
           << "id: " << xoz::log::hex(hdr.id) << ", type: " << hdr.type << ", isize: " << uint32_t(hdr.isize);

    if (hdr.cparts.size() > 0) {
        (*out) << ", [use/csize segm]: ";
        for (const auto& cpart: hdr.cparts) {
            (*out) << cpart.csize - cpart.future_csize << "/" << cpart.csize << " " << cpart.segm;
        }
    }
    (*out) << "}";

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

void Descriptor::destroy() {
    for (const auto& cpart: hdr.cparts) {
        // Dealloc all the segments even if the csize is zero
        // A non-empty segment could have associated a csize of zero in case
        // of an allocation request of 0 size but that >0 bytes were actually allocated
        // Here we need to dealloc everything.
        cblkarr.allocator().dealloc(cpart.segm);
    }
    hdr.cparts.clear();
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

void Descriptor::update_header() { update_sizes_of_header(); }

void Descriptor::compute_future_content_parts_sizes() {
    std::vector<uint64_t> cparts_sizes;
    cparts_sizes.resize(hdr.cparts.size());

    // Get a modifiable copy of the cparts' sizes
    std::transform(hdr.cparts.cbegin(), hdr.cparts.cend(), cparts_sizes.begin(),
                   [](const struct content_part_t& cpart) { return cpart.csize; });

    // Let the subclasses to say how much data *their* really owns
    declare_used_content_space_on_load(cparts_sizes);

    for (uint32_t i = 0; i < hdr.cparts.size(); ++i) {
        const uint32_t hdr_csize = hdr.cparts[i].csize;
        const uint64_t present_csize = cparts_sizes[i];

        // Note: we check this to avoid a overflow.
        // More checks should be made by the caller calling chk_content_parts_consistency et al.
        if (hdr_csize < present_csize) {
            throw WouldEndUpInconsistentXOZ(F() << "Declared csize of content part " << i
                                                << " for present version overflows with csize found in the header; "
                                                << "they are respectively " << present_csize << " and " << hdr_csize);
        }

        hdr.cparts[i].future_csize = assert_u32_sub_nonneg(hdr_csize, assert_u32(present_csize));
    }
}

void Descriptor::update_sizes_of_header() {
    // By contract (see update_isize), we need to provide to the callee what we think
    // is the current/present isize (we cannot just pass 0 or a hardcoded default.).
    // If the callee decides to no update the isize, we will keep then with the value
    // that we initially think for free.
    uint64_t present_isize = assert_u8_sub_nonneg(hdr.isize, future_idata_size());
    update_isize(present_isize);

    // Let the subclass to modify the cparts.
    // This is ugly!!!
    update_content_parts(hdr.cparts);

    // Ensure that the segments have an end-of-segment
    for (auto& cpart: hdr.cparts) {
        cpart.segm.add_end_of_segment();
    }

    // Sanity checks
    chk_content_parts_consistency(true, hdr);
    chk_content_parts_count(true, hdr, decl_cpart_cnt);

    if (not is_u64_add_ok(present_isize, future_idata_size())) {
        throw WouldEndUpInconsistentXOZ(
                F() << "Updated isize for present version overflows with isize for future version; "
                    << "they are respectively " << present_isize << " and " << future_idata_size());  // TODO test
    }

    if (present_isize % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F() << "Updated isize for present version (" << present_isize << ") "
                                            << "is an odd number (it must be even).");  // TODO test
    }

    uint64_t hdr_isize = assert_u64_add_nowrap(present_isize, future_idata_size());

    if (not does_hdr_isize_fit(hdr_isize)) {
        throw WouldEndUpInconsistentXOZ(F() << "Updated isize for present version (" << present_isize << ") "
                                            << "plus isize for future version (" << future_idata_size() << ") "
                                            << "does not fit in the header");  // TODO test
    }

    xoz_assert("odd hdr isize", hdr_isize % 2 == 0);

    hdr.isize = assert_u8(hdr_isize);
}

void Descriptor::declare_used_content_space_on_load([[maybe_unused]] std::vector<uint64_t>& cparts_sizes) const {}

void Descriptor::update_content_parts([[maybe_unused]] std::vector<struct Descriptor::content_part_t>& cparts) {}

void Descriptor::resize_content_part(struct Descriptor::content_part_t& cpart, uint32_t content_new_sz) {
    // No previous content and nothing to grow, then skip (no change)
    if (cpart.csize == 0 and content_new_sz == 0) {
        // TODO should try to dealloc anyways???
        xoz_assert("invariant", cpart.future_csize == 0);
        return;
    }

    if (not does_present_csize_fit(cpart, content_new_sz)) {
        throw WouldEndUpInconsistentXOZ(F() << "The new content size (" << content_new_sz << ") "
                                            << "plus the size from the future version (" << cpart.future_csize << ") "
                                            << "does not fit in the header.");
    }

    // Caller wants some space for the (new) content
    if (cpart.csize == 0) {
        xoz_assert("invariant", cpart.future_csize == 0);

        // TODO dealloc first! then alloc. A specialize realloc may be?
        cpart.segm = cblkarr.allocator().alloc(content_new_sz);
        cpart.segm.add_end_of_segment();

        xoz_assert("allocated less than requested", cpart.segm.calc_data_space_size() >= content_new_sz);

        // Save the caller's content_new_sz, not the real size of the segment.
        // This reflects correctly what the descriptor owns, the rest (padding)
        // is for "performance" reasons of the allocator.
        // We need to be *very* strict in the value of csize because we will use
        // it to know if there is future content or not and we *don't* want
        // to treat padding as future content (hence future_content_size = 0).
        cpart.csize = content_new_sz;
        return;
    }

    // We own some content but the caller does not want it
    // and we don't have any future data to preserve so we dealloc
    if (content_new_sz == 0 and cpart.future_csize == 0) {
        cblkarr.allocator().dealloc(cpart.segm);
        cpart.segm.remove_inline_data();
        cpart.segm.remove_end_of_segment();
        cpart.segm.clear();

        cpart.csize = 0;
        return;
    }

    uint32_t csize_new = content_new_sz + cpart.future_csize;  // TODO add an assert?
    if (cpart.csize < csize_new) {
        // The content is expanding.
        //
        // Perform the realloc
        cblkarr.allocator().realloc(cpart.segm, csize_new);

        // Copy from the io content the future content at the end of the io
        auto content_io = IOSegment(cblkarr, cpart.segm);
        content_io.seek_rd(assert_u32_sub_nonneg(cpart.csize, cpart.future_csize), IOBase::Seekdir::beg);
        content_io.seek_wr(cpart.future_csize, IOBase::Seekdir::end);
        content_io.copy_into_self(cpart.future_csize);

    } else if (cpart.csize > csize_new) {
        // The content is shrinking, we need to copy outside the future content,
        // do the realloc (shrink) and copy the future back.
        //
        // We can copy the future content into memory iff is small enough,
        // otherwise we go to disk.
        if (cpart.future_csize < RESIZE_CONTENT_MEM_COPY_THRESHOLD_SZ) {
            // The future_content size is small enough, copy it into memory
            std::vector<char> future_data;
            future_data.reserve(cpart.future_csize);
            {
                auto content_io = IOSegment(cblkarr, cpart.segm);
                content_io.seek_rd(cpart.future_csize, IOBase::Seekdir::end);
                content_io.readall(future_data);
            }

            // Perform the realloc
            cblkarr.allocator().realloc(cpart.segm, csize_new);

            // Copy back the future data
            {
                auto content_io = IOSegment(cblkarr, cpart.segm);  // cpart.segm changed, get a new io
                content_io.seek_wr(cpart.future_csize, IOBase::Seekdir::end);
                content_io.writeall(future_data);
            }

        } else {
            // The future_csize size is too large, copy it into disk
            auto future_sg = cblkarr.allocator().alloc(cpart.future_csize);
            auto future_io = IOSegment(cblkarr, future_sg);
            {
                auto content_io = IOSegment(cblkarr, cpart.segm);
                content_io.seek_rd(cpart.future_csize, IOBase::Seekdir::end);
                content_io.copy_into(future_io, cpart.future_csize);
            }

            // Perform the realloc
            cblkarr.allocator().realloc(cpart.segm, csize_new);

            // Copy back the future data
            {
                auto content_io = IOSegment(cblkarr, cpart.segm);  // cpart.segm changed, get a new io
                content_io.seek_wr(cpart.future_csize, IOBase::Seekdir::end);
                future_io.seek_rd(0);
                future_io.copy_into(content_io, cpart.future_csize);
            }
        }
    } /* else { neither shrink nor expand, leave it as is } */
    cpart.segm.add_end_of_segment();

    xoz_assert("allocated less than requested", cpart.segm.calc_data_space_size() >= csize_new);
    cpart.csize = csize_new;
}

IOSegment Descriptor::get_content_part_io(struct Descriptor::content_part_t& cpart) {
    // Hide from the caller the future content
    //
    // Note: if get_content_part_io() is called from read_struct_specifics_from,
    // by that time future content size is not set yet and it will default to 0
    auto io = IOSegment(cblkarr, cpart.segm);
    auto present_csize = assert_u32_sub_nonneg(cpart.csize, cpart.future_csize);

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

bool Descriptor::does_present_csize_fit(const struct Descriptor::content_part_t& cpart, uint64_t present_csize) const {
    if (not is_u64_add_ok(present_csize, cpart.future_csize)) {
        return false;
    }

    uint64_t hdr_csize = assert_u64_add_nowrap(present_csize, cpart.future_csize);

    return does_hdr_csize_fit(hdr_csize);
}

struct Descriptor::header_t Descriptor::create_header(const uint16_t type, [[maybe_unused]] const BlockArray& cblkarr) {
    struct Descriptor::header_t hdr = {.type = type, .id = 0x0, .isize = 0, .cparts = {}};

    return hdr;
}

void Descriptor::collect_segments_into(std::list<Segment>& collection) const {
    std::for_each(hdr.cparts.cbegin(), hdr.cparts.cend(),
                  [&collection](const auto& part) { collection.push_back(part.segm); });
}

void Descriptor::chk_content_parts_count(bool wouldBe, const Descriptor::header_t& hdr, const uint16_t decl_cpart_cnt) {
    F errmsg;
    if (hdr.cparts.size() != decl_cpart_cnt) {
        errmsg = std::move(F() << "The descriptor code declared to use " << decl_cpart_cnt
                               << " content parts but it has " << hdr.cparts.size()
                               << ". May be the update_content_parts() has a bug?");
        goto fail;
    }

    return;

fail:
    if (wouldBe) {
        throw WouldEndUpInconsistentXOZ(errmsg);
    } else {
        throw InconsistentXOZ(errmsg);
    }
}

void Descriptor::chk_content_parts_consistency(bool wouldBe, const Descriptor::header_t& hdr) {
    F errmsg;
    int part_ix = 0;

    for (const auto& cpart: hdr.cparts) {
        if (cpart.csize < cpart.future_csize) {
            errmsg = std::move(F() << "The content part at index " << part_ix << " declares to have a csize of "
                                   << cpart.csize << " bytes"
                                   << " which it is less than the computed future_csize of " << cpart.future_csize
                                   << " bytes.");
            goto fail;
        }

        if (cpart.csize > cpart.segm.calc_data_space_size()) {
            errmsg = std::move(F() << "The content part at index " << part_ix << " declares to have a csize of "
                                   << cpart.csize << " bytes"
                                   << " which it is greater than the available space in the segment of "
                                   << cpart.segm.calc_data_space_size() << " bytes.");
            goto fail;
        }

        if (not does_hdr_csize_fit(cpart.csize)) {
            errmsg = std::move(F() << "The content part at index " << part_ix << " declares to have a csize of "
                                   << cpart.csize << " bytes"
                                   << " that does not fit in the header.");
            goto fail;
        }

        ++part_ix;
    }

    return;

fail:
    if (wouldBe) {
        throw WouldEndUpInconsistentXOZ(errmsg);
    } else {
        throw InconsistentXOZ(errmsg);
    }
}
}  // namespace xoz
