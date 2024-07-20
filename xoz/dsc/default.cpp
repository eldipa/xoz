#include "xoz/dsc/default.h"

namespace xoz {
DefaultDescriptor::DefaultDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        Descriptor(hdr, cblkarr) {
    internal_data.resize(hdr.isize);
}

void DefaultDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(internal_data, hdr.isize); }

void DefaultDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(internal_data, hdr.isize); }

std::unique_ptr<Descriptor> DefaultDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                      [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<DefaultDescriptor>(hdr, cblkarr);
}

void DefaultDescriptor::set_data(const std::vector<char>& data) {
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
    internal_data = data;
    notify_descriptor_changed();
}

const std::vector<char>& DefaultDescriptor::get_data() const { return internal_data; }
}  // namespace xoz
