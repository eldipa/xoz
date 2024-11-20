#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

namespace xoz {

class IDMappingDescriptor: public Descriptor {
public:
    constexpr static uint16_t TYPE = 0x01bf;

    constexpr static char TempNamePrefix = '~';

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              RuntimeContext& rctx);

    static std::unique_ptr<IDMappingDescriptor> create(BlockArray& cblkarr);

    /*
     * Store/load the entire mapping.
     * */
    void store(const std::map<std::string, uint32_t>& id_by_name);
    std::map<std::string, uint32_t> load();

private:
    IDMappingDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr);
    explicit IDMappingDescriptor(BlockArray& cblkarr);

private:
    void fail_if_bad_values(uint32_t id, const std::string& name) const;
    uint32_t calculate_store_mapping_size(const std::map<std::string, uint32_t>& id_by_name) const;

    uint16_t num_entries;
    uint32_t content_sz;

private:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
    void update_sizes(uint64_t& isize, uint64_t& csize) override;
};
}  // namespace xoz
