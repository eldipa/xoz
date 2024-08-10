#include "xoz/dsc/opaque.h"

namespace xoz {
OpaqueDescriptor::OpaqueDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        Descriptor(hdr, cblkarr) {
    future_idata.resize(hdr.isize);
}

void OpaqueDescriptor::read_struct_specifics_from([[maybe_unused]] IOBase& io) {}

void OpaqueDescriptor::write_struct_specifics_into([[maybe_unused]] IOBase& io) {}

std::unique_ptr<Descriptor> OpaqueDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                     [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<OpaqueDescriptor>(hdr, cblkarr);
}

void OpaqueDescriptor::set_idata(const std::vector<char>& data) {
    // chk for overflow
    if (data.size() > uint8_t(-1)) {
        throw "";
    }

    auto isize = uint8_t(data.size());
    if (isize % 2 != 0) {
        throw "";
    }

    if (is_isize_greater_than_allowed(isize)) {
        throw "";
    }

    hdr.isize = isize;
    future_idata = data;
    notify_descriptor_changed();
}

const std::vector<char>& OpaqueDescriptor::get_idata() const { return future_idata; }
}  // namespace xoz
