#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "xoz/alloc/segm_allocator.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

class IDManager;

class DescriptorSet {
private:
    std::map<uint32_t, std::shared_ptr<Descriptor>> dsc_by_id;


    std::set<Descriptor*> present;


    // Descriptors owned by this DescriptorSet. Owned means that the descriptors
    // belongs to this set and to no other one. They may or no be present in the XOZ
    // file at the moment.
    std::set<Descriptor*> owned;

    // The owned set is subdivided into 3 subsets:
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
    std::set<Descriptor*> to_add;
    std::set<Descriptor*> to_remove;
    std::set<Descriptor*> to_update;

    SegmentAllocator sg_alloc_dsc;

public:
    explicit DescriptorSet(BlockArray& blkarr);

    void load_descriptors(IOBase& io, IDManager& idmgr);

    /*
    void write_modified_descriptors(IOBase& io);
    void write_all_descriptors(IOBase& io);
    */

    /*
    void add(std::shared_ptr<Descriptor> dscptr) {
        if (!dscptr) {
            throw; // TODO
        }

        // TODO ask the object if it belongs to another stream
        if dscptr.owner() != this and != nullptr
            err // the user must explicitly remove the object from its current stream and only then add it to this one

        // TODO we are using set<Descriptor*> and comparing pointers
        // This will break if
        //  - objects are moved ==> disable it
        //  - two objects (2 addresses) have the same obj_id
        //


        if dscptr.obj_id == 0
            err // this shoudl not happen: new objects should have a valid id from the Repository

        auto dsc = dscptr.get();

        owned.insert(dsc);
        dsc.set_owner(this);

        to_add.insert(dsc);
        to_delete.erase(dsc);
    }

    void remove(std::shared_ptr<Descriptor> dscptr) {
        if (!dscptr) {
            throw
        }

        if (not owned.contains(dsc)) {
            return; // or fail?
        }

        auto dsc = dscptr.get();

        to_add.erase(dsc);
        to_update.erase(dsc);

        to_delete.insert(dsc);

        dsc.set_owner(nullptr);
        owned.erase(dsc);
    }

    void mark_as_modified(std::shared_ptr<Descriptor> dscptr) {
        if (!dscptr) {
            throw
        }

        auto dsc = dscptr.get();

        if (not owned.contains(obj)) {
            return; // or fail? or add?
        }

        if (to_delete.contains(dsc)) {
            return; // or fail? or add?
        }


        to_update.insert(dsc);
    }
*/
private:
    void _write_modified_descriptors(IOBase& io, SegmentAllocator& sg_alloc_edata);
    void zeros(IOBase& io, const Extent& ext);
};
