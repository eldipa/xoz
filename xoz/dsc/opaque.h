#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

namespace xoz {
/*
 * Opaque descriptor: it is the most basic descriptor possible that just
 * carries the data read from a XOZ file about a descriptor and writes into
 * the file without further interpretation.
 *
 * It is meant to be used by XOZ to load and write descriptors of unknown
 * types.
 * */

class OpaqueDescriptor: public Descriptor {
public:
    OpaqueDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              RuntimeContext& rctx);


public:
    /*
     * The set_idata and get_idata methods are mostly for testing.
     * In practice, nobody should be reading or modifying a DefaultDescriptor
     * because in theory this descriptor represent opaque unknown type
     * so the caller should have no idea how to use the data anyways.
     *
     * Do not use!!
     * */
    void /* private */ set_idata(const std::vector<char>& data);

    const /* private */ std::vector<char>& get_idata() const;

protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
    void update_header() override {}
};
}  // namespace xoz
