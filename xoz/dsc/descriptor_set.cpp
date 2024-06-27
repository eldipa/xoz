#include "xoz/dsc/descriptor_set.h"

#include <algorithm>
#include <list>
#include <utility>

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/io/iosegment.h"
#include "xoz/log/format_string.h"
#include "xoz/mem/inet_checksum.h"
#include "xoz/repo/runtime_context.h"

DescriptorSet::DescriptorSet(const Segment& segm, BlockArray& sg_blkarr, BlockArray& ed_blkarr, RuntimeContext& rctx):
        segm(segm),
        sg_blkarr(sg_blkarr),
        ed_blkarr(ed_blkarr),
        st_blkarr(this->segm, sg_blkarr, 2),
        rctx(rctx),
        set_loaded(false),
        reserved(0),
        checksum(0),
        header_does_require_write(false) {}

void DescriptorSet::load_set() { load_descriptors(false, 0); }

void DescriptorSet::create_set(uint16_t u16data) { load_descriptors(true, u16data); }

void DescriptorSet::load_descriptors(const bool is_new, const uint16_t u16data) {
    if (set_loaded) {
        throw std::runtime_error("DescriptorSet cannot be reloaded.");
    }

    if (is_new and st_blkarr.blk_cnt() != 0) {
        throw std::runtime_error("");
    }

    if (not is_new and st_blkarr.blk_cnt() == 0) {
        throw std::runtime_error("");
    }

    const uint32_t header_size = 4;

    if (is_new) {
        // If the set is new we don't have the header of the set.
        // Remember to make room for it later (we can initialize the st_blkarr's allocator()
        // with an empty list of allocated segments now and alloc space for the header later)
        header_does_require_write = true;
    }

    auto io = IOSegment(sg_blkarr, segm);

    const uint32_t align = st_blkarr.blk_sz();  // better semantic name
    assert(align == 2);                         // pre RFC

    if (io.remain_rd() % align != 0) {
        throw InconsistentXOZ(F() << "The remaining for reading is not multiple of " << align
                                  << " at loading descriptors: " << io.remain_rd() << " bytes remains");
    }

    if (io.tell_rd() % align != 0) {
        throw InconsistentXOZ(F() << "The reading position is not aligned to " << align
                                  << " at loading descriptors: " << io.tell_rd() << " bytes position");
    }

    checksum = 0;
    uint16_t stored_checksum = 0;

    std::list<Extent> allocated_exts;
    if (not is_new) {
        // read the header
        reserved = io.read_u16_from_le();
        stored_checksum = io.read_u16_from_le();

        // stored_checksum is not part of the checksum
        checksum = inet_add(checksum, reserved);

        // ensure that the allocator knows that our header is already reserved by us
        const auto ext = Extent(st_blkarr.bytes2blk_nr(0), st_blkarr.bytes2blk_cnt(header_size), false);
        allocated_exts.push_back(ext);
    } else {
        reserved = u16data;
        checksum = inet_add(checksum, reserved);
        stored_checksum = inet_to_u16(checksum);
    }

    [[maybe_unused]] auto io_rd_begin = io.tell_rd();

    while (io.remain_rd()) {
        // Try to read padding and if it so, skip the descriptor load
        if (io.remain_rd() >= align) {
            if (io.read_u16_from_le() == 0x0000) {
                // padding, move on, not need to checksum-them
                continue;
            }

            // ups, no padding, revert the reading
            io.seek_rd(2, IOBase::Seekdir::bwd);
        }

        // Read the descriptor
        assert(io.tell_rd() % align == 0);
        uint32_t dsc_begin_pos = io.tell_rd();
        auto dsc = Descriptor::load_struct_from(io, rctx, ed_blkarr);
        uint32_t dsc_end_pos = io.tell_rd();

        // Descriptor::load_struct_from should had check for any anomaly of how much
        // data was read and how much the descriptor said it should be read and raise
        // an exception if such anomaly was detected.
        //
        // Here, we just chk that we are still aligned. This should never happen
        // and it should be understood as a bug in the load_struct_from or descriptor
        // subclass
        if (dsc_end_pos % align != 0) {
            throw InternalError(F() << "The reading position was not left aligned to " << align
                                    << " after a descriptor load: left at " << dsc_end_pos << " bytes position");
        }
        if (dsc_end_pos <= dsc_begin_pos or dsc_end_pos - dsc_begin_pos < align) {
            throw InternalError(F() << "The reading position after descriptor loaded was left too close or before the "
                                       "position before: left at "
                                    << dsc_end_pos << " bytes position");
        }

        // Set the Extent that corresponds to the place where the descriptor is
        uint32_t dsc_length = dsc_end_pos - dsc_begin_pos;

        dsc->ext = Extent(st_blkarr.bytes2blk_nr(dsc_begin_pos), st_blkarr.bytes2blk_cnt(dsc_length), false);
        allocated_exts.push_back(dsc->ext);

        uint32_t id = dsc->id();


        // Descriptors or either have a new unique temporal id from RuntimeContext or
        // the id is loaded from the io. In this latter case, the id is registered in the RuntimeContext
        // to ensure uniqueness. All of this happen during the load of the descriptor,
        // not here in the set.
        //
        // Here we do a double check: if we detect a duplicated here, it is definitely a bug,
        // most likely in the code, no necessary in the XOZ file
        //
        // Note that if no duplicated ids are found here, it does not mean that the id is
        // not duplicated against other descriptor in other stream. That's why the real and
        // truly useful check is performed in RuntimeContext (that has a global view) during the
        // descriptor load..
        if (id == 0) {
            throw InternalError(F() << "Descriptor id " << id << " is not allowed. "
                                    << "Mostly likely an internal bug");
        }

        if (owned.count(id) != 0) {
            throw InternalError(F() << "Descriptor id " << id
                                    << " found duplicated within the stream. This should never had happen. "
                                    << "Mostly likely an internal bug");
        }

        checksum = fold_inet_checksum(inet_add(checksum, dsc->checksum));

        dsc->set_owner(this);
        owned[id] = std::move(dsc);

        // dsc cannot be used any longer, it was transferred/moved to the dictionaries above
        // assert(!dsc); (ok, linter is detecting this)
    }

    auto checksum_check = fold_inet_checksum(inet_remove(checksum, stored_checksum));
    if (not is_inet_checksum_good(checksum_check)) {
        throw InconsistentXOZ(F() << "Mismatch checksum for descriptor set on loading. "
                                  << "Read: 0x" << std::hex << stored_checksum << ", "
                                  << "computed: 0x" << std::hex << checksum << ", "
                                  << "remained: 0x" << std::hex << checksum_check);
    }

    assert((is_new and allocated_exts.size() == 0) or not is_new);

#ifndef NDEBUG
    {
        uint32_t chk = inet_checksum(reserved);
        io.seek_rd(io_rd_begin);
        chk += inet_checksum(io, io_rd_begin, io_rd_begin + io.remain_rd());
        if (chk == 0xffff) {
            chk = 0;
        }

        assert(chk == checksum);
    }
#endif

    // let the allocator know which extents are allocated (contain the descriptors) and
    // which are free for further allocation (padding or space between the boundaries of the io)
    st_blkarr.allocator().initialize_from_allocated(allocated_exts);

    // Officially loaded.
    set_loaded = true;
}


void DescriptorSet::zeros(IOBase& io, const Extent& ext) {
    io.seek_wr(st_blkarr.blk2bytes(ext.blk_nr()));
    io.fill(0, st_blkarr.blk2bytes(ext.blk_cnt()));
}

void DescriptorSet::flush_writes() {
    auto io = IOSegment(sg_blkarr, segm);
    write_modified_descriptors(io);
}

bool DescriptorSet::does_require_write() const {
    fail_if_set_not_loaded();
    return header_does_require_write or to_add.size() != 0 or to_remove.size() != 0 or to_update.size() != 0;
}

void DescriptorSet::write_modified_descriptors(IOBase& io) {
    if (segm.length() == 0) {
        [[maybe_unused]] auto ext = st_blkarr.allocator().alloc_single_extent(4);

        // Sanity check of the allocation for the header:
        //  - 4 bytes allocated as 2 full blocks in a single extent,
        //  - extent that it must be at the begin of the set (blk nr 0)
        assert(ext.blk_cnt() == 2);
        assert(ext.blk_nr() == 0);
        assert(segm.length() != 0);

        header_does_require_write = true;
    }

    // Find any descriptor that shrank and it will require less space so we
    // can "split" the space and free a part.
    // Also, find any descriptor that grew so we remove it
    // and we re-add it later
    std::list<Extent> pending;
    for (const auto dsc: to_update) {
        uint32_t cur_dsc_sz = dsc->calc_struct_footprint_size();
        uint32_t alloc_dsc_sz = st_blkarr.blk2bytes(dsc->ext.blk_cnt());

        if (alloc_dsc_sz < cur_dsc_sz) {
            // grew so dealloc its current space and add it to the "to add" set
            //
            // note: we could try to expand the current extent and enlarge it in-place
            // but it is not supported by now and probably it would interfere with
            // the SegmentAllocator's split_above_threshold and it may lead to external
            // fragmentation inside the set. It is better a all-in-one "compaction" solution
            pending.push_back(dsc->ext);
            dsc->ext = Extent::EmptyExtent();

            // We add this desc to the to_add set but we don't remove it from
            // to_update.
            // It is OK, we will merge to_add and to_update sets later and
            // we will remove duplicated in the process.
            to_add.insert(dsc);

        } else if (alloc_dsc_sz > cur_dsc_sz) {
            // shrank so split and dealloc the unused part
            // Note: this split works because the descriptors sizes are a multiple
            // of the block size of the stream (sg_blk_sz_order).
            // By the RFC, this is a multiple of 2 bytes.

            auto ext2 = dsc->ext.split(st_blkarr.bytes2blk_cnt(cur_dsc_sz));

            pending.push_back(ext2);
        }
    }

    // Delete the descriptors' extents that we don't want
    // Record descriptor's extent to be deallocated (only if not empty).
    //
    // Empty extent can happen if the descriptor was never written to disk
    // (it was added to to_add) but then it was erased (so it was removed from
    // to_add and added to to_remove).
    std::copy_if(to_remove.begin(), to_remove.end(), std::back_inserter(pending),
                 [](const Extent& ext) { return not ext.is_empty(); });
    to_remove.clear();

    // NOTE: at this moment we could compute how much was freed, how much is
    // about to be added and how much is already present in-disk (to_update and things not in to_update).
    // If the criteria match we could do a compaction *before* allocating new space (so it is more efficient)
    // This however would require update the descriptors' extents to their new positions
    // NOTE: compaction is not implemented yet

    // The problem:
    //
    // If the caller adds several descriptors and between each add calls flush_writes,
    // we will be allocating tiny pieces of space (one per descriptor) instead
    // of allocating a big chunk
    // This makes the resulting segment of the set very fragmented and large
    //
    // Possible optimizations:
    //
    // 1. Coalescing: the resulting segment may be fragmented (too many extents)
    //    but the space pointed by the segment may not.
    //    If the extents of the segments, in order, points to continuous
    //    blocks without gaps, we can be "coalesced/merged" them into few extents
    //    and therefore resulting a smaller segment.
    //    This is cheap because we don't need to write anything to disk except
    //    the new set descriptor (holder)
    //    The problem is that this works only if the fragmentation happens
    //    at the segment level: in other words, the blocks are contiguous.
    //    If the fragmentation happen at the blk level no coalescing will be possible.
    //
    //    Moreover, the extents of the segment cannot be reordered so even if together
    //    points to contiguous blocks, a coalescing will destroy the implicit order
    //    (and scramble what the reader would read).
    //
    // 2. Overallocation: instead of allocating exactly what we need for a single desc
    //    we allocate much more, like std::vector does. This minimize the impact of
    //    (potentially) expanding the block array and also improves locality hence
    //    reducing the fragmentation of both data and segment.
    //    The cost of this is we are wasting space.
    //    On a release_free_space() call we may cut the segment so we keep the space
    //    that we really need and the rest can be safely freed.
    //    Of course, if this happens to frequently (like calling release_free_space() after each flush_writes()),
    //    we are getting back again a too fragmented segment. Back to square one.
    //
    // 3. Preallocation: if we are adding a bunch of descriptors (because they are new
    //    or they were present but changed the size) we can preallocate the required
    //    space. This does not fixes the original problem (too many flush_writes())
    //    but avoids creating a new one (us calling too many times the allocator).
    //
    // 4. Compactation/defragmentation: similarly to preallocation, we can allocate
    //    a single chunk of space to hold all the descriptors of the set (added or not),
    //    copy the content of the descriptors to the new chunk and free the former.
    //    This makes the resulting chunk and segment very "compact" (defragmented).
    //    The downside is that we need to write everything back and temporally
    //    we have a temporal space allocated.
    //    The "Preallocation" strategy is a sort of defragmentation strategy that
    //    applies to only the added+modified descriptors, so the penalty of writing
    //    them it is already paid.
    //


    // Zero'd the to-be-removed extents and then dealloc them.
    // We split this into two phases because once we dealloc/alloc
    // something in st_blkarr, the segment's io becomes invalid
    // (the underlying segment changed)
    //
    // It may feel unnecessary to do the zero'ing because we are going
    // to dealloc them anyways. However, in case that something goes wrong
    // we can to aim to have a consistent descriptor set and padding of zeros
    // will do the trick
    for (const auto& ext: pending) {
        zeros(io, ext);
    }

    auto prev_segm_data_sz = segm.calc_data_space_size();

    for (const auto& ext: pending) {
        st_blkarr.allocator().dealloc_single_extent(ext);
    }

    // Destroy (including dealloc any external blocks) now that their owners (descriptors) were erased
    for (const auto& dscptr: to_destroy) {
        dscptr->destroy();
    }
    to_destroy.clear();

    // Alloc space for the new descriptors but do not write anything yet
    // TODO XXX pre-alloc the sum of the descriptors' sizes so the allocator can reserve
    // in one shot all the required space. Then, we alloc each single extent hopefully
    // from that pre-allocated space.
    // This should reduce the fragmentation of the set's segment making it much smaller
    for (const auto dsc: to_add) {
        dsc->ext = st_blkarr.allocator().alloc_single_extent(dsc->calc_struct_footprint_size());
    }

    auto new_segm_data_sz = segm.calc_data_space_size();

    // Now that all the alloc/dealloc happen, let's rebuild the io object
    auto io2 = IOSegment(sg_blkarr, segm);

    if (new_segm_data_sz > prev_segm_data_sz) {
        io2.seek_wr(prev_segm_data_sz);
        io2.fill(char(0x00), io2.remain_wr());
    }

    // Add all the "new" descriptors to the "to update" list now that they
    // have space allocated in the stream
    // This will remove any duplicated descriptor between the two sets.
    to_update.insert(to_add.begin(), to_add.end());
    to_add.clear();

    for (const auto dsc: to_update) {
        auto pos = st_blkarr.blk2bytes(dsc->ext.blk_nr());
        checksum = inet_remove(checksum, dsc->checksum);

        io2.seek_wr(pos);
        dsc->write_struct_into(io2);
        checksum = inet_add(checksum, dsc->checksum);
    }
    to_update.clear();

    // note: we don't checksum this->reserved because it should had been checksum
    // earlier and in each change to this->reserved.
    checksum = fold_inet_checksum(checksum);

    if (checksum == 0xffff) {
        checksum = 0x0000;
    }

    io2.seek_wr(0);

    io2.write_u16_to_le(this->reserved);
    io2.write_u16_to_le(assert_u16(this->checksum));
    header_does_require_write = false;

#ifndef NDEBUG
    {
        io2.seek_rd(0);
        uint32_t chk = inet_checksum(io2, 0, 2);
        io2.seek_rd(4);
        chk += inet_checksum(io2, 4, 4 + io2.remain_rd());
        if (chk == 0xffff) {
            chk = 0;
        }

        assert(chk == checksum);
    }
#endif
}

void DescriptorSet::release_free_space() { st_blkarr.allocator().release(); }

uint32_t DescriptorSet::add(std::unique_ptr<Descriptor> dscptr, bool assign_persistent_id) {
    fail_if_not_allowed_to_add(dscptr.get());

    // Grab ownership
    auto p = std::shared_ptr<Descriptor>(dscptr.release());
    add_s(p, assign_persistent_id);

    return p->id();
}

void DescriptorSet::add_s(std::shared_ptr<Descriptor> dscptr, bool assign_persistent_id) {
    fail_if_not_allowed_to_add(dscptr.get());

    if (rctx.is_persistent(dscptr->id())) {
        rctx.register_persistent_id(dscptr->id());
    }

    if (assign_persistent_id) {
        if (dscptr->id() == 0 or rctx.is_temporal(dscptr->id())) {
            dscptr->hdr.id = rctx.request_persistent_id();
        }
    }

    if (dscptr->id() == 0) {
        dscptr->hdr.id = rctx.request_temporal_id();
    }

    // own it
    dscptr->set_owner(this);
    owned[dscptr->id()] = dscptr;
    dscptr->ext = Extent::EmptyExtent();

    auto dsc = dscptr.get();
    to_add.insert(dsc);

    checksum = fold_inet_checksum(inet_add(checksum, dsc->checksum));

    assert(not to_update.contains(dsc));
}


void DescriptorSet::move_out(uint32_t id, DescriptorSet& new_home) {
    fail_if_set_not_loaded();

    // before modifying this ot the new set, check any possible (and reasonable)
    // condition where the move_out could fail:
    //  - because we don't have the descriptor pointed by id
    //  - because we cannot add the descriptor to the new set
    auto dscptr = get_owned_dsc_or_fail(id);
    new_home.fail_if_not_allowed_to_add(dscptr.get());

    impl_remove(dscptr, true);
    new_home.add_s(dscptr, false);
}

void DescriptorSet::erase(uint32_t id) {
    fail_if_set_not_loaded();
    auto dscptr = get_owned_dsc_or_fail(id);
    impl_remove(dscptr, false);
}

void DescriptorSet::mark_as_modified(uint32_t id) {
    fail_if_set_not_loaded();
    auto dscptr = get_owned_dsc_or_fail(id);

    // Add descriptor to to_update unless it is in the to_add set. If it is, do nothing:
    // to_add implies to_update anyways.
    auto dsc = dscptr.get();
    if (not to_add.contains(dsc)) {
        to_update.insert(dsc);
    }
}

void DescriptorSet::impl_remove(std::shared_ptr<Descriptor>& dscptr, bool moved) {
    auto dsc = dscptr.get();

    // Remove the descriptor from everywhere but add it to to_remove.
    // On write_modified_descriptors, if the descriptor was added to the set
    // before, its non-empty extent will be deallocated; if it was never added,
    // its empty extent will be skipped.
    to_add.erase(dsc);
    to_update.erase(dsc);

    to_remove.insert(dsc->ext);

    // Defer the descriptor destruction if it was removed and not moved outside
    // For that, keep a reference to the descriptor
    if (not moved) {
        to_destroy.insert(dscptr);
    }

    dscptr->set_owner(nullptr);
    owned.erase(dscptr->id());

    if (dscptr->checksum != 0) {
        checksum = fold_inet_checksum(inet_remove(checksum, dscptr->checksum));
    }
}

void DescriptorSet::clear_set() {
    fail_if_set_not_loaded();
    for (const auto& p: owned) {
        auto dscptr = p.second;
        auto dsc = dscptr.get();

        dsc->set_owner(nullptr);
        to_remove.insert(dsc->ext);
        to_destroy.insert(dscptr);

        if (dscptr->checksum != 0) {
            checksum = fold_inet_checksum(inet_remove(checksum, dscptr->checksum));
        }
    }

    owned.clear();
    to_add.clear();
    to_update.clear();
}

void DescriptorSet::remove_set() {
    fail_if_set_not_loaded();
    clear_set();

    // Call destructors
    for (const auto& dscptr: to_destroy) {
        dscptr->destroy();
    }
    to_destroy.clear();

    // Reset the allocator, deallocating and releasing all the space.
    // It is expected to have an empty st_blkarr after the call.
    // (calling reset() is much faster than calling dealloc_single_extent()
    // on each descriptor's extent)
    st_blkarr.allocator().reset();
    assert(st_blkarr.blk_cnt() == 0);
    assert(st_blkarr.capacity() == 0);

    // We don't want to write to disk that these extents are freed
    // because the whole set will not exist anymore.
    // So we drop these
    to_remove.clear();

    // The set now is officially "unloaded". The caller will have
    // to call create_set()/load_set() again.
    set_loaded = false;
    header_does_require_write = false;
    reserved = 0;
    checksum = 0;
    segm = Segment::EmptySegment(sg_blkarr.blk_sz_order());
}

uint32_t DescriptorSet::assign_persistent_id(uint32_t id) {
    fail_if_set_not_loaded();
    auto dscptr = get_owned_dsc_or_fail(id);

    if (rctx.is_temporal(id)) {
        owned.erase(id);

        auto ext_copy = dscptr->ext;
        dscptr->ext = Extent::EmptyExtent();

        add_s(dscptr, true);
        dscptr->ext = ext_copy;
    } else {
        rctx.register_persistent_id(id);
    }

    return dscptr->hdr.id;
}

std::shared_ptr<Descriptor> DescriptorSet::get(uint32_t id) {
    fail_if_set_not_loaded();
    return get_owned_dsc_or_fail(id);
}

void DescriptorSet::fail_if_set_not_loaded() const {
    if (not set_loaded) {
        throw std::runtime_error("DescriptorSet not loaded. Missed call to load_set()?");
    }
}

std::shared_ptr<Descriptor> DescriptorSet::get_owned_dsc_or_fail(uint32_t id) {
    if (not owned.contains(id)) {
        throw std::invalid_argument(
                (F() << "Descriptor " << xoz::log::hex(id) << " does not belong to the set.").str());
    }

    auto dscptr = owned[id];

    if (!dscptr) {
        throw std::runtime_error(
                (F() << "Descriptor " << xoz::log::hex(id) << " was found null inside the set.").str());
    }

    if (dscptr->id() != id) {
        throw std::runtime_error((F() << "Descriptor " << xoz::log::hex(id) << " claims to have a different id of "
                                      << xoz::log::hex(dscptr->id()) << " inside the set.")
                                         .str());
    }

    if (dscptr->get_owner() != this) {
        throw std::runtime_error((F() << "Descriptor " << xoz::log::hex(id) << " was found pointing to "
                                      << (dscptr->get_owner() == nullptr ? "a null" : "a different") << " owner set (0x"
                                      << std::hex << dscptr->get_owner() << ") instead of us (0x" << std::hex << this
                                      << ")")
                                         .str());
    }

    return dscptr;
}

void DescriptorSet::fail_if_using_incorrect_blkarray(const Descriptor* dsc) const {
    assert(dsc != nullptr);
    if (std::addressof(dsc->ed_blkarr) != std::addressof(ed_blkarr)) {
        throw std::runtime_error((F() << (*dsc) << " claims to use a block array for external data at " << std::hex
                                      << std::addressof(dsc->ed_blkarr) << " but the descriptor set is using one at "
                                      << std::hex << std::addressof(ed_blkarr))
                                         .str());
    }
}

void DescriptorSet::fail_if_null(const Descriptor* dsc) const {
    if (!dsc) {
        throw std::invalid_argument("Pointer to descriptor cannot by null");
    }
}

void DescriptorSet::fail_if_duplicated_id(const Descriptor* dsc) const {
    assert(dsc);

    // This should never happen because the caller should never have another
    // unique_ptr to the descriptor to call add() for a second time
    // (unless it is doing nasty things).
    if (owned.contains(dsc->id())) {
        throw std::invalid_argument((F() << (*dsc) << " has an id that collides with " << (*owned.at(dsc->id()))
                                         << " that it is already owned by the set")
                                            .str());
    }
}

void DescriptorSet::fail_if_not_allowed_to_add(const Descriptor* dsc) const {
    fail_if_set_not_loaded();
    fail_if_null(dsc);
    fail_if_using_incorrect_blkarray(dsc);
    fail_if_duplicated_id(dsc);
}
