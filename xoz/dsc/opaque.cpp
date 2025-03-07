#include "xoz/dsc/opaque.h"

namespace xoz {
OpaqueDescriptor::OpaqueDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        Descriptor(hdr, cblkarr, 0) {}

void OpaqueDescriptor::read_struct_specifics_from([[maybe_unused]] IOBase& io) {}

void OpaqueDescriptor::write_struct_specifics_into([[maybe_unused]] IOBase& io) {}

void OpaqueDescriptor::update_isize([[maybe_unused]] uint64_t& isize) {
    // OpaqueDescriptor should not modify its internal data
}

std::unique_ptr<Descriptor> OpaqueDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                     [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<OpaqueDescriptor>(hdr, cblkarr);
}
}  // namespace xoz
