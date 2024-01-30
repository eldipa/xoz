#include "xoz/dsc/descriptor_set.h"

#include <utility>

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/io/iosegment.h"
#include "xoz/repo/id_manager.h"

DescriptorSet::DescriptorSet(Segment& segm, BlockArray& dblkarr, BlockArray& eblkarr, IDManager& idmgr):
        segm(segm), dblkarr(eblkarr), eblkarr(dblkarr), idmgr(idmgr) {}

void DescriptorSet::load_set() {
    auto io = IOSegment(dblkarr, segm);
    load_descriptors(io);
}

void DescriptorSet::load_descriptors(IOBase& io) {
    const uint16_t dblk_sz_order = dblkarr.blk_sz_order();
    const uint32_t align = dblkarr.blk_sz();  // better semantic name

    if (io.remain_rd() % align != 0) {
        throw InconsistentXOZ(F() << "The remaining for reading is not multiple of " << align
                                  << " at loading descriptors: " << io.remain_rd() << " bytes remains");
    }

    if (io.tell_rd() % align != 0) {
        throw InconsistentXOZ(F() << "The reading position is not aligned to " << align
                                  << " at loading descriptors: " << io.tell_rd() << " bytes position");
    }

    while (io.remain_rd()) {
        // Try to read padding and if it so, skip the descriptor load
        if (io.remain_rd() >= 2) {
            if (io.read_u16_from_le() == 0x0000) {
                // padding, move on
                continue;
            }

            // ups, no padding, revert the reading
            io.seek_rd(2, IOBase::Seekdir::bwd);
        }

        // Read the descriptor
        assert(io.tell_rd() % align == 0);
        uint32_t dsc_begin_pos = io.tell_rd();
        auto dsc = Descriptor::load_struct_from(io, idmgr);
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
        if (dsc_end_pos <= dsc_begin_pos or dsc_end_pos - dsc_begin_pos < 2) {
            throw InternalError(F() << "The reading position after descriptor loaded was left too close or before the "
                                       "position before: left at "
                                    << dsc_end_pos << " bytes position");
        }

        // Set the Extent that corresponds to the place where the descriptor is
        uint32_t dsc_length = dsc_end_pos - dsc_begin_pos;

        assert(u32_fits_into_u16(dsc_length >> dblk_sz_order));
        dsc->ext = Extent(dsc_begin_pos >> dblk_sz_order, uint16_t(dsc_length >> dblk_sz_order), false);
        dsc->owner = this;

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
        if (owned.count(id) != 0) {
            throw InternalError(F() << "Descriptor id " << id
                                    << " found duplicated within the stream. This should never had happen. "
                                    << "Mostly likely an internal bug");
        }

        owned[id] = std::move(dsc);

        // dsc cannot be used any longer, it was transferred/moved to the dictionaries above
        // assert(!dsc); (ok, linter is detecting this)
    }
}


void DescriptorSet::zeros(IOBase& io, const Extent& ext) {
    const auto dblk_sz_order = dblkarr.blk_sz_order();
    io.seek_wr(ext.blk_nr() << dblk_sz_order);
    io.fill(0, ext.blk_cnt() << dblk_sz_order);
}


// TODO: we write the descriptors in arbitrary order so this is not very cache-friendly
// An optimization is possible. Moreover, the space of deleted/shrank descriptors are zero'd
// regardless if the same space is overwritten later by a new/grew descriptor.
// If somehow sort all by block nr we could fix these two issues at once (but it is way tricky!)
void DescriptorSet::_write_modified_descriptors(IOBase& io) {
    // Find any descriptor that shrank and it will require less space so we
    // can "split" the space and free a part.
    // Also, find any descriptor that grew so we remove it
    // and we re-add it later
    const auto dblk_sz_order = dblkarr.blk_sz_order();
    for (const auto dsc: to_update) {
        uint32_t cur_dsc_sz = dsc->calc_struct_footprint_size();
        uint32_t alloc_dsc_sz = uint32_t(dsc->ext.blk_cnt()) << dblk_sz_order;  // TODO byte_to_blk and blk_to_byte

        if (alloc_dsc_sz < cur_dsc_sz) {
            // grew so dealloc its current space and add it to the "to add" set
            //
            // note: we could try to expand the current extent and enlarge it in-place
            // but it is not supported by now and probably it would interfere with
            // the SegmentAllocator's split_above_threshold and it may lead to external
            // fragmentation inside the set. It is better a all-in-one "compaction" solution
            zeros(io, dsc->ext);
            dblkarr.allocator().dealloc_single_extent(dsc->ext);
            dsc->ext = Extent::EmptyExtent();

            // We add this desc to the to_add set but we don't remove it from
            // to_update.
            // It is OK, we will merge to_add and to_update sets later and
            // we will remove duplicated in the process.
            to_add.insert(dsc);

        } else if (alloc_dsc_sz > cur_dsc_sz) {
            // shrank so split and dealloc the unused part
            // Note: this split works because the descriptors sizes are a multiple
            // of the block size of the stream (dblk_sz_order).
            // By the RFC, this is a multiple of 2 bytes.
            assert(cur_dsc_sz % dblkarr.blk_sz() == 0);
            assert(u32_fits_into_u16(cur_dsc_sz >> dblk_sz_order));

            auto ext2 = dsc->ext.split(uint16_t(cur_dsc_sz >> dblk_sz_order));

            zeros(io, ext2);
            dblkarr.allocator().dealloc_single_extent(ext2);
        }
    }

    // Delete the descriptors that we don't want
    for (const auto& dscptr: to_remove) {
        zeros(io, dscptr->ext);
        dblkarr.allocator().dealloc_single_extent(dscptr->ext);

        // Dealloc the content being pointed (descriptor's external data)
        // but only if *we* are the owner of the descriptor too
        // If we are not the owner of the descriptor, we must assume that
        // the descritor was moved to another set so we need to remove
        // the descritor but not its blocks.
        if (dscptr->hdr.own_edata and dscptr->owner == this) {
            eblkarr.allocator().dealloc(dscptr->hdr.segm);
        }
    }
    to_remove.clear();

    // NOTE: at this moment we could compute how much was freed, how much is
    // about to be added and how much is already present in-disk (to_update and things not in to_update).
    // If the criteria match we could do a compaction *before* allocating new space (so it is more efficient)
    // This however would require update the descriptors' extents to their new positions
    // NOTE: compaction is not implemented yet

    // Alloc space for the new descriptors but do not write anything yet
    for (const auto dsc: to_add) {
        dsc->ext = dblkarr.allocator().alloc_single_extent(dsc->calc_struct_footprint_size());
    }

    // Add all the "new" descriptors to the "to update" list now that they
    // have space allocated in the stream
    // This will remove any duplicated descriptor between the two sets.
    to_update.insert(to_add.begin(), to_add.end());
    to_add.clear();

    for (const auto dsc: to_update) {
        auto pos = dsc->ext.blk_nr() << dblk_sz_order;

        io.seek_wr(pos);
        dsc->write_struct_into(io);
    }
    to_update.clear();


    // TODO compute checksum


    // TODO we may free blocks from the stream, but always?
    dblkarr.allocator().release();
}

// TODO we are using set<Descriptor*> and comparing pointers
// This will break if
//  - objects are moved ==> disable it
//  - two objects (2 addresses) have the same obj_id
//

void DescriptorSet::add(std::shared_ptr<Descriptor> dscptr) {
    if (!dscptr) {
        throw std::invalid_argument("Pointer to descriptor cannot by null");
    }

    // Check if the object belongs to another set
    // If that happen, the user must explicitly remove the descriptor from its current set
    // and only then add it to this one
    if (dscptr->owner != this and dscptr->owner != nullptr) {
        throw std::runtime_error("Descriptor already belongs to another set and cannot be added to a second one.");
    }

    if (dscptr->owner == this) {
        assert(dscptr->id() != 0);
    } else {
        assert(dscptr->owner == nullptr);
        if (dscptr->id() == 0) {
            dscptr->hdr.id = idmgr.request_temporal_id();
        }
    }


    assert(dscptr->id() != 0);
    owned[dscptr->id()] = dscptr;
    dscptr->owner = this;

    auto dsc = dscptr.get();

    to_add.insert(dsc);
    to_remove.erase(dscptr);
}

void DescriptorSet::move_out(std::shared_ptr<Descriptor> dscptr, DescriptorSet& new_home) {
    impl_remove(dscptr, &new_home);
    new_home.add(dscptr);
}

void DescriptorSet::move_out(uint32_t id, DescriptorSet& new_home) {
    if (not owned.contains(id)) {
        throw std::runtime_error("Descriptor does not belong to the set.");
    }
    move_out(owned[id], new_home);
}

void DescriptorSet::erase(std::shared_ptr<Descriptor> dscptr) { impl_remove(dscptr, nullptr); }

void DescriptorSet::erase(uint32_t id) {
    if (not owned.contains(id)) {
        throw std::runtime_error("Descriptor does not belong to the set.");
    }
    erase(owned[id]);
}

void DescriptorSet::mark_as_modified(std::shared_ptr<Descriptor> dscptr) {
    if (!dscptr) {
        throw std::invalid_argument("Pointer to descriptor cannot by null");
    }

    assert(dscptr->id() != 0);
    if (dscptr->owner != this or not owned.contains(dscptr->id())) {
        throw std::runtime_error("Descriptor does not belong to the set.");
    }

    if (to_remove.contains(dscptr)) {
        throw std::invalid_argument("Descriptor pending to be removed cannot be marked as modified");
    }

    auto dsc = dscptr.get();
    to_update.insert(dsc);
}

void DescriptorSet::mark_as_modified(uint32_t id) {
    if (not owned.contains(id)) {
        throw std::runtime_error("Descriptor does not belong to the set.");
    }
    mark_as_modified(owned[id]);
}

void DescriptorSet::impl_remove(std::shared_ptr<Descriptor> dscptr, DescriptorSet* new_home) {
    if (!dscptr) {
        throw std::invalid_argument("Pointer to descriptor cannot by null");
    }

    assert(dscptr->id() != 0);
    if (dscptr->owner != this or not owned.contains(dscptr->id())) {
        throw std::runtime_error("Descriptor does not belong to the set.");
    }

    auto dsc = dscptr.get();

    to_add.erase(dsc);
    to_update.erase(dsc);

    to_remove.insert(dscptr);

    owned.erase(dscptr->id());

    // If the "remove" has a "move" semantics, set the owner of the descriptor
    // to its new home set so we are *not* removing its external data blocks,
    // otherwise leave the owner untouched.
    if (new_home) {
        dscptr->owner = new_home;
    }
}

/*
void DescriptorSet::_write_descriptors(IOBase& io, bool write_all) {

    try {
        auto rwd = io.auto_rewind();

        if (write_all) {
            _write_all_descriptors(io);
        } else {
            _write_modified_descriptors(io);
        }

        rwd.dont_rewind();

        modified_objects.clear();
        modified_parts.clear();

    } catch (...) {
        // If an exception happen, the rwd.dont_rewind() in the try-block above
        // never happen so the rwd object restored to the io's write pointer position
        // of the *begin* of the write_modified_descriptors method call.
        //
        // At this moment we know that the new end-of-stream was not written
        // and it may be possible that the previous end-of-stream was overwritten.
        // In this scenario we override it again with a new end-of-stream.
        //
        // This is a best effort strategy to repair the stream and leave it
        // in a consistent state (with a valid end-of-stream)
        auto rwd = io.auto_rewind();

        auto eos = TypeZeroDescriptor::create_end_of_stream(0);
        eos->write_struct_into(io);

        // Keep propagating the exception
        throw;
    }
}
*/
