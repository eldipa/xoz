#include "test/plain.h"
#include "xoz/dsc/spy.h"

using namespace ::xoz;
typedef ::xoz::dsc::internals::DescriptorInnerSpyForTesting DSpy;

namespace testing_xoz {
PlainDescriptor::PlainDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        PlainDescriptor(hdr, cblkarr, 0) {
}

PlainDescriptor::PlainDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, uint16_t cpart_cnt):
        Descriptor(hdr, cblkarr, cpart_cnt) {
    idata.resize(hdr.isize);  // TODO is this correct?
}

void PlainDescriptor::read_struct_specifics_from(IOBase& io) { io.readall(idata); }

void PlainDescriptor::write_struct_specifics_into(IOBase& io) { io.writeall(idata); }

void PlainDescriptor::update_isize(uint64_t& isize) {
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

PlainWithImplContentDescriptor::PlainWithImplContentDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        PlainDescriptor(hdr, cblkarr, PlainWithImplContentDescriptor::Parts::CNT) {
}

std::unique_ptr<Descriptor> PlainWithImplContentDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                    [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<PlainWithImplContentDescriptor>(hdr, cblkarr);
}

void PlainWithImplContentDescriptor::set_content(const std::vector<char>& content) {
    uint32_t content_size = assert_u32(content.size());
    auto cpart = get_content_part(Parts::Data);
    cpart.resize(content_size);

    auto io = cpart.get_io();
    io.writeall(content);
    notify_descriptor_changed();
}

const std::vector<char> PlainWithImplContentDescriptor::get_content() /* TODO const */ {
    std::vector<char> content;
    auto io = get_content_part(Parts::Data).get_io();
    io.readall(content);

    return content;
}

void PlainWithImplContentDescriptor::del_content() {
    get_content_part(Parts::Data).resize(0);
    notify_descriptor_changed();
}

PlainWithContentDescriptor::PlainWithContentDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        PlainDescriptor(hdr, cblkarr, PlainWithContentDescriptor::Parts::CNT), content_size(0) {
}

void PlainWithContentDescriptor::read_struct_specifics_from(IOBase& io) {
    if (DSpy(*this).does_own_content()) {
        content_size = io.read_u32_from_le();
    }

    PlainDescriptor::read_struct_specifics_from(io);
}

void PlainWithContentDescriptor::write_struct_specifics_into(IOBase& io) {
    if (DSpy(*this).does_own_content()) {
        io.write_u32_to_le(content_size);
    }

    PlainDescriptor::write_struct_specifics_into(io);
}

void PlainWithContentDescriptor::update_isize(uint64_t& isize) {
    PlainDescriptor::update_isize(isize);
    isize += optional_field_size();
}

std::unique_ptr<Descriptor> PlainWithContentDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                    [[maybe_unused]] RuntimeContext& rctx) {
    return std::make_unique<PlainWithContentDescriptor>(hdr, cblkarr);
}

void PlainWithContentDescriptor::set_content(const std::vector<char>& content) {
    content_size = assert_u32(content.size());
    auto cpart = get_content_part(Parts::Data);
    cpart.resize(content_size);

    auto io = cpart.get_io();
    io.writeall(content);
    notify_descriptor_changed();
}

const std::vector<char> PlainWithContentDescriptor::get_content() /* TODO const */ {
    std::vector<char> content;
    auto io = get_content_part(Parts::Data).get_io();
    io.readall(content);

    return content;
}

void PlainWithContentDescriptor::del_content() {
    get_content_part(Parts::Data).resize(0);
    content_size = 0;
    notify_descriptor_changed();
}

uint32_t PlainWithContentDescriptor::optional_field_size() const {
    return assert_u32(DSpy(*this).does_own_content() ? sizeof(uint32_t) : 0);
}
}
