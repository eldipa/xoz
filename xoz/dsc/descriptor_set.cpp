#include "xoz/dsc/descriptor_set.h"

#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/ext/block_array.h"
#include "xoz/repo/id_manager.h"

DescriptorSet::DescriptorSet(BlockArray& blkarr /* TODO */): sg_alloc_dsc() { sg_alloc_dsc.manage_block_array(blkarr); }

void DescriptorSet::load_descriptors(IOBase& io, IDManager& idmgr) {
    if (io.remain_rd() % 2 != 0) {
        throw InconsistentXOZ(F() << "The remaining for reading is not multiple of 2 at loading descriptors: "
                                  << io.remain_rd() << " bytes remains");
    }

    if (io.tell_rd() % 2 != 0) {
        throw InconsistentXOZ(F() << "The reading position is not aligned to 2 at loading descriptors: " << io.tell_rd()
                                  << " bytes position");
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

        assert(io.tell_rd() % 2 == 0);
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
        if (dsc_end_pos % 2 != 0) {
            throw InternalError(F() << "The reading position was not left aligned to 2 after a descripto load: left at "
                                    << dsc_end_pos << " bytes position");
        }

        // Set the Extent that corresponds to the place where the descriptor
        uint32_t dsc_length = dsc_end_pos - dsc_begin_pos;

        assert(u32_fits_into_u16(dsc_length >> 1));
        dsc->ext = Extent(dsc_begin_pos >> 1, uint16_t(dsc_length >> 1), false);  // TODO blk_sz
        dsc->owner = this;

        uint32_t id = dsc->id();


        // Descriptors or either has an new unique temporal id from IDManager or
        // the id is loaded from the io but registered in the IDManager that should
        // ensure uniqueness.
        //
        // If we detect a duplicated here, it is definitely a bug, most likely in the code,
        // no necessary in the XOZ file
        //
        // Note that if no duplicated ids are found here, it does not mean that the id is
        // not duplicated against other descriptor in other stream. That's why the check
        // is performed in IDManager that has a global view.
        if (dsc_by_id.count(id) != 0) {
            throw InternalError(F() << "Descriptor id " << id
                                    << " found duplicated within the stream. This should never had happen. Mostly "
                                       "likely an internal bug");
        }

        dsc_by_id[id] = std::move(dsc);

        // dsc cannot be used any longer, it was transferred/moved to the dictionaries above
        // assert(!dsc); (ok, linter is detecting this)
    }
}


void DescriptorSet::zeros(IOBase& io, const Extent& ext) {
    const auto blk_sz_order = sg_alloc_dsc.blkarr().blk_sz_order();
    io.seek_wr(ext.blk_nr() << blk_sz_order);
    io.fill(0, ext.blk_cnt() << blk_sz_order);
}


// TODO: we write the descriptors in arbitrary order so this is not very cache-friendly
// An optimization is possible. Moreover, the space of deleted/shrank descriptors are zero'd
// regardless if the same space is overwritten later by a new/grew descriptor.
// If somehow sort all by block nr we could fix these two issues at once (but it is way tricky!)
void DescriptorSet::_write_modified_descriptors(IOBase& io, SegmentAllocator& sg_alloc_edata) {
    // Find any descriptor that shrank and it will require less space so we
    // can "split" the space and free a part.
    // Also, find any descriptor that grew so we remove it
    // and we re-add it later
    const auto blk_sz_order = sg_alloc_dsc.blkarr().blk_sz_order();
    for (const auto dsc: to_update) {
        auto cur_dsc_sz = dsc->calc_struct_footprint_size();
        auto alloc_dsc_sz = uint32_t(dsc->ext.blk_cnt()) << blk_sz_order;  // TODO byte_to_blk and blk_to_byte

        if (alloc_dsc_sz < cur_dsc_sz) {
            // grew, dealloc its current space and add it to the "to add" set
            //
            // note: we could try to expand the current extent and enlarge it in-place
            // but it is not supported by now and probably it would interfer with
            // the SegmentAllocator's split_above_threshold and it may lead to external
            // fragmentation inside the set. It is better a all-in-one "compactation" solution
            zeros(io, dsc->ext);
            sg_alloc_dsc.dealloc_single_extent(dsc->ext);
            dsc->ext = Extent::EmptyExtent();

            // We add this desc to the to_add set but we don't remove it from
            // to_update.
            // It is OK, we will merge to_add and to_update sets later and
            // we will remove duplicated in the process.
            to_add.insert(dsc);

        } else if (alloc_dsc_sz > cur_dsc_sz) {
            // shrank, split and dealloc the unused part
            // Note: this split works because the descriptors sizes are a multiple
            // of the block size of the stream. By the RFC, this is a multiple of 2 bytes.
            assert(cur_dsc_sz % sg_alloc_dsc.blkarr().blk_sz() == 0);
            assert((cur_dsc_sz >> blk_sz_order) <= uint16_t(-1));

            auto ext2 = dsc->ext.split(uint16_t(cur_dsc_sz >> blk_sz_order));

            zeros(io, ext2);
            sg_alloc_dsc.dealloc_single_extent(ext2);
        }
    }

    // Delete the descriptors that we don't want
    for (const auto dsc: to_remove) {
        zeros(io, dsc->ext);
        sg_alloc_dsc.dealloc_single_extent(dsc->ext);

        // Dealloc the content being pointed (descriptor's external data)
        if (dsc->hdr.own_edata) {
            sg_alloc_edata.dealloc(dsc->hdr.segm);
        }

        delete dsc;
    }
    to_remove.clear();

    // NOTE: at this moment we could compute how much was freed, how much is
    // about to be added and how much is already present in-disk (to_update and things not in to_update).
    // If the criteria match we could do a compaction *before* allocating new space (so it is more efficient)
    // This however would require update the descriptors' extents to their new positions
    // NOTE: compaction is not implemented yet

    // Alloc space for the new descriptors but do not write anything yet
    for (const auto dsc: to_add) {
        dsc->ext = sg_alloc_dsc.alloc_single_extent(dsc->calc_struct_footprint_size());
    }

    // Add all the "new" descriptors to the "to update" list now that they
    // have space allocated in the stream
    // This will remove any duplicated descriptor between the two sets.
    to_update.insert(to_add.begin(), to_add.end());
    to_add.clear();

    for (const auto dsc: to_update) {
        auto pos = dsc->ext.blk_nr() << blk_sz_order;

        io.seek_wr(pos);
        dsc->write_struct_into(io);
    }
    to_update.clear();


    // TODO compute checksum


    // TODO we may free blocks from the stream, but always?
    sg_alloc_dsc.release();
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
