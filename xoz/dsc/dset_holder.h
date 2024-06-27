#pragma once

#include <cstdint>
#include <memory>

#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/io/iobase.h"

class DescriptorSetHolder: public Descriptor {
public:
    DescriptorSetHolder(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr, RuntimeContext& rctx);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr,
                                              RuntimeContext& rctx);

    static std::unique_ptr<DescriptorSetHolder> create(BlockArray& ed_blkarr, RuntimeContext& rctx,
                                                       uint16_t u16data = 0);

    // TODO warn the caller to *not* remove the set. Instead, delete the DescriptorSetHolder descriptor.
    // NOTE: very likely we need a hook on_destroy() or on_remove() in the Descriptor API
    const std::unique_ptr<DescriptorSet>& set() { return dset; }

protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;

public:
    void destroy() override;

protected:
    void update_header() override;
    void flush_writes() override;
    void release_free_space() override;

private:
    std::unique_ptr<DescriptorSet> dset;
    uint16_t reserved;

    // TODO does make sense to keep a private reference? could have it Descriptor parent class?
    BlockArray& ed_blkarr;
    RuntimeContext& rctx;
};
