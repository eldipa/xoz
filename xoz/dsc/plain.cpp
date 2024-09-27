#include "xoz/dsc/plain.h"

namespace xoz {
PlainDescriptor::PlainDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        Descriptor(hdr, cblkarr) {
    idata.resize(hdr.isize);  // TODO is this correct?
}

void PlainDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(idata); }

void PlainDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(idata); }

void PlainDescriptor::update_sizes(uint8_t& isize, [[maybe_unused]] uint32_t& csize) {
    // PlainDescriptor does not have any content so csize is left unmodified.
    isize = assert_u8(idata.size());
}

std::unique_ptr<Descriptor> PlainDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                    [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<PlainDescriptor>(hdr, cblkarr);
}

void PlainDescriptor::set_idata(const std::vector<char>& data) {
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

    idata = data;
    notify_descriptor_changed();
}

const std::vector<char>& PlainDescriptor::get_idata() const { return idata; }

}  // namespace xoz