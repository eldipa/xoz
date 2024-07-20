#pragma once
#include <cstdint>
#include <map>
#include <memory>

#include "xoz/ext/extent.h"
#include "xoz/io/iobase.h"
#include "xoz/segm/segment.h"

namespace xoz {
class RuntimeContext;
class DescriptorSet;
class BlockArray;

class Descriptor {
public:
    struct header_t {
        bool own_content;
        uint16_t type;

        uint32_t id;

        uint8_t isize;   // in bytes
        uint32_t csize;  // in bytes

        Segment segm;  // data segment, only for own_content descriptors
    };

public:
    /*
     * Callers should call update_header() before calling write_struct_into()
     * to ensure that the content of the descriptor is updated so any calculation
     * of its size will be correct.
     * */
    static std::unique_ptr<Descriptor> load_struct_from(IOBase& io, RuntimeContext& rctx, BlockArray& ed_blkarr);
    void write_struct_into(IOBase& io, RuntimeContext& rctx);

    static std::unique_ptr<Descriptor> load_struct_from(IOBase&& io, RuntimeContext& rctx, BlockArray& ed_blkarr) {
        return load_struct_from(io, rctx, ed_blkarr);
    }
    void write_struct_into(IOBase&& io, RuntimeContext& rctx) { return write_struct_into(io, rctx); }

    /*
     * Load the header of the descriptor but do not create a Descriptors class or call any
     * subclass-specific loading.
     * The computed checksum *does* include the descriptor's internal data (isize) however.
     *
     * The read pointer of io is left at the begin of the descriptor's internal data.
     * */
    static struct header_t load_header_from(IOBase& io, RuntimeContext& rctx, BlockArray& ed_blkarr, bool& ex_type_used,
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

public:
    // Return the size in bytes to represent the Descriptor structure in disk
    // *including* the descriptor internal data (see calc_internal_data_space_size)
    uint32_t calc_struct_footprint_size() const;

    // Return the size in bytes that this descriptor has for internal data.
    // Such internal data space can be used by a Descriptor subclass
    // to retrieve / store specifics fields.
    // For the perspective of this method, such interpretation is transparent
    // and the whole space is seen as a single consecutive chunk of space
    // whose size is returned.
    uint32_t calc_internal_data_space_size() const { return hdr.isize; }

    // Return the size in bytes of that referenced by the segment and
    // that represent the content of the descriptor (not the descriptor's internal data).
    //
    // The size may be larger than content_size() (the csize field in the descriptor
    // header) if the descriptor has more space allocated than the declared in csize.
    // In this sense, calc_content_space_size() is the
    // total usable space while hdr.csize (or content_size) is the used space.
    //
    // For non-owner descriptors returns always 0
    uint32_t calc_content_space_size() const;

    uint32_t content_size() const { return hdr.own_content ? hdr.csize : 0; }

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
     * */
    template <typename T>
    T* cast(bool ret_null = false) {
        auto ptr = dynamic_cast<T*>(this);
        if (!ptr and not ret_null) {
            throw std::runtime_error("Descriptor cannot be dynamically down casted.");
        }

        return ptr;
    }

    /*
     * Dynamically downcast a unique pointer to Descriptor (<base_ptr>)
     * to an unique pointer to the given concrete subclass T.
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

public:  // public but it should be interpreted as an opaque section
    friend void PrintTo(const struct header_t& hdr, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const struct header_t& hdr);

    /*
     * Does the descriptor owns content?
     * */
    bool does_own_content() const { return hdr.own_content; }

    /*
     * Return a const reference to the segment that points to the owned content
     * data.
     * The segment is **undefined** if does_own_content() returns false.
     * */
    const Segment& content_segment_ref() const {
        assert(does_own_content());
        return hdr.segm;
    }

    friend class DescriptorSet;

protected:
    struct header_t hdr;

    /*
     * Descriptor's constructor with its header and a reference to the block array
     * for content data.
     * The constructor is meant for internal use and its subclasses because it exposes
     * too much its header.
     * */
    Descriptor(const struct header_t& hdr, BlockArray& ed_blkarr):
            hdr(hdr), ext(Extent::EmptyExtent()), ed_blkarr(ed_blkarr), owner_raw_ptr(nullptr), checksum(0) {}

    static void chk_isize_fit_or_fail(bool has_id, const struct Descriptor::header_t& hdr);

    /* Subclasses must override these methods to read/write specific data
     * from/into the iobase (xoz file) where the read/write pointer of io object
     * is immediately after the descriptor (common) header.
     *
     * See load_struct_from and write_struct_into methods for more context.
     *
     * Subclasses must *not* use the allocator of the ed_blkarr during the read
     * because it may not be enabled by the moment. Subclasses can use the ed_blkarr
     * for reading/writing without problem.
     * */
    virtual void read_struct_specifics_from(IOBase& io) = 0;
    virtual void write_struct_specifics_into(IOBase& io) = 0;
    void read_struct_specifics_from(IOBase&& io) { read_struct_specifics_from(io); }
    void write_struct_specifics_into(IOBase&& io) { write_struct_specifics_into(io); }

    /*
     * Subclasses must to do any deallocation and clean up because the descriptor
     * is about to be removed (destroyed).
     *
     * By default this method dealloc any allocated block (content) in the block array
     * if the descriptor owns any.
     * */
    virtual void destroy();

    constexpr static inline bool is_isize_greater_than_allowed(uint8_t isize) { return isize > 127; }

    constexpr static inline bool is_csize_greater_than_allowed(uint32_t csize) { return csize > 0x7fffffff; }

    constexpr static inline bool is_id_temporal(const uint32_t id) { return bool(id & 0x80000000); }

    constexpr static inline bool is_id_persistent(const uint32_t id) { return not Descriptor::is_id_temporal(id); }

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
     * */
    virtual void update_header() = 0;

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
    BlockArray& ed_blkarr;

protected:
    BlockArray& external_blkarr() { return ed_blkarr; }

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

public:
private:
    static void chk_rw_specifics_on_idata(bool is_read_op, IOBase& io, uint32_t idata_begin, uint32_t subclass_end,
                                          uint32_t idata_sz);
    static void chk_struct_footprint(bool is_read_op, IOBase& io, uint32_t dsc_begin, uint32_t dsc_end,
                                     const Descriptor* const dsc, bool ex_type_used);
};
}  // namespace xoz
