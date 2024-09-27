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
 * types, hence, it has not a specified type.
 * */

class OpaqueDescriptor: public Descriptor {
public:
    OpaqueDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              RuntimeContext& rctx);

protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
    void update_sizes(uint64_t& isize, uint64_t& csize) override;
};
}  // namespace xoz
