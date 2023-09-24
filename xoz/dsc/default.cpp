#include "xoz/dsc/default.h"

DefaultDescriptor::DefaultDescriptor(const struct Descriptor::header_t& hdr): Descriptor(hdr) {
    dsc_data.resize(hdr.dsize);
}

void DefaultDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(dsc_data, hdr.dsize); }

void DefaultDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(dsc_data, hdr.dsize); }

std::unique_ptr<Descriptor> DefaultDescriptor::create(const struct Descriptor::header_t& hdr) {
    return std::make_unique<DefaultDescriptor>(hdr);
}

void DefaultDescriptor::set_data(const std::vector<char>& data) {
    if (data.size() > uint8_t(-1)) {
        throw "";
    }

    hdr.dsize = uint8_t(data.size());
    if (hdr.dsize % 2 != 0) {
        throw "";
    }

    dsc_data = data;
}
