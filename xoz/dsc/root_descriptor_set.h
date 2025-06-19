#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/descriptor_set.h"

namespace xoz {

class RootDescriptorSet: public DescriptorSet {
public:
    constexpr static uint16_t TYPE = 0x0002;

    enum Streams : uint16_t {
        Main = 0,
        Meta,
        END  // this *must* be the last entry
    };


    static std::unique_ptr<RootDescriptorSet> create_root_descriptor_set(BlockArray& cblkarr, RuntimeContext& rctx);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              RuntimeContext& rctx);

    RootDescriptorSet(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, RuntimeContext& rctx);
    RootDescriptorSet(const uint16_t TYPE, BlockArray& cblkarr, RuntimeContext& rctx);
};
}  // namespace xoz
