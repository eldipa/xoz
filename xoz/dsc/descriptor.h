#pragma once
#include <cstdint>
#include <map>
#include <memory>

#include "xoz/ext/extent.h"
#include "xoz/io/iobase.h"
#include "xoz/segm/segment.h"

class IDManager;
class DescriptorSet;
class BlockArray;

class Descriptor {

public:
    static std::unique_ptr<Descriptor> load_struct_from(IOBase& io, IDManager& idmgr, BlockArray& ed_blkarr);
    void write_struct_into(IOBase& io);

    static std::unique_ptr<Descriptor> load_struct_from(IOBase&& io, IDManager& idmgr, BlockArray& ed_blkarr) {
        return load_struct_from(io, idmgr, ed_blkarr);
    }
    void write_struct_into(IOBase&& io) { return write_struct_into(io); }

public:
    // Return the size in bytes to represent the Descriptor structure in disk
    // *including* the descriptor data (see calc_data_space_size)
    uint32_t calc_struct_footprint_size() const;

    // Return the size in bytes that this descriptor has. Such data space
    // can be used by a Descriptor subclass to retrieve / store specifics
    // fields.
    // For the perspective of this method, such interpretation is transparent
    // and the whole space is seen as a single consecutive chunk of space
    // whose size is returned.
    uint32_t calc_data_space_size() const { return hdr.dsize; }

    // Return the size in bytes of that referenced by the segment and
    // that represent the external data (not the descriptor's data).
    //
    // The size may be larger than calc_external_data_size() (the esize field in the descriptor
    // header) if the descriptor has more space allocated than the declared in esize.
    // In this sense, calc_external_data_space_size() is the
    // total usable space while hdr.esize (or calc_external_data_size) is the used space.
    //
    // For non-owner descriptors returns always 0
    uint32_t calc_external_data_space_size() const;

    uint32_t calc_external_data_size() const { return hdr.own_edata ? hdr.esize : 0; }

public:
    virtual ~Descriptor() {}

    friend void PrintTo(const Descriptor& dsc, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const Descriptor& dsc);

    uint32_t id() const { return hdr.id; }


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

public:  // public but it should be interpreted as an opaque section
    struct header_t {
        bool own_edata;
        uint16_t type;

        uint32_t id;

        uint8_t dsize;   // in bytes
        uint32_t esize;  // in bytes

        Segment segm;  // data segment, only for own_edata descriptors
    };

    friend void PrintTo(const struct header_t& hdr, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const struct header_t& hdr);

    /*
     * Does the descriptor owns external data?
     * */
    bool does_own_edata() const { return hdr.own_edata; }

    /*
     * Return a const reference to the segment that points to the owned external
     * data.
     * The segment is **undefined** if does_own_edata() returns false.
     * */
    const Segment& edata_segment_ref() const {
        assert(does_own_edata());
        return hdr.segm;
    }

    friend class DescriptorSet;

protected:
    struct header_t hdr;

    /*
     * Descriptor's constructor with its header and a reference to the block array
     * for external data.
     * The constructor is meant for internal use and its subclasses because it exposes
     * too much its header.
     * */
    Descriptor(const struct header_t& hdr, BlockArray& ed_blkarr):
            hdr(hdr), ext(Extent::EmptyExtent()), ed_blkarr(ed_blkarr), checksum(0) {}

    static void chk_dsize_fit_or_fail(bool has_id, const struct Descriptor::header_t& hdr);

    /* Subclasses must override these methods to read/write specific data
     * from/into the iobase (repository) where the read/write pointer of io object
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
     * Method called just at the begin of the write_struct_into() method.
     * It is meant to be extended by subclasses that wish to sync the descriptor's header (hdr)
     * or perform some tweak before writing the descriptor to disk
     * */
    virtual void pre_write_struct() {}

    /*
     * Subclasses must to do any deallocation and clean up because the descriptor
     * is about to be removed (destroyed).
     *
     * By default this method dealloc any allocated block in the external block array
     * if the descriptor owns any.
     * */
    virtual void destroy();

    constexpr inline bool is_dsize_greater_than_allowed(uint8_t dsize) { return dsize > 127; }

    constexpr inline bool is_esize_greater_than_allowed(uint32_t esize) { return esize > 0x7fffffff; }

    constexpr inline bool is_id_temporal(const uint32_t id) const { return bool(id & 0x80000000); }

    constexpr inline bool is_id_persistent(const uint32_t id) const { return not is_id_temporal(id); }

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

    // Block array that holds the external data of the descriptor (if any).
    BlockArray& ed_blkarr;

public:
    /*
     * This is the inet checksum computed during the
     * last read_struct_from/load_struct_from/write_struct_into call.
     * It does *not* reflect the checksum of the current in-memory state of the descriptor.
     * */
    uint16_t /* internal */ checksum;

private:
    static void chk_rw_specifics_on_data(bool is_read_op, IOBase& io, uint32_t data_begin, uint32_t subclass_end,
                                         uint32_t data_sz);
    static void chk_struct_footprint(bool is_read_op, IOBase& io, uint32_t dsc_begin, uint32_t dsc_end,
                                     const Descriptor* const dsc, bool ex_type_used);
};

// Signature that a function must honor to be used as a descriptor-create function
// It takes a descriptor (common) header and it must return a dynamic allocated
// subclass of Descriptor as a pointer to the base class Descriptor.
//
// Once created the read_struct_specifics_from is invoked to complete the initialization
// of the subclass descriptor
typedef std::unique_ptr<Descriptor> (*descriptor_create_fn)(const struct Descriptor::header_t& hdr,
                                                            BlockArray& ed_blkarr, IDManager& idmgr);

void initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& descriptors);

void deinitialize_descriptor_mapping();
