#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include "xoz/ext/extent.h"
#include "xoz/io/iobase.h"
#include "xoz/io/iosegment.h"
#include "xoz/segm/segment.h"

namespace xoz {
class RuntimeContext;
class DescriptorSet;
class BlockArray;

class File;

namespace dsc::internals {
class DescriptorInnerSpyForTesting;
class DescriptorInnerSpyForInternal;
}  // namespace dsc::internals

class Descriptor {
public:
    struct content_part_t {
        struct {
            bool pending : 1;
            uint32_t future_csize : 31;  // in bytes
        } s;
        uint32_t csize;  // in bytes (31 bits only, MSB is unused)
        Segment segm;    // data segment
    };

    struct header_t {
        uint16_t type;

        uint32_t id;

        uint8_t isize;  // in bytes

        std::vector<struct content_part_t> cparts;
    };

    class Content {
    public:
        Content(Descriptor& dsc, struct Descriptor::content_part_t& cpart): dsc(dsc), cpart(cpart) {}

        [[nodiscard]] inline IOSegment get_io() { return dsc.get_content_part_io(cpart); }

        inline void resize(uint32_t content_new_sz) { return dsc.resize_content_part(cpart, content_new_sz); }

        [[nodiscard]] inline uint32_t size() const { return assert_u32_sub_nonneg(cpart.csize, cpart.s.future_csize); }

        inline void set_pending() {
            cpart.s.pending = true;
            dsc.notify_descriptor_changed();
        }
        inline void unset_pending() { cpart.s.pending = false; }
        [[nodiscard]] inline bool is_pending() const { return cpart.s.pending; }

    private:
        Descriptor& dsc;
        struct Descriptor::content_part_t& cpart;
    };

public:
    /*
     * Callers should call update_header() before calling write_struct_into()
     * to ensure that the content of the descriptor is updated so any calculation
     * of its size will be correct.
     * */
    static std::unique_ptr<Descriptor> load_struct_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr);
    void write_struct_into(IOBase& io, RuntimeContext& rctx);

    static std::unique_ptr<Descriptor> load_struct_from(IOBase&& io, RuntimeContext& rctx, BlockArray& cblkarr) {
        return load_struct_from(io, rctx, cblkarr);
    }
    void write_struct_into(IOBase&& io, RuntimeContext& rctx) { return write_struct_into(io, rctx); }

    /*
     * Load the header of the descriptor but do not create a Descriptors class or call any
     * subclass-specific loading.
     * The computed checksum *does* include the descriptor's internal data (isize) however.
     *
     * The read pointer of io is left at the begin of the descriptor's internal data.
     * */
    static struct header_t load_header_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr, bool& ex_type_used,
                                            uint32_t* checksum);

    /*
     * Call the virtual method flush_writes() and update_header()
     * and optionally release_free_space().
     * */
    virtual void full_sync(const bool release) {
        flush_writes();
        if (release) {
            release_free_space();
        }
        update_header();
    }

    void collect_segments_into(std::list<Segment>& collection) const;

private:
    // Return the size in bytes to represent the Descriptor structure in disk
    // *including* the descriptor internal data
    uint32_t calc_struct_footprint_size() const;

public:
    virtual ~Descriptor() {}

    friend void PrintTo(const Descriptor& dsc, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const Descriptor& dsc);

    [[nodiscard]] constexpr inline uint32_t id() const { return hdr.id; }
    void id(uint32_t new_id) { hdr.id = new_id; }


    /*
     * Dynamically downcast the current Descriptor (<this> instance pointer)
     * to the given concrete subclass T.
     *
     * If the cast works, return a pointer to this casted to T.
     * If the cast fails, throw an exception (if ret_null is false) or return
     * nullptr (if ret_null is true).
     *
     * While expensive, cast<T>(true) can be used to check the type of a descriptor.
     * The methods may_cast<T>() are an alias of cast<T>(true)
     * */
    template <typename T>
    T* cast(bool ret_null = false) const {
        auto ptr = dynamic_cast<T*>(this);
        if (!ptr and not ret_null) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }

        return ptr;
    }

    template <typename T>
    T* cast(bool ret_null = false) {
        // TODO implement this method in terms of its const version
        auto ptr = dynamic_cast<T*>(this);
        if (!ptr and not ret_null) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }

        return ptr;
    }

    template <typename T>
    T* may_cast() const {
        return this->cast<T>(true);
    }

    template <typename T>
    T* may_cast() {
        return this->cast<T>(true);
    }

    /*
     * Dynamically downcast a unique pointer to Descriptor (<base_ptr>)
     * to an unique pointer to the given concrete subclass T.
     *
     * Once cast() returns, the former base_ptr loose the ownership of the Descriptor
     * and such will be owned by the returned unique pointer.
     *
     * See the instance method cast<T>() documentation about the ret_null parameter.
     * Regardless of the value of ret_null, if the provided base_ptr is null, raise
     * an exception.
     * */
    template <typename T>
    static std::unique_ptr<T> cast(std::unique_ptr<Descriptor>& base_ptr, bool ret_null = false) {
        if (!base_ptr) {
            throw std::runtime_error("Pointer to descriptor (base class) cannot be null.");
        }

        // Down cast. The cast<T> method should fail if ret_null is false and something fails.
        auto subcls_ptr_raw = base_ptr->cast<T>(ret_null);

        // If the raw pointer to the subclass is null return a shared_ptr to null.
        // This could happen only if the base_ptr does not point to a T instance and ret_null
        // is true. Otherwise, we should never get a null ptr from the cast<T>() call above.
        if (!subcls_ptr_raw) {
            return std::unique_ptr<T>(nullptr);
        }

        // Take ownership of the subclass instance with a unique_ptr to a Subclass
        // and release the ownership from the unique_ptr to a Base class.
        auto subcls_ptr = std::unique_ptr<T>(subcls_ptr_raw);
        base_ptr.release();

        return subcls_ptr;
    }

    /*
     * Dynamically downcast a shared pointer to Descriptor (<base_ptr>)
     * to a shared pointer to the given concrete subclass T.
     *
     * The former base_ptr does *not* loose the shared ownership of the Descriptor
     * so once cast() returns, both base_ptr and the returned pointer points to the
     * same Descriptor instance.
     * */
    template <typename T>
    static std::shared_ptr<T> cast(const std::shared_ptr<Descriptor>& base_ptr, bool ret_null = false) {
        if (!base_ptr) {
            throw std::runtime_error("Pointer to descriptor (base class) cannot be null.");
        }

        // To keep the shared ownership between the base_ptr and the returned pointer,
        // we must use dynamic_pointer_cast.
        // Creating a shared_ptr<T> from the raw pointer of base_ptr will *not* make
        // the trick.
        std::shared_ptr<T> ret = std::dynamic_pointer_cast<T, Descriptor>(base_ptr);
        if (not ret and not ret_null) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }
        return ret;
    }

    /*
     * Return true if the concrete object is of type DescriptorSet or
     * a subclass of it, otherwise return false.
     * */
    bool is_descriptor_set() const;

public:  // public but it should be interpreted as an opaque section
    friend void PrintTo(const struct header_t& hdr, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const struct header_t& hdr);

    friend class DescriptorSet;
    friend class File;

    friend class ::xoz::dsc::internals::DescriptorInnerSpyForTesting;
    friend class ::xoz::dsc::internals::DescriptorInnerSpyForInternal;

private:
    struct header_t hdr;

    // This is how many content parts are declared by the Descriptor subclass
    // We may find less or more when loading from disk/file:
    //  - if we find less it means that the subclass may have an older version
    //    with less content parts but the new version wants (declared) more
    //    Non-existing content parts should be created as empty to match decl_cpart_cnt.
    //  - if we find more it means that the subclass may have an newer version
    //    the declared more parts than the current (old) one.
    //    The first decl_cpart_cnt parts will be usable and the remaining
    //    (unexpected) will be hidden but preserved.
    uint16_t decl_cpart_cnt;

protected:
    /*
     * Descriptor's constructor with its header and a reference to the block array
     * for content data.
     * The constructor is meant for internal use and its subclasses because it exposes
     * too much its header.
     * */
    Descriptor(const struct header_t& hdr, BlockArray& cblkarr, uint16_t decl_cpart_cnt);
    Descriptor(const uint16_t type, BlockArray& cblkarr, uint16_t decl_cpart_cnt);

    static void chk_hdr_isize_fit_or_fail(bool has_id, const struct Descriptor::header_t& hdr);

    /* Subclasses must override these methods to read/write specific data
     * from/into the iobase (xoz file) where the read/write pointer of io object
     * is immediately after the descriptor (common) header.
     *
     * See load_struct_from and write_struct_into methods for more context.
     *
     * Subclasses must *not* use the allocator of the cblkarr during the read
     * because it may not be enabled by the moment. Subclasses can use the cblkarr
     * for reading/writing without problem.
     *
     * Note: calling get_content_part_io() from read_struct_from() is *not* allowed.
     * You can but it is undefined.
     *
     * Subclasses must *not* assume than the size of the io is the size of
     * subclasses' content: subclasses *must* encode their content size
     * in either the idata or at the begin of the content. TODO eventually change this
     *
     * Note: after read_struct_specifics_from() finishes, the update_sizes() should
     * return valid and meaningful sizes.
     * */
    virtual void read_struct_specifics_from(IOBase& io) = 0;
    virtual void write_struct_specifics_into(IOBase& io) = 0;
    void read_struct_specifics_from(IOBase&& io) { read_struct_specifics_from(io); }
    void write_struct_specifics_into(IOBase&& io) { write_struct_specifics_into(io); }

    /*
     * Method called once all the descriptors were loaded.
     * Use this to access freely to other descriptors via finding them
     * from the root of the sets.
     * */
    virtual void on_after_load([[maybe_unused]] std::shared_ptr<DescriptorSet> root) {}

    /*
     * Subclasses must to do any deallocation and clean up because the descriptor
     * is about to be removed (destroyed).
     *
     * By default this method dealloc any allocated block (content) in the block array
     * if the descriptor owns any.
     * */
    virtual void destroy();

    constexpr static inline bool is_id_temporal(const uint32_t id) { return bool(id & 0x80000000); }

    constexpr static inline bool is_id_persistent(const uint32_t id) { return not Descriptor::is_id_temporal(id); }

    /*
     * Return if the given isize/csize for the present version of the descriptor fits
     * or not into the header.
     * These methods take into account the isize/csize for any future version of
     * the descriptor.
     * */
    bool does_present_isize_fit(uint64_t present_isize) const;
    bool does_present_csize_fit(const struct content_part_t& cpart, uint64_t present_csize) const;

    /*
     * Subclasses *must* call this method to notify that the instance had been modified
     * (aka, something changed in their this->hdr).
     *
     * Subclasses does not need to notify about changes on their content **unless**
     * this involves changes in the inline-data of the segment that owns the content.
     * If the segment has no inline-data or that part was not modified, changes
     * in the rest of the content (rest of the blocks pointed by the segment) don't
     * need to be notified as they are not written in the descriptor.
     *
     * Changes on the id() (this->hdr.id) must *not* be notified. The caller
     * should make such change via an operation on DescriptorSet that owns
     * this descriptor so the sets knows about the change directly from the caller
     * and not from us.
     * */
    void notify_descriptor_changed();

private:
    static std::vector<struct content_part_t> reserve_content_part_vec(uint16_t content_part_cnt);
    static uint32_t read_content_parts(IOBase& io, BlockArray& cblkarr, std::vector<struct content_part_t>& parts);
    uint32_t write_content_parts(IOBase& io, const std::vector<struct content_part_t>& parts, int cparts_cnt);

    void read_future_idata(IOBase& io);
    void write_future_idata(IOBase& io) const;
    uint8_t future_idata_size() const;

    void compute_future_content_parts_sizes();


protected:
    /*
     * Subclass must update the content of this->hdr such
     * calc_struct_footprint_size() and calc_internal_data_space_size() reflect the updated
     * sizes of the descriptor struct and a next write_struct_into() will work
     * as expected (writing that amount of bytes).
     *
     * This method does not call neither flush_writes() nor release_free_space().
     * So this method should reflect the actual state of what it is currently
     * written in disk/allocated, even if there are pending writes that may change
     * that.
     * If the caller wants it, it should call release_free_space() and flush_writes()
     * explicitly before calling update_header().
     *
     * When computing hdr.isize, update_header() *must* take into account the size
     * returned by future_idata_size().
     * */
    virtual void update_header();

    /*
     * Subclass must declare the size of the internal data needed.
     *
     * The caller will pass by reference the current "expected" isize
     * so if the subclass didn't change what it would write in the internal
     * data section (in terms of size), it could leave the parameter untouched.
     *
     * However, subclass *must* override the method anyways (even if it do nothing)
     * because the Descriptor class cannot know a priori how much isize is needed
     * if the descriptor subclass was created from memory instead of loading from disk.
     * In those cases, Descriptor has no idea of the isize!
     * */
    virtual void update_isize(uint64_t& isize) = 0;

    /*
     * Subclass may update the content size and segment.
     *
     * If the subclass doesn't have content or if it does but accesses through
     * the resize_content_part() and get_content_part_io() interfaces, the sizes and segments
     * are automatically updated by Descriptor and it is not needed be managed
     * explicitly by the subclass.
     *
     * Subclass must override this if they interact with the underlying content part(s)
     * outsize the Descriptor's interface *or* if it wants to do a last-minute modification.
     * (Note: last-minute modifications *probably* should be done in flush_writes()
     * and not here).
     *
     * Warning: this method requires a *deep* understanding of what csize and future_csize
     * mean and how they should be computed. Otherwise, backward/forward compatibility
     * may be *broken*.
     * In general, prefer to use the resize_content_part()/get_content_part_io() interface
     * instead of managing the content parts directly.
     *
     * The parameter will be a reference to the Descriptor's content parts vector.
     * Do *not* add/remove entries; do *not* modify entries that the subclass suppose
     * not be aware of.
     * */
    virtual void update_content_parts(std::vector<struct content_part_t>& cparts);

    /*
     * Declare the size of each content part (csize).
     *
     * By default, the Descriptor class will do nothing on this and instead use what
     * it found during the load of the descriptor from the disk.
     *
     * Subclasses should override this to change the default values to adjust what
     * *they* think it is *theirs* csize. These may differ from what the Descriptor
     * class loaded from disk.
     *
     * There are three posibilites:
     *  - the declared sizes matches the found in the header: ok
     *  - the declared sizes are greater than the found in the header: fail (throw)
     *
     * The third case is when the declared sizes are lesser than the found:
     * the Descriptor class will asume that any "excessing" data (not declared by the subclass)
     * is coming from the same subclass but from a future version so that "excessing" data
     * will be hidden by the Descriptor and preserver on write so the data in disk can be
     * read by future version of the code.
     *
     * Note: the method is for content parts' csizes; the Descriptor class will automatically
     * learn how much of internal data (isize) the subclass needed (by measuring how much
     * the subclass read on read_struct_specifics_from()) so it is not needed to declare anything
     * as it is implicit.
     * The same three posibilites can happen and they are handled in the same way
     * after the call to read_struct_specifics_from().
     *
     * Note: if the subclass decides to *not* override this method, it *will* imply that
     * the content parts the current version is aware of are fully of its own and
     * future version cannot sneak into data that the previous version cannot understand
     * In other words, not overriding implies not supporting forward-compatibility.
     *
     * This does *not* apply to additional content parts that a future version could add, however.
     * */
    virtual void declare_used_content_space_on_load(std::vector<uint64_t>& cparts_sizes) const;

    /*
     * Subclass must release any free/unused space allocated by the descriptor
     * that it is not longer needed.
     *
     * A call to release_free_space() does not imply a call to flush_writes()
     * so even if the descriptor releases some space, a call to flush_writes()
     * may allocate it back.
     * This also does not imply a call to update_header() so the header of
     * the descriptor may leave out of sync.
     * */
    virtual void release_free_space() {}

    /*
     * Subclass must flush any pending write.
     * This does not call neither update_header() nor release_free_space().
     *
     * Caller should call update_header() after this method to ensure
     * that the header is up-to-date.
     * See full_sync() method.
     * */
    virtual void flush_writes() {}

    virtual void complete_load() {}

    void update_sizes_of_header();

private:
    /*
     * No copy nor move constructors/assign operators
     * Make the descriptor no-copyable and pointer-stable.
     * */
    Descriptor(const Descriptor&) = delete;
    Descriptor(Descriptor&&) = delete;

    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor& operator=(Descriptor&&) = delete;

private:
    // This field is meant to be filled and controlled by the DescriptorSet that owns
    // this descriptor
    //
    // It is the place/location where the descriptor was loaded. It may be an
    // EmptyExtent if the descriptor was never loaded from disk.
    Extent ext;

    // Block array that holds the content of the descriptor (if any).
    BlockArray& cblkarr;

    /*
     * During the load_struct_from(), we may have a descriptor with isize
     * of I but the subclass read less than I bytes (in read_struct_specifics_from())
     * This could happen if the descriptor was written by a App version greater than
     * the reader so those unread bytes must be preserved to avoid data losses.
     * */
    std::vector<char> future_idata;

protected:
    [[nodiscard]] inline Content get_content_part(uint16_t part_num) { return Content(*this, hdr.cparts.at(part_num)); }

    /*
     * Resize the content space to have the given size. If no content
     * exists before, a new space is allocated, otherwise a resize happen.
     *
     * The method will preserve any 'future' content behind the scene
     * storing it at the end of the content space in additional space.
     * This does not affect the free space requested: if the caller requested
     * a resize of N bytes, N bytes will get.
     *
     * A resize of 0 bytes may dealloc the space.
     * It it the correct way to disown the content and free space, however
     * the space may not be fully released if the descriptor has 'future' content.
     * */
    void resize_content_part(struct content_part_t& cpart, uint32_t content_new_sz);

    /*
     * Get content return the present version's content (hiding future content).
     *
     * Subclasses should use get_content_part_io() only.
     * */
    IOSegment get_content_part_io(struct content_part_t& cpart);

    /*
     * Count incompressible content parts.
     * A content part is compressible if it has zero size and all the parts
     * to its right are compressible as well.
     * */
    uint16_t count_incompressible_cparts() const;

public:
    static uint16_t count_incompressible_cparts(const struct header_t& hdr);

private:
    /*
     * Raw pointer to the set that owns this descriptor. There is at most one owner.
     * Raw pointer is used because once a sets owns a descriptor, the lifetime
     * of the set supersedes the lifetime of the descriptor so using a raw pointer
     * should be safe.
     *
     * Of course, the pointer can be null if the descriptor is not owned by a set.
     * */
    DescriptorSet* owner_raw_ptr;


public:  // Meant to be accesible from the tests and from the DescriptorSet
    /*
     * Set/get the descriptor set owner of the descriptor (this).
     * It may be null if the descriptor has no owner.
     * */
    void set_owner(DescriptorSet* owner) { this->owner_raw_ptr = owner; }
    DescriptorSet* get_owner() const { return this->owner_raw_ptr; }

public:
    /*
     * This is the inet checksum computed during the
     * last read_struct_from/load_struct_from/write_struct_into call.
     * It does *not* reflect the checksum of the current in-memory state of the descriptor.
     * */
    uint16_t /* internal */ checksum;

private:
    static void chk_rw_specifics_on_idata(bool is_read_op, IOBase& io, uint32_t idata_begin, uint32_t subclass_end,
                                          uint32_t idata_sz);
    static void chk_struct_footprint(bool is_read_op, IOBase& io, uint32_t dsc_begin, uint32_t dsc_end,
                                     const Descriptor* const dsc, bool ex_type_used);
    static void chk_dset_type(bool is_read_op, const Descriptor* const dsc, const struct Descriptor::header_t& hdr,
                              const RuntimeContext& rctx);

    constexpr static inline bool does_hdr_isize_fit(uint64_t hdr_isize) { return hdr_isize <= 127; }
    constexpr static inline bool does_hdr_csize_fit(uint64_t hdr_csize) { return hdr_csize <= 0x7fffffff; }

    static struct Descriptor::header_t create_header(const uint16_t type, const BlockArray& cblkarr);

    static std::unique_ptr<Descriptor> begin_load_dsc_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr,
                                                           uint32_t dsc_begin_pos, bool& ex_type_used);
    static void finish_load_dsc_from(IOBase& io, RuntimeContext& rctx, BlockArray& cblkarr, Descriptor& dsc,
                                     uint32_t dsc_begin_pos, uint32_t idata_begin_pos, bool ex_type_used);

    static void chk_content_parts_count(bool wouldBe, const header_t& hdr, const uint16_t decl_cpart_cnt);
    static void chk_content_parts_consistency(bool wouldBe, const header_t& hdr);
};
}  // namespace xoz
