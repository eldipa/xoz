#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/blk/segment_block_array.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/internals.h"
#include "xoz/io/iobase.h"

class IDManager;

class DescriptorSet {
private:
    // Descriptors owned by this DescriptorSet. Owned means that the descriptors
    // belongs to this set and to no other one. They may or no be present in the XOZ
    // file at the moment.
    std::map<uint32_t, std::shared_ptr<Descriptor>> owned;

    // The owned descriptors can be classified into 3 subsets:
    //
    //  - to_add: for descriptors owned by the set that are not present in the XOZ file but they should be
    //  - to_update: for descriptors owned by the set that are present in the XOZ file but changed and
    //               such change is not being reflected by the file and it should be
    //  - the rest: descriptors owned and present in the XOZ file that don't require an update (not changed)
    //
    std::set<Descriptor*> to_add;
    std::set<Descriptor*> to_update;

    // Descriptors in the to_remove set are not owned by the set but they were moments ago.
    // The remotion can happen if the descriptor is explicitly deleted (erase) or it is moved
    // to another set. In the first case, the current set was the last owner and the external data blocks
    // are removed (see erase()); in the second case, the descriptor is still alive (in another set) so no external
    // data block is deleted.
    std::set<Extent, Extent::Compare> to_remove;

    // The segment that holds the descriptors of this set. The segment points to blocks
    // in the sg_blkarr block array while the descriptors may point to "external" data blocks in
    // the ed_blkarr blocks array.
    Segment& segm;
    BlockArray& sg_blkarr;
    BlockArray& ed_blkarr;

    // This is the block array constructed from the segment and sg_blkarr used to alloc/dealloc
    // descriptors.
    // For reading/writing descriptors (including padding), we use sg_blkarr directly.
    SegmentBlockArray st_blkarr;

    IDManager& idmgr;

    bool set_loaded;

public:
    /*
     * The segment is where the descriptor set lives. It must be a segment
     * from the sg_blkarr. Changes to the segment are possible due the addition
     * and remotion of descriptors.
     *
     * For descriptors that own external data, the descriptor set will remove
     * data blocks from ed_blkarr when the descriptor is removed from the set
     * (and it was not moved to another set).
     *
     * Writes/additions/deletions of the content of these external data blocks are made by
     * the descriptors and not handled by the set.
     **/
    DescriptorSet(Segment& segm, BlockArray& sg_blkarr, BlockArray& ed_blkarr, IDManager& idmgr);

    /*
     * Load the set into memory. This must be called once to initialize the internal allocator
     * properly.
     * */
    void load_set();

    /*
     * Write the set to disk. This can be called multiple times: the implementation
     * will try to avoid any write if there are no changes to write.
     *
     * This method must be called at least once if the set or its descriptors
     * were modified. See does_require_write() method.
     * */
    void write_set();

    /*
     * Check if there is any change pending to be written (addition of new descriptors,
     * remotion, or update).
     * */
    bool does_require_write() const;

    /*
     * Count how many descriptors are owned by this set. This does not count
     * how many descriptors are already present in the XOZ file but it should
     * count that after a write_set() call.
     * */
    uint32_t count() const {
        fail_if_set_not_loaded();
        auto cnt = owned.size();
        return assert_u32(cnt);
    }

    /*
     * Add the given descriptor to the set. If the descriptor already belongs
     * to another set, it will throw: user must call 'move_out()' on the other
     * set to move the descriptor to this one.
     *
     * If the descriptor has not a valid id, a new temporal one is assigned.
     *
     * While the addition to the set takes place immediately, the XOZ file
     * will not reflect this until the descriptor set is written to.
     *
     * Return the id of the descriptor added.
     * */
    uint32_t add(std::unique_ptr<Descriptor> dscptr, bool assign_persistent_id = false);

    /*
     * Move out the descriptor from this set and move it into the "new home" set.
     * If the descriptor does not belong to this set, throw.
     * The method will call add() on the other "new" set to ensure that the descriptor
     * has an owner.
     *
     * When the descriptor set is written to the XOZ file, the deletion
     * of the descriptor from this set will take place. Because it is a move,
     * no external data block is touch (the "new home" set will be responsible
     * of handling that).
     * */
    void move_out(uint32_t id, DescriptorSet& new_home);

    /*
     * Erase the descriptor, including the deletion of its external data blocks (if any).
     * The erase takes place when the set is written to the file.
     *
     * If the descriptor does not belong to the set, throw. An erased descriptor
     * cannot be neither marked as modified, moved out nor added again.
     * */
    void erase(uint32_t id);

    /*
     * Mark the descriptor as modified so it is rewritten in the XOZ file
     * when the set is written.
     *
     * If the descriptor does not belong to the set, throw.
     * */
    void mark_as_modified(uint32_t id);

    /*
     * Assign a persistent id to the given descriptor and return the new id.
     * (the old id become useless).
     * */
    uint32_t assign_persistent_id(uint32_t id);

    /*
     * Return a reference to the descriptor. If changes are made to it,
     * either the Descriptor subclass or the user must call mark_as_modified
     * to let the DescriptorSet know about the changes.
     * */
    std::shared_ptr<Descriptor> get(uint32_t id);


    template <typename T>
    std::shared_ptr<T> get(uint32_t id) {
        auto ptr = std::dynamic_pointer_cast<T>(this->get(id));
        if (!ptr) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }

        return ptr;
    }

    typedef xoz::dsc::internals::DescriptorIterator<std::map<uint32_t, std::shared_ptr<Descriptor>>::iterator>
            dsc_iterator_t;
    typedef xoz::dsc::internals::DescriptorIterator<std::map<uint32_t, std::shared_ptr<Descriptor>>::const_iterator>
            const_dsc_iterator_t;

    inline dsc_iterator_t begin() { return xoz::dsc::internals::DescriptorIterator(owned.begin()); }
    inline dsc_iterator_t end() { return xoz::dsc::internals::DescriptorIterator(owned.end()); }

    inline const_dsc_iterator_t cbegin() const { return xoz::dsc::internals::DescriptorIterator(owned.cbegin()); }
    inline const_dsc_iterator_t cend() const { return xoz::dsc::internals::DescriptorIterator(owned.cend()); }


    void /* internal */ release_free_space();

private:
    void load_descriptors(IOBase& io);
    void write_modified_descriptors(IOBase& io);

    void add_s(std::shared_ptr<Descriptor> dscptr, bool assign_persistent_id);
    void zeros(IOBase& io, const Extent& ext);

    DescriptorSet(const DescriptorSet&) = delete;
    DescriptorSet& operator=(const DescriptorSet&) = delete;

    // The descriptors have a pointer to the descriptor set
    // that owns them so we cannot allow move semantics
    // otherwise we cannot guarantee pointer stability.
    DescriptorSet(DescriptorSet&&) = delete;
    DescriptorSet& operator=(DescriptorSet&&) = delete;

    std::shared_ptr<Descriptor> impl_remove(uint32_t id, bool moved);

    void fail_if_set_not_loaded() const;
    std::shared_ptr<Descriptor> get_owned_dsc_or_fail(uint32_t id);
};
