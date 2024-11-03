#include "xoz/dsc/private.h"

namespace xoz {
PrivateDescriptor::PrivateDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        OpaqueDescriptor(hdr, cblkarr) {}

std::unique_ptr<Descriptor> PrivateDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                      [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<PrivateDescriptor>(hdr, cblkarr);
}
}  // namespace xoz
