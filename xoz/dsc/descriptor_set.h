#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

class IDManager;

class DescriptorSet {
private:
    // Descriptors owned by this DescriptorSet. Owned means that the descriptors
    // belongs to this set and to no other one. They may or no be present in the XOZ
    // file at the moment.
    std::map<uint32_t, std::shared_ptr<Descriptor>> owned;

    // The owned set of descriptors is subdivided into 3 subsets:
    //
    //  - to_add: for descriptors that are not present in the XOZ file but they should be
    //  - to_remove: for descriptors that are present in the XOZ file but they shouldn't be
    //  - to_update: for descriptors that are present in the XOZ file but changed and
    //               such change is not being reflected by the file and it should be
    //
    // Descriptors owned but not present in any of those 3 subsets are the descriptors
    // that are present in the file and that they didn't change in memory.
    //
    // Descriptors in the to_remove set may not be present in the owned set. This happen
    // when a descriptor owned by a set is moved to another set so the former looses
    // the ownership but the descriptor still needs to be added to to_remove so it is cleaned
    // in the XOZ file.
    // To avoid dangling pointers, the to_remove is a set of shared pointers and not raw pointers.
    std::set<Descriptor*> to_add;
    std::set<std::shared_ptr<Descriptor>> to_remove;
    std::set<Descriptor*> to_update;

    Segment& segm;
    BlockArray& dblkarr;
    BlockArray& eblkarr;

    IDManager& idmgr;

public:
    /*
     * The segment is where the descriptor set lives. It must be a segment
     * from the dblkarr. Changes to the segment are possible due the addition
     * and remotion of descriptors.
     *
     * For descriptors that own external data, the descriptor set will remove
     * data blocks from eblkarr when the descriptor is removed from the set
     * (and it was not moved to another set).
     *
     * Writes/additions/deletions of the external data blocks are made by
     * the descriptors and not handled by the set.
     **/
    DescriptorSet(Segment& segm, BlockArray& dblkarr, BlockArray& eblkarr, IDManager& idmgr);

    void load_set();
    void write_set();

    uint32_t count() const {
        auto cnt = owned.size();
        return assert_u32(cnt);
    }

    /*
    void write_modified_descriptors(IOBase& io);
    void write_all_descriptors(IOBase& io);
    */

    /*
     * Add the given descriptor to the set. If the descriptor already belongs
     * to another set, it will throw: user must call 'move_out()' on the other
     * set to move the descriptor to this one.
     *
     * If the descriptor has not a valid id, a new temporal one is assigned.
     *
     * While the addition to the set takes place immediately, the XOZ file
     * will not reflect this until the descriptor set is written to.
     * */
    void add(std::unique_ptr<Descriptor> dscptr);

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
     * If the descriptor does not belong to the set, throw.
     * */
    void erase(uint32_t id);

    /*
     * Mark the descriptor as modified so it is rewritten in the XOZ file
     * when the set is written.
     *
     * If the descriptor does not belong to the set, throw.
     * */
    void mark_as_modified(uint32_t id);

private:
    void load_descriptors(IOBase& io);
    void write_modified_descriptors(IOBase& io);

    void add_s(std::shared_ptr<Descriptor> dscptr);
    void zeros(IOBase& io, const Extent& ext);

    DescriptorSet(const DescriptorSet&) = delete;
    DescriptorSet& operator=(const DescriptorSet&) = delete;

    // The descriptors have a pointer to the descriptor set
    // that owns them so we cannot allow move semantics
    // otherwise we cannot guarantee pointer stability.
    DescriptorSet(DescriptorSet&&) = delete;
    DescriptorSet& operator=(DescriptorSet&&) = delete;

    void impl_remove(std::shared_ptr<Descriptor> dscptr, DescriptorSet* new_home);
};
