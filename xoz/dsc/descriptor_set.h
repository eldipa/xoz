#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/blk/segment_block_array.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/internals.h"
#include "xoz/io/iobase.h"

namespace xoz {
class RuntimeContext;

class DescriptorSet: public Descriptor {
private:
    struct by_id {
        constexpr bool operator()(Descriptor* const& lhs, Descriptor* const& rhs) const {
            return lhs->id() < rhs->id();
        }
    };

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
    std::set<Descriptor*, by_id> to_add;
    std::set<Descriptor*, by_id> to_update;

    // Descriptors in <to_remove> are not owned by the set (this) but they *were* moments ago.
    // The remotion could happen if the descriptor was explicitly deleted (erase) or it is was moved
    // to another set. In the first case, the current set was the last owner and the data blocks owned
    // by the removed descriptor were removed (see erase()); in the second case, the descriptor
    // is still alive (in another set) so no data block was deleted.
    std::set<Extent, Extent::Compare> to_remove;
    std::set<std::shared_ptr<Descriptor>> to_destroy;

    /*
     * These are the sub-descriptor-sets owned by this.
     * */
    std::set<DescriptorSet*, by_id> children;
    bool visited;

    /*
     * <segm> is the segment that holds the descriptors of this set. The segment points to blocks
     * in the <sg_blkarr> block array that contains the header of the set and the descriptors
     * of the set. These descriptors may point to "content" data blocks in
     * the <cblkarr> blocks array.
     *
     * The <st_blkarr> is the block array constructed from the segment and <sg_blkarr> used to alloc/dealloc
     * descriptors. We use <st_blkarr> to track the allocated space within the space pointed
     * by the segment.
     * For reading/writing descriptors (including padding), we use <sg_blkarr> directly.
     * See load_descriptors / write_modified_descriptors.
     *
     *                                   <st_blkarr> of 2 bytes blks
     *        <segm>                     <sg_blkarr> of N bytes blks      <cblkarr> of M bytes blks
     *   segment of the set                segment-pointed blocks          "content" data blocks
     *         +--+                              +-------+                      +--------+
     *         |  |                              |       |                      |        |
     *       extents  --------------------->    descriptors  ---------------->     data
     *         |  |                              |       |                      |        |
     *         +--+                              +-------+                      +--------+
     *
     * Note: currently both sg_blkarr and cblkarr point to the *same* block array.
     * */
    Segment dset_segm;
    BlockArray& sg_blkarr;
    BlockArray& cblkarr;
    SegmentBlockArray st_blkarr;

    RuntimeContext& rctx;

    bool set_loaded;

    /*
     * Private data (and its size).
     * Used to preserve fields from future versions of xoz.
     * */
    std::vector<char> pdata;

    uint16_t ireserved;  // see MASK_DSET_IRESERVED
    uint16_t creserved;

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

protected:
    enum Streams : uint16_t {
        Main = 0,
        END  // this *must* be the last entry
    };

public:
    constexpr static uint16_t TYPE = 0x0001;

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
     * empty.
     *
     * For descriptors that own content data, the descriptor set will remove
     * data blocks from cblkarr when the descriptor is removed from the set
     * (and it was not moved to another set).
     *
     * Writes/additions/deletions of the content of these data blocks are made by
     * the descriptors and not handled by the set.
     **/
    static std::unique_ptr<DescriptorSet> create(const Segment& segm, BlockArray& cblkarr, RuntimeContext& rctx);
    static std::unique_ptr<DescriptorSet> create(BlockArray& cblkarr, RuntimeContext& rctx);

    DescriptorSet(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, RuntimeContext& rctx);
    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
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
     * will try to avoid any write if there are no changes.
     * */
    void full_sync(const bool release) override;

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
     * Like count() but count only for subsets (DescriptorSet that live in the current set).
     * */
    uint32_t count_subset() const {
        fail_if_set_not_loaded();
        auto cnt = children.size();
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
     * Note: if fail_if_not_allowed_to_add() fails it 99% likely due a bug in the Application
     * */
    uint32_t add(std::unique_ptr<Descriptor> dscptr, bool assign_persistent_id = false);

    /*
     * Create a new descriptor of type T with the given arguments (Args) calling
     * the class method T::create. The first argument of T::create must be
     * a reference to BlockArray (create_and_add() will pass the set's cblkarr)
     *
     * Then, add the descriptor to the set and return a shared pointer to it.
     * */
    template <typename T, typename... Args>
    std::shared_ptr<T> create_and_add(bool assign_persistent_id, Args... args) {
        static_assert(std::is_base_of_v<Descriptor, T> == true);
        auto uptr = T::create(std::ref(cblkarr), args...);
        uint32_t id = add(std::move(uptr), assign_persistent_id);
        return get<T>(id);
    }

    template <typename T, typename... Args>
    std::shared_ptr<T> create_and_add_dset(bool assign_persistent_id, Args... args) {
        static_assert(std::is_base_of_v<DescriptorSet, T> == true);
        auto uptr = T::create(std::ref(cblkarr), std::ref(rctx), args...);
        uint32_t id = add(std::move(uptr), assign_persistent_id);
        return get<T>(id);
    }

    /*
     * Move out the descriptor from this set and move it into the "new home" set.
     * If the descriptor does not belong to this set, throw.
     * The method will call add() on the other "new" set to ensure that the descriptor
     * has an owner.
     *
     * When the descriptor set is written to the XOZ file, the deletion
     * of the descriptor from this set will take place. Because it is a move,
     * no descriptor's content data block is touch (the "new home" set will be responsible
     * of handling that).
     * */
    void move_out(uint32_t id, DescriptorSet& new_home);
    void move_out(uint32_t id, std::unique_ptr<DescriptorSet>& new_home);

    /*
     * Erase the descriptor, including the deletion of its content data blocks (if any).
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
     *
     * If the descriptor with id (parameter) is not found, raise an error.
     * If the descriptor has a persistent id (this means that the parameter is
     * a persistent id), do nothing (except possibly check that the persistent
     * id is registered).
     * Otherwise, request a new persistent id and assign it to the descriptor.
     *
     * Return the new descriptor id.
     * */
    uint32_t assign_persistent_id(uint32_t id);

    /*
     * Return a reference to the descriptor. If changes are made to it,
     * either the Descriptor subclass or the user must call mark_as_modified
     * to let the DescriptorSet know about the changes.
     *
     * If no descriptor has the given id, this method throws.
     *
     * Note: the application may opt to keep a copy of the id and call get()
     * to get the descriptor as many times as it wants *or* to do it once
     * and keep the shared pointer.
     *
     * Both are fine: xoz ensures that while the xoz file is open, the ids
     * (persistent or temporal) and the shared pointer will remain pointing
     * to the correct descriptor.
     *
     * If the application wants to access very frequently to the descriptor,
     * keeping a shared pointer may be faster than calling get() every time.
     * The downside is that a shared pointer occupies more space and may
     * prevent xoz to do optimizations on descriptor that are otherwise inaccesible.
     *
     * Keeping an id may require less space at the expenses of a lookup.
     * The advantage may disappear if the application needs to keep a shared pointer
     * to the set needed to do the lookup.
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

    /*
     * Return if the set has a descriptor with the given id or not.
     * */
    bool contains(uint32_t id) const;

    typedef xoz::dsc::internals::DescriptorIterator<std::map<uint32_t, std::shared_ptr<Descriptor>>::iterator>
            dsc_iterator_t;
    typedef xoz::dsc::internals::DescriptorIterator<std::map<uint32_t, std::shared_ptr<Descriptor>>::const_iterator>
            const_dsc_iterator_t;

    inline dsc_iterator_t begin() { return xoz::dsc::internals::DescriptorIterator(owned.begin()); }
    inline dsc_iterator_t end() { return xoz::dsc::internals::DescriptorIterator(owned.end()); }

    inline const_dsc_iterator_t cbegin() const { return xoz::dsc::internals::DescriptorIterator(owned.cbegin()); }
    inline const_dsc_iterator_t cend() const { return xoz::dsc::internals::DescriptorIterator(owned.cend()); }

    /*
     * Travel through the links between descriptor sets and call
     * the given function fn on each set in a depth-first order.
     *
     * If the function fn returns a boolean, it if it returns true,
     * the iteration stops immediately.
     *
     * The function must receive two arguments:
     *  - DescriptorSet* :  the set to process
     *  - size_t : the level in the stack
     *
     * top_down_for_each_set() and bottom_up_for_each_set() are the same
     * except that the first iterates in pre-order will the second in post-order.
     * */
    template <class Fn>
    static bool bottom_up_for_each_set(DescriptorSet& root, Fn fn) {
        return _depth_first_for_each_set_adapter<Fn, true>(root, fn);
    }

    template <class Fn>
    static bool top_down_for_each_set(DescriptorSet& root, Fn fn) {
        return _depth_first_for_each_set_adapter<Fn, false>(root, fn);
    }

    Segment segment() const { return dset_segm; }

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
    void load_descriptors(std::queue<DescriptorSet*>& to_load_dsets);
    void write_modified_descriptors(IOBase& io);

    void add_s(std::shared_ptr<Descriptor> dscptr, bool assign_persistent_id);
    void zeros(IOBase& io, const Extent& ext);

    void impl_remove(std::shared_ptr<Descriptor>& dscptr, bool moved);

    std::shared_ptr<Descriptor> get_owned_dsc_or_fail(uint32_t id);

protected:
    void update_isize(uint64_t& isize) override;
    void update_content_parts(std::vector<struct Descriptor::content_part_t>& cparts) override;

private:
    void flush_writes_no_recursive(const bool release);
    void release_free_space_no_recursive();
    void full_sync_no_recursive(const bool release);
    void clear_set_no_recursive();
    void destroy_no_recursive();

    // Override these only to make them fail.
    // Callers (including Descriptor parent class) should not call them
    // and instead they should call full_sync().
    void flush_writes() override;
    void release_free_space() override;

public:
    /*
     * Load the set into memory. This must be called once to initialize the internal allocator
     * properly.
     * */
    void /* internal */ load_set();
    BlockArray& /* internal - for testing */ expose_block_array() { return cblkarr; }

    /*
     * Check if there is any change pending to be written (addition of new descriptors,
     * remotion, or update).
     * This is only for testing:
     * does_require_write may return False but a call to full_sync could modify a subdset
     * which in turn propagates changes to dset so it *does* require write (True)
     * In other words, it is hard to reason about does_require_write and it is not
     * super useful anyways
     * */
    bool /* testing */ does_require_write() const;

    uint64_t /* internal */ count_descriptors_external_references() const;

    uint16_t /* testing */ _get_ireserved() const;
    uint16_t /* testing */ _get_creserved() const;
    std::vector<char> /* testing */ _get_pdata() const;

    void /* testing */ _set_ireserved(uint16_t v);
    void /* testing */ _set_creserved(uint16_t v);
    void /* testing */ _set_pdata(const std::vector<char>& v);

    // Internal, for sub-classing only
    DescriptorSet(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, uint16_t decl_cpart_cnt,
                  RuntimeContext& rctx);
    DescriptorSet(const uint16_t TYPE, BlockArray& cblkarr, uint16_t decl_cpart_cnt, RuntimeContext& rctx);

    // This is for testing only
    DescriptorSet(const Segment& segm, BlockArray& cblkarr, RuntimeContext& rctx);

private:
    void fail_if_set_not_loaded() const;
    void fail_if_using_incorrect_blkarray(const Descriptor* dsc) const;
    void fail_if_null(const Descriptor* dsc) const;
    void fail_if_duplicated_id(const Descriptor* dsc) const;

    void chk_if_any_descriptor_has_external_references() const;
    void chk_if_descriptor_has_external_references(const std::shared_ptr<Descriptor>& dscptr) const;

private:
    template <class Fn, bool PostOrder>
    static bool _depth_first_for_each_set_adapter(DescriptorSet& root, Fn fn) {
        using ret_type = std::invoke_result_t<decltype(fn), DescriptorSet*, size_t>;
        if constexpr (std::is_same_v<ret_type, void>) {
            // Addapt void(DescriptorSet*, size_t) to bool(DescriptorSet*, size_t)
            auto adapted = [fn](DescriptorSet* s, size_t l) -> bool {
                fn(s, l);
                return false;
            };
            return _depth_first_for_each_set<decltype(adapted), PostOrder>(root, adapted);
        } else {
            return _depth_first_for_each_set<Fn, PostOrder>(root, fn);
        }
    }

    template <class Fn, bool PostOrder>
    static bool _depth_first_for_each_set(DescriptorSet& root, Fn fn) {
        std::deque<std::tuple<decltype(root.children.begin()), decltype(root.children.begin()), DescriptorSet*>>
                to_explore;

        if constexpr (not PostOrder) {
            if (fn(&root, 0)) {
                return true;
            }
        }

        to_explore.push_back({root.children.begin(), root.children.end(), &root});

        while (not to_explore.empty()) {
            auto [cur, end, parent_dset] = to_explore.back();
            if (cur == end) {
                if constexpr (PostOrder) {
                    if (fn(parent_dset, to_explore.size() - 1)) {
                        return true;
                    }
                }

                to_explore.pop_back();
                continue;
            }

            if constexpr (not PostOrder) {
                if (fn(*cur, to_explore.size())) {
                    return true;
                }
            }

            auto orig = cur++;
            to_explore.back() = {cur, end, parent_dset};
            to_explore.push_back({(*orig)->children.begin(), (*orig)->children.end(), (*orig)});
        }

        return false;
    }


protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
};
}  // namespace xoz
