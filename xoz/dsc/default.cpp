#include "xoz/dsc/default.h"

DefaultDescriptor::DefaultDescriptor(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr):
        Descriptor(hdr, ed_blkarr) {
    dsc_data.resize(hdr.dsize);
}

void DefaultDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(dsc_data, hdr.dsize); }

void DefaultDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(dsc_data, hdr.dsize); }

std::unique_ptr<Descriptor> DefaultDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr) {
    return std::make_unique<DefaultDescriptor>(hdr, ed_blkarr);
}

void DefaultDescriptor::set_data(const std::vector<char>& data) {
    // chk for overflow
    if (data.size() > uint8_t(-1)) {
        throw "";
    }

    auto dsize = uint8_t(data.size());
    if (dsize % 2 != 0) {
        throw "";
    }

    if (is_dsize_greater_than_allowed(dsize)) {
        throw "";
    }

    hdr.dsize = dsize;
    dsc_data = data;
}
