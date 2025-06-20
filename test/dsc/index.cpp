#include "xoz/dsc/descriptor_set.h"

#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "test/plain.h"
#include "xoz/err/exceptions.h"
#include "xoz/file/runtime_context.h"
#include "xoz/blk/segment_block_array.h"
#include "xoz/blk/vector_block_array.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include "xoz/file/file.h"
#include "xoz/io/iosegment.h"
#include "xoz/mem/inet_checksum.h"


#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing::ElementsAre;

using ::testing_xoz::PlainDescriptor;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

namespace {
    TEST(DescriptorFinderTest, FindByIdAndName) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        Segment subsg(blk_sz_order);
        auto subdset = DescriptorSet::create(subsg, d_blkarr, rctx);

        // Add one descriptor to the dset and another to the subdset
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr1 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = subdset->add(std::move(dscptr1));

        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id2 = dset->add(std::move(dscptr2), true); // persistent id

        // Add the subset to the main set
        uint32_t id3 = dset->add(std::move(subdset)); // temporal id

        // Count is not recursive: the set has only 2 direct descriptors
        EXPECT_EQ(dset->count(), (uint32_t)2);
        EXPECT_EQ(dset->does_require_write(), (bool)true);
        EXPECT_EQ(dset->get(id2)->get_owner(), std::addressof(*dset));
        EXPECT_EQ(dset->get(id3)->get_owner(), std::addressof(*dset));

        // Check subset (get a reference to it again because the ref subset was left
        // invalid after the std::move in the dset->add above)
        auto xsubdset = dset->get<DescriptorSet>(id3);
        EXPECT_EQ(xsubdset->count(), (uint32_t)1);
        EXPECT_EQ(xsubdset->get(id1)->get_owner(), std::addressof(*xsubdset));

        // See if we can find the descriptors using the index
        auto idmap = dset->create_and_add<IDMappingDescriptor>(false);
        rctx.index.init_index(*dset, idmap);
        EXPECT_EQ(rctx.index.find(id1)->get_owner(), std::addressof(*xsubdset));
        EXPECT_EQ(rctx.index.find(id2)->get_owner(), std::addressof(*dset));
        EXPECT_EQ(rctx.index.find(id3)->get_owner(), std::addressof(*dset));

        auto dsc = dset->get(id2);

        rctx.index.add_name("foo", id2, true);
        EXPECT_EQ(std::addressof(*rctx.index.find("foo")), std::addressof(*dsc));
        EXPECT_EQ(std::addressof(*rctx.index.find<PlainDescriptor>("foo")), std::addressof(*dsc));

        // "bar" does not exist
        EXPECT_THAT(
            ensure_called_once([&]() {
                rctx.index.find("bar");
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "No descriptor with name 'bar' was found."
                        )
                    )
                )
        );

        // This id does not exist either
        EXPECT_THAT(
            ensure_called_once([&]() {
                rctx.index.find(33);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x00000021 does not belong to any set."
                        )
                    )
                )
        );
    }

    TEST(DescriptorFinderTest, ManageNames) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        // Add one descriptor to the dset and another to the subdset
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id2 = dset->add(std::move(dscptr2), true); // persistent id

        auto dscptr3 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id3 = dset->add(std::move(dscptr3)); // temporal id

        auto dsc2 = dset->get(id2);
        auto dsc3 = dset->get(id3);

        // See if we can find the descriptors using the index
        auto idmap = dset->create_and_add<IDMappingDescriptor>(false);
        idmap->store({{"foo", id2}});
        rctx.index.init_index(*dset, idmap);
        EXPECT_EQ(std::addressof(*rctx.index.find(id2)), std::addressof(*dsc2));
        EXPECT_EQ(std::addressof(*rctx.index.find(id3)), std::addressof(*dsc3));
        EXPECT_EQ(std::addressof(*rctx.index.find("foo")), std::addressof(*dsc2));

        // Adding new names require the descriptor to have a persistent id otherwise it fails
        EXPECT_THAT(
            ensure_called_once([&]() {
                rctx.index.add_name("bar", id3);
                }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Temporal ids cannot be registered."
                        )
                    )
                )
        );

        // Prove that the descriptor id3 cannot be found by the name 'bar;
        // (the add_name above failed)
        EXPECT_EQ(rctx.index.contains("bar"), (bool)false);

        // Assign a new persistent id for descriptor 3
        id3 = dset->assign_persistent_id(id3);

        // Add new names
        rctx.index.add_name("bar", id3);
        EXPECT_EQ(std::addressof(*rctx.index.find("bar")), std::addressof(*dsc3));

        // Same descriptor can have multiple names
        rctx.index.add_name("baz", dsc3);
        EXPECT_EQ(std::addressof(*rctx.index.find("bar")), std::addressof(*dsc3));
        EXPECT_EQ(std::addressof(*rctx.index.find("baz")), std::addressof(*dsc3));

        // A name can be reassigned to the same descriptor
        rctx.index.add_name("bar", id3);

        // But cannot be reassigned to another descriptor (leaving the former "unnamed")
        EXPECT_THAT(
            ensure_called_once([&]() {
                rctx.index.add_name("bar", id2);
                }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "The name 'bar' is already in use by another descriptor (0x00000002) "
                        "and cannot be assigned to descriptor 0x00000001."
                        )
                    )
                )
        );

        // It is possible however to override
        rctx.index.add_name("bar", id2, true);
        EXPECT_EQ(std::addressof(*rctx.index.find("bar")), std::addressof(*dsc2));
        EXPECT_EQ(std::addressof(*rctx.index.find("baz")), std::addressof(*dsc3));

        // Names can be deleted once but not twice
        rctx.index.delete_name("bar");
        EXPECT_THAT(
            ensure_called_once([&]() {
                rctx.index.delete_name("bar");
                }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "The name 'bar' was not found."
                        )
                    )
                )
        );

        // this calls to idmap->store() under the hood
        rctx.index.flush(idmap);

        auto mapping = idmap->load();
        EXPECT_EQ(mapping.size(), (size_t)2);
        EXPECT_EQ(mapping["foo"], (uint32_t)id2);
        EXPECT_EQ(mapping["baz"], (uint32_t)id3);

        // Temporal names are names that can be used to find descriptors but
        // the mapping is not stored
        rctx.index.add_temporal_name("~zap", id2, true);
        EXPECT_EQ(std::addressof(*rctx.index.find("~zap")), std::addressof(*dsc2));

        // this calls to idmap->store() under the hood, ~zap however should not be stored
        rctx.index.flush(idmap);

        auto mapping2 = idmap->load();
        EXPECT_EQ(mapping2.size(), (size_t)2); // ~zap is not present
        EXPECT_EQ(mapping2["foo"], (uint32_t)id2);
        EXPECT_EQ(mapping2["baz"], (uint32_t)id3);
    }
}
