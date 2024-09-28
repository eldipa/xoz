#include "test/plain.h"

using namespace ::xoz;

namespace testing_xoz {
PlainDescriptor::PlainDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        Descriptor(hdr, cblkarr) {
    idata.resize(hdr.isize);  // TODO is this correct?
}

void PlainDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(idata); }

void PlainDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(idata); }

void PlainDescriptor::update_sizes(uint64_t& isize, [[maybe_unused]] uint64_t& csize) {
    isize = assert_u8(idata.size());
}

std::unique_ptr<Descriptor> PlainDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                    [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<PlainDescriptor>(hdr, cblkarr);
}

void PlainDescriptor::set_idata(const std::vector<char>& data) {
    // chk for overflow
    if (not does_present_isize_fit(data.size())) {
        throw "";
    }

    idata = data;
    notify_descriptor_changed();
}

const std::vector<char>& PlainDescriptor::get_idata() const { return idata; }


PlainWithContentDescriptor::PlainWithContentDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        PlainDescriptor(hdr, cblkarr), content_size(0) {
}

void PlainWithContentDescriptor::read_struct_specifics_from(IOBase& io) {
    if (does_own_content()) {
        content_size = io.read_u32_from_le();
    }

    PlainDescriptor::read_struct_specifics_from(io);
}

void PlainWithContentDescriptor::write_struct_specifics_into(IOBase& io) {
    if (does_own_content()) {
        io.write_u32_to_le(content_size);
    }

    PlainDescriptor::write_struct_specifics_into(io);
}

void PlainWithContentDescriptor::update_sizes(uint64_t& isize, uint64_t& csize) {
    PlainDescriptor::update_sizes(isize, csize);

    isize += optional_field_size();
    csize = get_hdr_csize();
}

std::unique_ptr<Descriptor> PlainWithContentDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                    [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<PlainWithContentDescriptor>(hdr, cblkarr);
}

void PlainWithContentDescriptor::set_content(const std::vector<char>& content) {
    if (not does_present_csize_fit(content.size())) {
        throw "";
    }

    content_size = assert_u32(content.size());
    resize_content(content_size);

    auto io = get_content_io();
    io.writeall(content);
}

const std::vector<char> PlainWithContentDescriptor::get_content() /* TODO const */ {
    std::vector<char> content;
    auto io = get_content_io();
    io.readall(content);

    return content;
}

void PlainWithContentDescriptor::del_content() { resize_content(0); }

uint32_t PlainWithContentDescriptor::optional_field_size() const {
    return assert_u32(does_own_content() ? sizeof(uint32_t) : 0);
}
}
