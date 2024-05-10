#include "xoz/dsc/descriptor_set.h"

#include <algorithm>
#include <list>
#include <utility>

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/io/iosegment.h"
#include "xoz/mem/inet_checksum.h"
#include "xoz/repo/id_manager.h"

DescriptorSet::DescriptorSet(Segment& segm, BlockArray& sg_blkarr, BlockArray& ed_blkarr, IDManager& idmgr):
        segm(segm),
        sg_blkarr(sg_blkarr),
        ed_blkarr(ed_blkarr),
        st_blkarr(segm, sg_blkarr, 2),
        idmgr(idmgr),
        set_loaded(false),
        reserved(0),
        checksum(0),
        header_does_require_write(false) {}

void DescriptorSet::load_set() { load_descriptors(false); }

void DescriptorSet::create_set() { load_descriptors(true); }

void DescriptorSet::load_descriptors(const bool is_new) {
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
        // Make room for it know before initializing the st_blkarr's allocator()
        //
        // This is needed because we will later claim that the Extent from
        // blocks 0 and 1 (4 bytes, the header size) are already used and they should be added
        // as an already-allocated extent. Hence, we *need* to actually have something
        // allocated for real.
        st_blkarr.grow_by_blocks(st_blkarr.bytes2blk_cnt(header_size));
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
    }

    {
        // ensure that the allocator knows that our header is already reserved by us
        const auto ext = Extent(st_blkarr.bytes2blk_nr(0), st_blkarr.bytes2blk_cnt(header_size), false);
        allocated_exts.push_back(ext);
    }

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
        auto dsc = Descriptor::load_struct_from(io, idmgr, ed_blkarr);
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


        // Descriptors or either have a new unique temporal id from IDManager or
        // the id is loaded from the io. In this latter case, the id is registered in the IDManager
        // to ensure uniqueness. All of this happen during the load of the descriptor,
        // not here in the set.
        //
        // Here we do a double check: if we detect a duplicated here, it is definitely a bug,
        // most likely in the code, no necessary in the XOZ file
        //
        // Note that if no duplicated ids are found here, it does not mean that the id is
        // not duplicated against other descriptor in other stream. That's why the real and
        // truly useful check is performed in IDManager (that has a global view) during the
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

        owned[id] = std::move(dsc);

        // dsc cannot be used any longer, it was transferred/moved to the dictionaries above
        // assert(!dsc); (ok, linter is detecting this)
    }

    auto checksum_check = fold_inet_checksum(inet_remove(checksum, stored_checksum));
    if (not is_inet_checksum_good(checksum_check)) {
        throw InconsistentXOZ(F() << "Mismatch checksum for descriptor set, remained " << std::hex << checksum);
    }

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

void DescriptorSet::write_set() {
    auto io = IOSegment(sg_blkarr, segm);
    write_modified_descriptors(io);
}

bool DescriptorSet::does_require_write() const {
    fail_if_set_not_loaded();
    return header_does_require_write or to_add.size() != 0 or to_remove.size() != 0 or to_update.size() != 0;
}

void DescriptorSet::write_modified_descriptors(IOBase& io) {
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


    // Zero'd the to-be-removed extents and then dealloc them.
    // We split this into two phases because once we dealloc/alloc
    // something in st_blkarr, the segment's io becomes invalid
    // (the underlying segment changed)
    for (const auto& ext: pending) {
        zeros(io, ext);
    }

    for (const auto& ext: pending) {
        st_blkarr.allocator().dealloc_single_extent(ext);
    }

    // Destroy (including dealloc any external blocks) now that their owners (descriptors) were erased
    for (const auto& dscptr: to_destroy) {
        dscptr->destroy();
    }
    to_destroy.clear();

    // Alloc space for the new descriptors but do not write anything yet
    for (const auto dsc: to_add) {
        dsc->ext = st_blkarr.allocator().alloc_single_extent(dsc->calc_struct_footprint_size());
    }

    // Now that all the alloc/dealloc happen, let's rebuild the io object
    auto io2 = IOSegment(sg_blkarr, segm);

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

    checksum = inet_add(checksum, this->reserved);
    checksum = fold_inet_checksum(checksum);

    if (checksum == 0xffff) {
        checksum = 0x0000;
    }

    io2.seek_wr(0);

    io2.write_u16_to_le(this->reserved);
    io2.write_u16_to_le((uint16_t)this->checksum);
    header_does_require_write = false;
}

void DescriptorSet::release_free_space() { st_blkarr.allocator().release(); }

uint32_t DescriptorSet::add(std::unique_ptr<Descriptor> dscptr, bool assign_persistent_id) {
    fail_if_set_not_loaded();
    if (!dscptr) {
        throw std::invalid_argument("Pointer to descriptor cannot by null");
    }

    // This should never happen because the caller should never have another
    // unique_ptr to the descriptor to call add() for a second time
    // (unless it is doing nasty things).
    if (owned.contains(dscptr->id())) {
        throw std::invalid_argument("Descriptor is already owned by the set");
    }

    // Grab ownership
    auto p = std::shared_ptr<Descriptor>(dscptr.release());
    add_s(p, assign_persistent_id);

    return p->id();
}

void DescriptorSet::add_s(std::shared_ptr<Descriptor> dscptr, bool assign_persistent_id) {
    if (!dscptr) {
        throw std::invalid_argument("Pointer to descriptor cannot by null");
    }

    if (idmgr.is_persistent(dscptr->id())) {
        idmgr.register_persistent_id(dscptr->id());
    }

    // TODO chk that descriptor is using the same "external" block array than us

    if (assign_persistent_id) {
        if (dscptr->id() == 0 or idmgr.is_temporal(dscptr->id())) {
            dscptr->hdr.id = idmgr.request_persistent_id();
        }
    }

    if (dscptr->id() == 0) {
        dscptr->hdr.id = idmgr.request_temporal_id();
    }

    // own it
    owned[dscptr->id()] = dscptr;
    dscptr->ext = Extent::EmptyExtent();

    auto dsc = dscptr.get();
    to_add.insert(dsc);

    checksum = fold_inet_checksum(inet_add(checksum, dsc->checksum));

    assert(not to_update.contains(dsc));
}


void DescriptorSet::move_out(uint32_t id, DescriptorSet& new_home) {
    fail_if_set_not_loaded();
    auto dscptr = impl_remove(id, true);
    new_home.add_s(dscptr, false);
}

void DescriptorSet::erase(uint32_t id) {
    fail_if_set_not_loaded();
    impl_remove(id, false);
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

std::shared_ptr<Descriptor> DescriptorSet::impl_remove(uint32_t id, bool moved) {
    auto dscptr = get_owned_dsc_or_fail(id);

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

    owned.erase(dscptr->id());

    if (dscptr->checksum != 0) {
        checksum = fold_inet_checksum(inet_remove(checksum, dscptr->checksum));
    }

    return dscptr;
}

void DescriptorSet::clear_set() {
    fail_if_set_not_loaded();
    for (const auto& p: owned) {
        auto dscptr = p.second;
        auto dsc = dscptr.get();

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

    // We don't want to write to disk that these extents are freed
    // because the whole set will not exist anymore.
    // So we drop these
    to_remove.clear();

    // Remove any space associated with the set, even its header
    st_blkarr.shrink_by_blocks(st_blkarr.blk_cnt());

    // The set now is officially "unloaded". The caller will have
    // to call create_set() again.
    set_loaded = false;
    header_does_require_write = false;
    reserved = 0;
    checksum = 0;
}

uint32_t DescriptorSet::assign_persistent_id(uint32_t id) {
    fail_if_set_not_loaded();
    auto dscptr = get_owned_dsc_or_fail(id);

    if (idmgr.is_temporal(id)) {
        owned.erase(id);

        auto ext_copy = dscptr->ext;
        dscptr->ext = Extent::EmptyExtent();

        add_s(dscptr, true);
        dscptr->ext = ext_copy;
    } else {
        idmgr.register_persistent_id(id);
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
        throw std::runtime_error((F() << "Descriptor " << id << " does not belong to the set.").str());
    }

    auto dscptr = owned[id];

    if (!dscptr) {
        throw std::runtime_error((F() << "Descriptor " << id << " was found null inside the set.").str());
    }

    if (dscptr->id() != id) {
        throw std::runtime_error((F() << "Descriptor " << id << " claims to have a different id of " << dscptr->id()
                                      << " inside the set.")
                                         .str());
    }

    return dscptr;
}
