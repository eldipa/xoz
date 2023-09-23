#include "xoz/dsc/default.h"

DefaultDescriptor::DefaultDescriptor(const struct Descriptor::header_t& hdr): Descriptor(hdr) {}

void DefaultDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(dsc_data, hdr.dsize); }

void DefaultDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(dsc_data, hdr.dsize); }

std::unique_ptr<Descriptor> DefaultDescriptor::create(const struct Descriptor::header_t& hdr) {
    return std::make_unique<DefaultDescriptor>(hdr);
}
