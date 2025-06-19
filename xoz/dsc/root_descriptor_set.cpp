#include "xoz/dsc/root_descriptor_set.h"

namespace xoz {
std::unique_ptr<RootDescriptorSet> RootDescriptorSet::create_root_descriptor_set(BlockArray& cblkarr,
                                                                                 RuntimeContext& rctx) {
    auto dset = std::make_unique<RootDescriptorSet>(TYPE, cblkarr, rctx);
    dset->load_set();
    return dset;
}

std::unique_ptr<Descriptor> RootDescriptorSet::create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                      RuntimeContext& rctx) {
    assert(hdr.type == RootDescriptorSet::TYPE);
    auto dsc = std::make_unique<RootDescriptorSet>(hdr, cblkarr, rctx);
    return dsc;
}

RootDescriptorSet::RootDescriptorSet(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, RuntimeContext& rctx):
        DescriptorSet(hdr, cblkarr, RootDescriptorSet::Streams::END, rctx) {}
RootDescriptorSet::RootDescriptorSet(const uint16_t TYPE, BlockArray& cblkarr, RuntimeContext& rctx):
        DescriptorSet(TYPE, cblkarr, RootDescriptorSet::Streams::END, rctx) {}

}  // namespace xoz
