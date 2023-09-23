#pragma once
#include <cstdint>
#include <map>
#include <memory>

#include "xoz/mem/iobase.h"
#include "xoz/segm/segment.h"

class Descriptor {

public:
    struct header_t {
        bool is_obj;
        uint16_t type;

        uint32_t obj_id;

        uint8_t dsize;  // in bytes
        uint32_t size;  // in bytes

        Segment segm;  // data segment, only for obj descriptors
    };

    explicit Descriptor(const struct header_t& hdr): hdr(hdr) {}

    static std::unique_ptr<Descriptor> load_struct_from(IOBase& io);
    void write_struct_into(IOBase& io);

    // Subclasses must override these methods to read/write specific data
    // from/into the iobase (repository) that read/write pointer is immediately
    // after the descriptor (common) header.
    virtual void read_struct_specifics_from(IOBase& io) = 0;
    virtual void write_struct_specifics_into(IOBase& io) = 0;

    virtual ~Descriptor() {}

protected:
    struct header_t hdr;
};

// Signature that a function must honor to be used as a descriptor-create function
// It takes a descriptor (common) header and it must return a dynamic allocated
// subclass of Descriptor as a pointer to the base class Descriptor.
//
// Once created the read_struct_specifics_from is invoked to complete the initialization
// of the subclass descriptor
typedef std::unique_ptr<Descriptor> (*descriptor_create_fn)(const struct Descriptor::header_t& hdr);

void initialize_descriptor_mapping(const std::map<uint16_t, descriptor_create_fn>& non_obj_descriptors,
                                   const std::map<uint16_t, descriptor_create_fn>& obj_descriptors);

void deinitialize_descriptor_mapping();
