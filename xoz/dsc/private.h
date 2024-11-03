#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/opaque.h"

namespace xoz {
/*
 * Private descriptor: it is a reserved descriptor type of the xoz library.
 * Applications should not use them directly.
 * */

class PrivateDescriptor: public OpaqueDescriptor {
public:
    PrivateDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              RuntimeContext& rctx);
};
}  // namespace xoz
