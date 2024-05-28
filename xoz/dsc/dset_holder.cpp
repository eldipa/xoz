#include "xoz/dsc/dset_holder.h"

#include <memory>
#include <utility>

#include "xoz/mem/inet_checksum.h"

DescriptorSetHolder::DescriptorSetHolder(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr,
                                         IDManager& idmgr):
        Descriptor(hdr, ed_blkarr),
        reserved(0),
        ed_blkarr(ed_blkarr),
        idmgr(idmgr),
        ext_indirect(Extent::EmptyExtent()) {}

std::unique_ptr<Descriptor> DescriptorSetHolder::create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr,
                                                        [[maybe_unused]] IDManager& idmgr) {
    assert(hdr.type == 0x01);

    // The magic will happen in read_struct_specifics_from() where we do the real
    // read/load of the holder and of its descriptor set.
    auto dsc = std::make_unique<DescriptorSetHolder>(hdr, ed_blkarr, idmgr);
    return dsc;
}

std::unique_ptr<DescriptorSetHolder> DescriptorSetHolder::create(BlockArray& ed_blkarr, IDManager& idmgr,
                                                                 uint16_t u16data) {

    struct header_t hdr = {.own_edata = false,  // empty set can be encoded without external data blocks

                           // As specified in the RFC, the holder of the descriptor set is a descriptor
                           // of type 0x01
                           .type = 0x01,
                           .id = 0x0,

                           // We need space for ours reserved field plus the reserved field of
                           // the empty set
                           .dsize = assert_u8(sizeof(uint16_t) * 2),

                           // No external data for an empty set
                           .esize = 0,
                           .segm = Segment::create_empty_zero_inline()};

    auto dsc = std::make_unique<DescriptorSetHolder>(hdr, ed_blkarr, idmgr);

    // Create the set later so we can pass the header.segment now that it is
    // in the heap. This is required because we want to ensure pointer stability.
    auto dset = std::make_unique<DescriptorSet>(dsc->hdr.segm, ed_blkarr, ed_blkarr, idmgr);
    dset->create_set(u16data);

    // The segment owned by the set (that points to the blocks that will hold
    // the set's descriptors) must have 0 inline data because it is not
    // supported by us.
    // Technically, we don't really care but it simplifies how much esize
    // we have (see hdr) because the size is just the same as the data space
    // size pointed by the segment (because it is all external).
    assert(dsc->hdr.segm.inline_data_sz() == 0);
    assert(not dsc->dset);

    // Keep and own a reference to the set.
    dsc->dset = std::move(dset);

    return dsc;
}

/*
uint32_t DescriptorSetHolder::get_checksum() const {
    return checksum;
}

void DescriptorSetHolder::set_checksum(uint32_t checksum) {
    this->checksum = checksum;
}
*/

void DescriptorSetHolder::read_struct_specifics_from(IOBase& io) {
    reserved = io.read_u16_from_le();

    Segment dset_segm;
    uint16_t dset_reserved = 0;
    if (is_indirect()) {
        if (not hdr.own_edata) {
            throw InconsistentXOZ("");
        }

        if (hdr.segm.ext_cnt() != 1 or hdr.segm.inline_data_sz() != 2) {
            throw InconsistentXOZ("");
        }

        uint16_t ext_indirect_stored_chksum = 0;
        {
            // Read only the last 2 bytes which are in the inline data size
            // (as the (inline_data_sz() == 2) condition above proved
            IOSegment io_indirect(ed_blkarr, hdr.segm);
            io_indirect.seek_rd(2, IOSegment::Seekdir::end);

            ext_indirect_stored_chksum = io_indirect.read_u16_from_le();
        }

        // Don't let the inline data interfere us
        hdr.segm.remove_inline_data();

        uint32_t ext_indirect_chksum = 0;
        {
            // Read the indirect segment that points to the descriptor set's blocks
            IOSegment io_indirect(ed_blkarr, hdr.segm);  // TODO cap/limit to hdr.esize
            dset_segm = Segment::load_struct_from(io_indirect, Segment::EndMode::AnyEnd, (uint32_t)(-1),
                                                  &ext_indirect_chksum);

            // Validate that the segment is not corrupted (we do this because this segment
            // lives physically outside the descriptor holder so it is not covered by the
            // checksum of the set that owns us (the holder))
            auto checksum_check = fold_inet_checksum(inet_remove(ext_indirect_chksum, ext_indirect_stored_chksum));
            if (not is_inet_checksum_good(checksum_check)) {
                throw InconsistentXOZ(F() << "Mismatch checksum for indirect segment, remained " << std::hex
                                          << checksum_check);
            }
        }

        // Keep a reference to the indirect extent, because we are its owner
        this->ext_indirect = hdr.segm.exts()[0];
        if (ext_indirect.is_empty()) {
            // TODO this should never happen, not even if w stored an empty set in indirect mode
            throw "";
        }
    } else {
        if (hdr.own_edata) {
            // Easiest case: the holder's segment points to the set's blocks
            dset_segm = hdr.segm;
        } else {
            // Second easiest case: the holder does not point to anything, the set
            // is empty. So build it from those bits.
            dset_reserved = io.read_u16_from_le();
            dset_segm = Segment::create_empty_zero_inline();
        }
    }

    if (dset_segm.inline_data_sz() != 0) {
        throw InconsistentXOZ(F() << "Unexpected non-zero inline data in segment for descriptor set holder.");
    }

    // DescriptorSet does not work with segments with inline data, even if it is empty.
    // Remove it before creating the set.
    dset_segm.remove_inline_data();
    auto dset = std::make_unique<DescriptorSet>(dset_segm, ed_blkarr, ed_blkarr, idmgr);

    if (not hdr.own_edata) {
        dset->create_set(dset_reserved);
    } else {
        assert(dset_reserved == 0);
        // TODO this will trigger a recursive chain reaction of reads if the set has other holders
        dset->load_set();
    }

    // Keep and own a reference to the set
    this->dset = std::move(dset);
}

void DescriptorSetHolder::write_struct_specifics_into(IOBase& io) {
    io.write_u16_to_le(reserved);

    if (not is_indirect() and dset->count() == 0) {
        io.write_u16_to_le(dset->u16data());
    }
}

void DescriptorSetHolder::update_header() {
    // Make the set to be 100% sync so we can know how much space its segment will require
    if (dset->count() == 0) {
        // Release any unused space so we get an empty segment from the set.
        //
        // This is a "little expensive" operation only if the set was not empty and become
        // empty before calling update_header(). If the set was empty after calling update_header(),
        // a second update_header() call will be "cheap"
        uint16_t u16data = dset->u16data();
        dset->remove_set();
        dset->create_set(u16data);
        assert(dset->segment().length() == 0);
        // TODO note: empty sets that fall here will have does_require_write in true which may
        // trigger unnecessary writes. However, because the set is truly empty, it should not
        // trigger a chain reaction of writes.
        // To meditate.
    }

    // We need to get the exact segment of the set before continuing.
    // Because the set is not empty or we are in indirect mode we know
    // that the segment is going to be non-empty.
    if (dset->count() != 0 or is_indirect()) {
        // TODO this will trigger a recursive chain reaction of writes if the set has other holders
        dset->write_set();
    }

    if (is_indirect()) {
        uint32_t ext_indirect_chksum = 0;

        // Now, after ensuring that the set's segment was updated
        // (via write_set() or remove_set()+create_set()) we can call this method
        // to get an updated ext_indirect Extent large enough to hold set's segment
        realloc_extent_to_store_dset_segment();
        assert(not ext_indirect.is_empty());

        {
            // Build the segment to be stored in the header by first adding the single
            // extent that points to the blocks that will hold the set's segment
            hdr.segm = Segment::EmptySegment();
            hdr.segm.add_extent(ext_indirect);
            hdr.segm.add_end_of_segment();

            IOSegment io_indirect(ed_blkarr, hdr.segm);
            dset->segment().write_struct_into(io_indirect, &ext_indirect_chksum);
        }

        {
            // Add 2 bytes in the header's segment inline to hold the dset's segment checksum
            hdr.segm.reserve_inline_data(2);

            IOSegment io_indirect(ed_blkarr, hdr.segm);
            io_indirect.seek_rd(2, IOSegment::Seekdir::end);

            io_indirect.write_u16_to_le(inet_to_u16(ext_indirect_chksum));
        }

        // yes, of course
        hdr.own_edata = true;
        hdr.esize = dset->segment().calc_struct_footprint_size();

        hdr.dsize = 2;  // 1 uint16 field: the holder's reserved field
    } else {
        if (dset->count() == 0) {
            // Second easiest case: the set is empty so we don't need to own any external data
            // and instead we save the minimum bits in holder's private space to reconstruct
            // an empty set later.
            hdr.own_edata = false;
            hdr.esize = 0;
            hdr.segm = Segment::create_empty_zero_inline();

            hdr.dsize = 4;  // 2 uint16 fields: the holder's and set's reserved fields
        } else {
            // Easiest case: the holder's segment is the set's segment. Nothing else is needed
            // except ensuring it has an end-of-segment because it is required by Descriptor
            hdr.segm = dset->segment();
            hdr.segm.add_end_of_segment();
            hdr.own_edata = true;
            hdr.esize = hdr.segm.calc_data_space_size(ed_blkarr.blk_sz_order());

            hdr.dsize = 2;  // 1 uint16 field: the holder's reserved field
        }
    }
}

void DescriptorSetHolder::destroy() {
    dset->remove_set();

    // Note we may be in indirect mode but still have an empty ext_indirect
    // This can happen if the user turn the is_indirect mode but it didn't
    // call update_header ever.
    if (is_indirect() and not ext_indirect.is_empty()) {
        ed_blkarr.allocator().dealloc_single_extent(ext_indirect);
    }
}

void DescriptorSetHolder::realloc_extent_to_store_dset_segment() {
    uint32_t cur_sz = ext_indirect.calc_data_space_size(ed_blkarr.blk_sz_order());
    uint32_t req_sz = dset->segment().calc_struct_footprint_size();
    assert(req_sz > 0);

    const bool should_expand = (cur_sz < req_sz);
    const bool should_shrink = ((cur_sz >> 1) >= req_sz);
    if (should_expand or should_shrink) {
        // TODO in the case of (cur_sz < req_sz), should we alloc more than req_sz?
        if (ext_indirect.is_empty()) {
            ext_indirect = ed_blkarr.allocator().alloc_single_extent(req_sz);
        } else {
            ext_indirect = ed_blkarr.allocator().realloc_single_extent(ext_indirect, req_sz);
        }
    }
}
