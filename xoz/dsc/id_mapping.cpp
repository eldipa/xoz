#include "xoz/dsc/id_mapping.h"

namespace xoz {
IDMappingDescriptor::IDMappingDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
        Descriptor(hdr, cblkarr), num_entries(0), content_sz(0) {}

IDMappingDescriptor::IDMappingDescriptor(BlockArray& cblkarr):
        Descriptor(IDMappingDescriptor::TYPE, cblkarr), num_entries(0), content_sz(0) {}

void IDMappingDescriptor::read_struct_specifics_from(IOBase& io) { num_entries = io.read_u16_from_le(); }

void IDMappingDescriptor::write_struct_specifics_into(IOBase& io) { io.write_u16_to_le(num_entries); }

void IDMappingDescriptor::update_sizes(uint64_t& isize, uint64_t& csize) {
    isize = sizeof(uint32_t);  // num entries
    csize = content_sz;
}

std::unique_ptr<Descriptor> IDMappingDescriptor::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                        [[maybe_unused]] RuntimeContext& rctx) {
    return std::unique_ptr<IDMappingDescriptor>(new IDMappingDescriptor(hdr, cblkarr));
}

std::unique_ptr<IDMappingDescriptor> IDMappingDescriptor::create(BlockArray& cblkarr) {
    return std::unique_ptr<IDMappingDescriptor>(new IDMappingDescriptor(cblkarr));
}

uint32_t IDMappingDescriptor::calculate_store_mapping_size(const std::map<std::string, uint32_t>& id_by_name) const {
    uint32_t sz = 0;
    for (auto const& [name, id]: id_by_name) {
        fail_if_bad_values(id, name);

        // Temp names are not stored
        if (name[0] == TempNamePrefix) {
            continue;
        }

        sz += sizeof(id);
        sz += sizeof(uint8_t);  // name length
        sz += assert_u8(name.size());
    }

    return sz;
}

void IDMappingDescriptor::store(const std::map<std::string, uint32_t>& id_by_name) {
    content_sz = calculate_store_mapping_size(id_by_name);
    resize_content(content_sz);

    // Note: sanity checks were made in calculate_store_mapping_size()
    // No need to repeat them here.
    auto io = get_content_io();
    uint32_t cnt = 0;
    for (auto const& [name, id]: id_by_name) {
        // Temp names are not stored
        if (name[0] == TempNamePrefix) {
            continue;
        }

        ++cnt;
        io.write_u32_to_le(id);
        io.write_u8_to_le(assert_u8(name.size()));
        io.writeall(name.data(), assert_u8(name.size()));
    }

    num_entries = assert_u16(cnt);
}

std::map<std::string, uint32_t> IDMappingDescriptor::load() {
    std::map<std::string, uint32_t> id_by_name;

    char buf[256];
    auto io = get_content_io();
    for (unsigned i = 0; i < num_entries; ++i) {
        uint32_t id = io.read_u32_from_le();
        uint8_t len = io.read_u8_from_le();

        xoz_assert("", len <= sizeof(buf));

        io.readall(buf, len);
        std::string name(buf, len);

        fail_if_bad_values(id, name);
        id_by_name[name] = id;
    }

    return id_by_name;
}

void IDMappingDescriptor::fail_if_bad_values(uint32_t id, const std::string& name) const {
    if (id == 0) {
        throw std::runtime_error("Descriptor id '0' is not valid.");
    }

    if (id & 0x80000000) {
        throw std::runtime_error("Descriptor id exceeds 2^31.");
    }

    if (name.size() > 255) {
        throw std::runtime_error("Name for the descriptor is too large.");
    }

    if (name.size() == 0) {
        throw std::runtime_error("Name for the descriptor cannot be empty.");
    }

    if (name == "/" or name == "." or name == "..") {
        throw std::runtime_error("Name for the descriptor is reserved.");
    }
}
}  // namespace xoz
