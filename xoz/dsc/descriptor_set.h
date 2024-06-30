#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/blk/segment_block_array.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/internals.h"
#include "xoz/io/iobase.h"

class RuntimeContext;

class DescriptorSet: public Descriptor {
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
    std::set<std::shared_ptr<Descriptor>> to_destroy;

    /*
     * <segm> is the segment that holds the descriptors of this set. The segment points to blocks
     * in the <sg_blkarr> block array that contains the header of the set and the descriptors
     * of the set. These descriptors may point to "external" data blocks in
     * the <ed_blkarr> blocks array.
     *
     * The <st_blkarr> is the block array constructed from the segment and <sg_blkarr> used to alloc/dealloc
     * descriptors. We use <st_blkarr> to track the allocated space within the space pointed
     * by the segment.
     * For reading/writing descriptors (including padding), we use <sg_blkarr> directly.
     * See load_descriptors / write_modified_descriptors.
     *
     *                                   <st_blkarr> of 2 bytes blks
     *        <segm>                     <sg_blkarr> of N bytes blks      <ed_blkarr> of M bytes blks
     *   segment of the set                segment-pointed blocks          external data blocks
     *         +--+                              +-------+                      +--------+
     *         |  |                              |       |                      |        |
     *       extents  --------------------->    descriptors  ---------------->     data
     *         |  |                              |       |                      |        |
     *         +--+                              +-------+                      +--------+
     *
     * Note: currently both sg_blkarr and ed_blkarr point to the *same* block array.
     * */
    Segment segm;
    BlockArray& sg_blkarr;
    BlockArray& ed_blkarr;
    SegmentBlockArray st_blkarr;

    RuntimeContext& rctx;

    bool set_loaded;

    uint16_t reserved;  // u16data (get/set)
    uint32_t current_checksum;

    /*
     * Track if we changed the header and it requires a write
     *
     * note: the header may be written anyways even if this
     * variable is false. For example, if there were
     * changes in the descriptors (because we require an update
     * of the current_checksum).
     * */
    bool header_does_require_write;
    Extent header_ext;

public:
    /*
     * Create a descriptor set.
     * The segment is where the descriptor set lives. It must be a segment
     * from the sg_blkarr.
     *
     * The descriptor set will keep a private copy that it will mutate:
     * changes to the segment are possible due the addition
     * and remotion of descriptors to/from the set.
     * The caller may get an updated segment calling segment() method.
     *
     * If the segment is empty or it was not provided, the set is considered
     * empty and create_set() will be immediately invoked.
     * Otherwise, load_set() will be immediately invoked.
     *
     * For descriptors that own external data, the descriptor set will remove
     * data blocks from ed_blkarr when the descriptor is removed from the set
     * (and it was not moved to another set).
     *
     * Writes/additions/deletions of the content of these external data blocks are made by
     * the descriptors and not handled by the set.
     **/
    static std::unique_ptr<DescriptorSet> create(const Segment& segm, BlockArray& ed_blkarr, RuntimeContext& rctx);
    static std::unique_ptr<DescriptorSet> create(BlockArray& ed_blkarr, RuntimeContext& rctx);

    DescriptorSet(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr, RuntimeContext& rctx);
    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr,
                                              RuntimeContext& rctx);

    /*
     * Remove all the descriptors of the set but do not remove the set itself.
     * */
    void clear_set();

    /*
     * Remove all the descriptors *and* the set. Once called, the DescriptorSet instance
     * becomes invalid and it should be throw away.
     * */
    void destroy() override;

    /*
     * Flush any pending write to disk. This can be called multiple times: the implementation
     * will try to avoid any write if there are no changes to write.
     *
     * This method must be called at least once if the set or its descriptors
     * were modified. See does_require_write() method.
     * */
    void flush_writes() override;

    void release_free_space() override;

    void update_header() override;

    /*
     * Check if there is any change pending to be written (addition of new descriptors,
     * remotion, or update).
     * */
    bool does_require_write() const;

    /*
     * Count how many descriptors are owned by this set. This does not count
     * how many descriptors are already present in the XOZ file but it should
     * count that after a flush_writes() call.
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
     *
     * Note: the caller should call fail_if_not_allowed_to_add() on the descriptor
     * *before* calling add() to validate that the addition will not fail.
     * The method add() will do the same validations anyways but by the time
     * add() is running, the ownership of the descriptor via unique_ptr<> was already
     * transfered to the add() and in case of an error, the caller will have lost
     * its descriptor.
     * This is a limitation of C++'s unique_ptr<> that we cannot pass by reference
     * when the unique_ptr<> is pointing to a parent/base class (like Descriptor is).
     * Sorry.
     *
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
    void move_out(uint32_t id, std::unique_ptr<DescriptorSet>& new_home);

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
     *
     * If no descriptor has the given id, this method throws.
     * */
    std::shared_ptr<Descriptor> get(uint32_t id);


    template <typename T>
    std::shared_ptr<T> get(uint32_t id, bool ret_null = false) {
        auto ptr = std::dynamic_pointer_cast<T>(this->get(id));
        if (!ptr and not ret_null) {
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

    Segment segment() const { return segm; }

public:
    void fail_if_not_allowed_to_add(const Descriptor* dsc) const;

private:
    /*
     * No copy nor move constructors/assign operators
     * Make the descriptor set no-copyable and pointer-stable.
     * */
    DescriptorSet(const DescriptorSet&) = delete;
    DescriptorSet& operator=(const DescriptorSet&) = delete;

    /*
     * The descriptors have a pointer to the descriptor set
     * that owns them so we cannot allow move semantics
     * otherwise we cannot guarantee pointer stability.
     * */
    DescriptorSet(DescriptorSet&&) = delete;
    DescriptorSet& operator=(DescriptorSet&&) = delete;


private:
    void load_descriptors(const bool is_new, const uint16_t u16data);
    void write_modified_descriptors(IOBase& io);

    void add_s(std::shared_ptr<Descriptor> dscptr, bool assign_persistent_id);
    void zeros(IOBase& io, const Extent& ext);

    void impl_remove(std::shared_ptr<Descriptor>& dscptr, bool moved);

    std::shared_ptr<Descriptor> get_owned_dsc_or_fail(uint32_t id);

private:
    /*
     * Load the set into memory. This must be called once to initialize the internal allocator
     * properly.
     * */
    void load_set();

    /*
     * Create a new set (nothing is written to disk but there would be pending writes).
     * This must be called once.
     *
     * Note: u16data is an opaque argument to configure the set creation.
     * */
    void create_set(uint16_t u16data = 0);

public:
    uint16_t /* private */ u16data() const { return this->reserved; }

private:
    void fail_if_set_not_loaded() const;
    void fail_if_using_incorrect_blkarray(const Descriptor* dsc) const;
    void fail_if_null(const Descriptor* dsc) const;
    void fail_if_duplicated_id(const Descriptor* dsc) const;

private:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
};
