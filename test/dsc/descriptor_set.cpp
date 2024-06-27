#include "xoz/dsc/descriptor_set.h"

#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/default.h"
#include "xoz/err/exceptions.h"
#include "xoz/repo/runtime_context.h"
#include "xoz/blk/segment_block_array.h"
#include "xoz/blk/vector_block_array.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include "xoz/repo/repository.h"
#include "xoz/io/iosegment.h"


#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing::ElementsAre;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, blk_sz_order, disk_sz, data_sz, segm_data_sz, obj_data_sz) do {      \
    EXPECT_EQ((dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ((dsc).calc_data_space_size(), (unsigned)(data_sz));                                  \
    EXPECT_EQ((dsc).calc_external_data_space_size((blk_sz_order)), (unsigned)(segm_data_sz));      \
    EXPECT_EQ((dsc).calc_external_data_size(), (unsigned)(obj_data_sz));       \
} while (0)

#define XOZ_EXPECT_SET_SERIALIZATION(blkarr, dset, data) do {       \
    auto sg = (dset).segment();                                     \
    EXPECT_EQ(hexdump(IOSegment((blkarr), sg)), (data));            \
} while (0)


#define XOZ_EXPECT_REPO_SERIALIZATION(repo, at, len, data) do {           \
    EXPECT_EQ(hexdump((repo).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(DescriptorSetTest, EmptySet) {
        RuntimeContext rctx({});

        // Data block array: this will be the block array that the set will
        // use to access "external data blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, ed_blkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        // Mandatory: we load the descriptors from the segment above (of course, none)
        dset.create_set();

        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: expected only its header with a 0x0000 checksum
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000"
                );

        // Load another set from the previous set's segment to see that
        // both are consistent each other
        DescriptorSet dset2(dset.segment(), d_blkarr, d_blkarr, rctx);
        dset2.load_set();

        // Header already written before, so no need to write it back (because it didn't change)
        EXPECT_EQ(dset2.count(), (uint32_t)0);
        EXPECT_EQ(dset2.does_require_write(), (bool)false);

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000"
                );
    }

    TEST(DescriptorSetTest, EmptySetNoDefaultConstruction) {
        RuntimeContext rctx({});

        // Data block array: this will be the block array that the set will
        // use to access "external data blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, ed_blkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        // Mandatory: we load the descriptors from the segment above (of course, none)
        dset.create_set(0x41);

        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: expected only its header with a 0x0000 checksum
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "4100 4100"
                );

        // Load another set from the previous set's segment to see that
        // both are consistent each other
        DescriptorSet dset2(dset.segment(), d_blkarr, d_blkarr, rctx);
        dset2.load_set();

        // Header already written before, so no need to write it back (because it didn't change)
        EXPECT_EQ(dset2.count(), (uint32_t)0);
        EXPECT_EQ(dset2.does_require_write(), (bool)false);

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "4100 4100"
                );
    }

    TEST(DescriptorSetTest, AddUpdateEraseDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);
        EXPECT_EQ(dset.get(id1)->get_owner(), (DescriptorSet*)(&dset));

        // Write down the set: we expect to see that single descriptor there
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        DescriptorSet dset2(dset.segment(), d_blkarr, d_blkarr, rctx);

        dset2.load_set();

        EXPECT_EQ(dset2.count(), (uint32_t)1);
        EXPECT_EQ(dset2.does_require_write(), (bool)false);

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 fa00 fa00"
                );

        // Mark the descriptor as modified so the set requires a new write
        dset.mark_as_modified(id1);

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);
        EXPECT_EQ(dset.get(id1)->get_owner(), (DescriptorSet*)(&dset));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        // Retrieve the descriptor object, change it a little, mark it as modified
        // and check that the set correctly updated the content (serialization)
        auto dscptr2 = dset.get<DefaultDescriptor>(id1);
        dscptr2->set_data({'A', 'B'});

        dset.mark_as_modified(id1);

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);
        EXPECT_EQ(dset.get(id1)->get_owner(), (DescriptorSet*)(&dset));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3b47 fa04 4142"
                );

        // Delete it
        dset.erase(id1);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // The deleted descriptors are left as padding.
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000"
                );

        // Release free space
        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000"
                );
    }

    TEST(DescriptorSetTest, GrowShrinkDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        dscptr->set_data({'A', 'B'});

        uint32_t id1 = dset.add(std::move(dscptr));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3b47 fa04 4142"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Replace descriptor's data
        auto dscptr2 = dset.get<DefaultDescriptor>(id1);
        dscptr2->set_data({'C', 'D'});

        dset.mark_as_modified(id1);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3d49 fa04 4344"
                );

        // Grow descriptor's data
        dscptr2->set_data({'A', 'B', 'C', 'D'});

        dset.mark_as_modified(id1);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7e8f fa08 4142 4344"
                );


        // Shrink descriptor's data
        dscptr2->set_data({'E', 'F'});

        dset.mark_as_modified(id1);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3f4b fa04 4546 0000"
                );


        // Shrink descriptor's data to zero
        dscptr2->set_data({});

        dset.mark_as_modified(id1);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00 0000 0000"
                );

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );
    }

    TEST(DescriptorSetTest, MoveDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.get(id1)->get_owner(), (DescriptorSet*)(&dset));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);

        dset2.create_set();

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000"
                );

        // Move the descriptor from dset to dset2
        dset.move_out(id1, dset2);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        EXPECT_EQ(dset2.count(), (uint32_t)1);
        EXPECT_EQ(dset2.does_require_write(), (bool)true);
        EXPECT_EQ(dset2.get(id1)->get_owner(), (DescriptorSet*)(&dset2));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 fa00 fa00"
                );
    }

    TEST(DescriptorSetTest, MoveModifiedDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);

        dset2.create_set();

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000"
                );

        // Modify the descriptor living in dset
        auto dscptr2 = dset.get<DefaultDescriptor>(id1);
        dscptr2->set_data({'A', 'B'});

        dset.mark_as_modified(id1);

        // Move the descriptor from dset to dset2
        dset.move_out(id1, dset2);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        EXPECT_EQ(dset2.count(), (uint32_t)1);
        EXPECT_EQ(dset2.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 3b47 fa04 4142"
                );
    }

    TEST(DescriptorSetTest, MoveThenModifyDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);

        dset2.create_set();

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000"
                );

        // Move the descriptor from dset to dset2
        dset.move_out(id1, dset2);

        // Modify the descriptor living in dset2
        auto dscptr2 = dset2.get<DefaultDescriptor>(id1);
        dscptr2->set_data({'A', 'B'});

        dset2.mark_as_modified(id1);


        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        EXPECT_EQ(dset2.count(), (uint32_t)1);
        EXPECT_EQ(dset2.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 3b47 fa04 4142"
                );
    }

    TEST(DescriptorSetTest, OwnExternalDataDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();
        dset.flush_writes();

        // Any descriptor set has a header of 4 bytes
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)1); // this block is for suballocation
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(2));

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 130,
            .segm = d_blkarr.allocator().alloc(130).add_end_of_segment() // <-- allocation here
        };

        // Check that the block array grew due the descriptor's external data (alloc 130 bytes)
        // plus the header of the set (4 bytes).
        // This requires 5 blocks, one for suballocation, with 3 subblocks allocated
        // (one for the external data and 2 for the header)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2));

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7f0b fa80 8200 0124 0086 0020 00c0"
                );
        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Check that the array grew further (in subblocks) due the write of the set
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 6));

        // Delete the descriptor: its external data blocks should be released too
        dset.erase(id1);
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000"
                );

        // Check that the array shrank to 4 bytes (no external blocks + no data in the set
        // but 4 bytes of header)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(2));
    }

    TEST(DescriptorSetTest, OwnExternalDataMovedDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();
        dset.flush_writes();

        // Any descriptor set has a header of 4 bytes
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)1); // this block is for suballocation
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(2));

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 130,
            .segm = d_blkarr.allocator().alloc(130).add_end_of_segment() // <-- allocation here
        };

        // Check that the block array grew due the descriptor's external data (alloc 130 bytes)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2));

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7f0b fa80 8200 0124 0086 0020 00c0"
                );
        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Check that the array grew further (in subblocks) due the write of the set
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 6));

        // Create another set
        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);
        dset2.create_set();
        dset2.flush_writes();

        // Check for the new descriptor set's header
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 6 + 2));

        // Move the descriptor from dset to dset2: while the descriptor is deleted from dset,
        // its external blocks should not be deallocated because the descriptor "moved" to
        // the other set.
        dset.move_out(id1, dset2);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 7f0b fa80 8200 0124 0086 0020 00c0"
                );

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000"
                );

        dset2.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 7f0b fa80 8200 0124 0086 0020 00c0"
                );

        // Expected no change: what the dset2 grew, the dset shrank and the external blocks
        // should not had changed at all.
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1 /* TODO */ + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 6 + 2));


        // Delete the descriptor: its external data blocks should be released too
        dset2.erase(id1);
        dset2.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        dset2.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000"
                );

        // Check that the array shrank to 8 bytes (no external blocks + no data in the set,
        // but 2 headers for 4 bytes each
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(2 + 2));
    }


    TEST(DescriptorSetTest, MultipleDescriptors) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);

        dset.create_set();
        dset2.create_set();

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let the descriptor set assign a new id each

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        {
            // Add descriptor 1, 2, 3 to dset
            // Note: we write the set each time we add a descriptor to make
            // the output determinisitc otherwise, if multiples descriptors
            // are pending to be added, there is no deterministic order
            // in which they will be written.
            dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
            dset.flush_writes();

            auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr2->set_data({'A', 'B'});
            uint32_t id2 = dset.add(std::move(dscptr2));
            dset.flush_writes();

            auto dscptr3 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr3->set_data({'C', 'D'});
            dset.add(std::move(dscptr3));
            dset.flush_writes();

            // Then, add a bunch of descriptors to dset2
            // Note: we add a bunch but we don't write the set until the end.
            // This tests that multiples descriptors can be added at once and because
            // all the descriptors are the same, it doesn't matter
            // the order and their output will still be deterministic.
            for (int i = 0; i < 2; ++i) {
                dset2.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
            }
            dset2.flush_writes();

            EXPECT_EQ(dset.get(id2)->get_owner(), (DescriptorSet*)(&dset));

            dset.move_out(id2, dset2);
            dset.flush_writes();
            dset2.flush_writes();

            EXPECT_EQ(dset2.get(id2)->get_owner(), (DescriptorSet*)(&dset2));

            for (int i = 0; i < 3; ++i) {
                dset2.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
            }
            dset2.flush_writes();
        }

        EXPECT_EQ(dset.count(), (uint32_t)2);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        EXPECT_EQ(dset2.count(), (uint32_t)6);
        EXPECT_EQ(dset2.does_require_write(), (bool)false);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 374a fa00 0000 0000 fa04 4344"
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 1d4c fa00 fa00 fa04 4142 fa00 fa00 fa00"
                );


        // While there are 2 bytes of padding in the set that could be reused,
        // they are not at the end of the set so they cannot be released as
        // free space.
        // The following does not change the set.
        dset.release_free_space();

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 374a fa00 0000 0000 fa04 4344"
                );


        // Find the last descriptor. It is the one that has 2 bytes of data ({'C', 'D'})
        uint32_t last_dsc_id = 0;
        for (auto it = dset.begin(); it != dset.end(); ++it) {
            if ((*it)->calc_data_space_size() == 2) {
                last_dsc_id = (*it)->id();
            }
        }

        // Delete it and release the free space
        dset.erase(last_dsc_id);
        dset.flush_writes();
        dset.release_free_space();

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );
    }

    TEST(DescriptorSetTest, Iterate) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let the descriptor set assign a new id each

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        {
            // Add descriptor 1, 2, 3 to dset. All except the last
            // are added *and* written; the last is added only
            // to test that even if still pending to be written
            // it can be accessed
            dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
            dset.flush_writes();

            auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr2->set_data({'A', 'B', 'C', 'D'});
            dset.add(std::move(dscptr2));
            dset.flush_writes();

            auto dscptr3 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr3->set_data({'C', 'D'});
            dset.add(std::move(dscptr3));
            // leave the set unwritten so dscptr3 is unwritten as well
        }

        EXPECT_EQ(dset.count(), (uint32_t)3);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        std::list<uint32_t> sizes;

        // Test that we can get the descriptors (order is no guaranteed)
        sizes.clear();
        for (auto it = dset.begin(); it != dset.end(); ++it) {
            sizes.push_back((*it)->calc_data_space_size());
        }

        sizes.sort(); // make the test deterministic
        EXPECT_THAT(sizes, ElementsAre(
                    (uint32_t)0,
                    (uint32_t)2,
                    (uint32_t)4
                    )
                );

        // Test that we can get the descriptors - const version
        sizes.clear();
        for (auto it = dset.cbegin(); it != dset.cend(); ++it) {
            sizes.push_back((*it)->calc_data_space_size());
        }

        sizes.sort(); // make the test deterministic
        EXPECT_THAT(sizes, ElementsAre(
                    (uint32_t)0,
                    (uint32_t)2,
                    (uint32_t)4
                    )
                );
    }

    TEST(DescriptorSetTest, AssignPersistentId) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // see above

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        // Let the set assign a temporal id
        hdr.id = 0x0;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));

        // The set should honor our temporal id
        hdr.id = 0x81f11f1f;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));

        // Let the set assign a persistent id for us, even if the id is a temporal one
        hdr.id = 0x0;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr), true);
        hdr.id = 0x81f11f10;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr), true);

        // The set should honor our persistent id, even if assign_persistent_id is true
        hdr.id = 0xff1;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
        hdr.id = 0xff2;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr), true);

        // Add a descriptor with a temporal id and then assign it a persistent id
        hdr.id = 0x80a0a0a0;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
        dset.assign_persistent_id(hdr.id);

        // Add a descriptor with a persistent id and then assign it a persistent id
        // This should have no effect
        hdr.id = 0xaff1;
        dset.add(std::make_unique<DefaultDescriptor>(hdr, d_blkarr));
        dset.assign_persistent_id(hdr.id);

        // Let's collect all the ids
        std::list<uint32_t> ids;

        for (auto it = dset.begin(); it != dset.end(); ++it) {
            ids.push_back((*it)->id());
        }

        ids.sort(); // make the test deterministic
        EXPECT_THAT(ids, ElementsAre(
                    (uint32_t)1,
                    (uint32_t)2,
                    (uint32_t)0xff1,
                    (uint32_t)0xff2,
                    (uint32_t)0xff3,
                    (uint32_t)0xaff1,
                    (uint32_t)0x80000000,
                    (uint32_t)0x81f11f1f
                    )
                );

        // check that all the persistent ids were registered
        EXPECT_EQ(rctx.is_registered(1), (bool)true);
        EXPECT_EQ(rctx.is_registered(2), (bool)true);
        EXPECT_EQ(rctx.is_registered(0xff1), (bool)true);
        EXPECT_EQ(rctx.is_registered(0xff2), (bool)true);
        EXPECT_EQ(rctx.is_registered(0xff3), (bool)true);
        EXPECT_EQ(rctx.is_registered(0xaff1), (bool)true);
    }

    class DescriptorSubRW : public DefaultDescriptor {
    public:
        DescriptorSubRW(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr) : DefaultDescriptor(hdr, ed_blkarr) {}
        void read_struct_specifics_from(IOBase&) override {
            return; // 0 read
        }
        void write_struct_specifics_into(IOBase&) override {
            return; // 0 write
        }
        static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr) {
            return std::make_unique<DescriptorSubRW>(hdr, ed_blkarr);
        }
    };

    TEST(DescriptorSetTest, DownCast) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Down cast to Descriptor subclass again
        // If the downcast works, cast<X> does neither throws nor return null
        auto dscptr2 = dset.get<DefaultDescriptor>(id1);
        EXPECT_EQ((bool)dscptr2, (bool)true);

        // If the downcast fails, throw an exception (it does not return null either)
        EXPECT_THAT(
            ensure_called_once([&]() {
                [[maybe_unused]]
                auto dscptr3 = dset.get<DescriptorSubRW>(id1);
                }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Descriptor cannot be dynamically down casted."
                        )
                    )
                )
        );

        // Only if we pass ret_null = true, the failed cast will return null
        // and avoid throwing.
        auto dscptr4 = dset.get<DescriptorSubRW>(id1, true);
        EXPECT_EQ((bool)dscptr4, (bool)false);

        // Getting a non-existing descriptor (id not found) is an error
        // and it does not matter if ret_null is true or not.
        EXPECT_THAT(
            ensure_called_once([&]() {
                [[maybe_unused]]
                auto dscptr3 = dset.get<DescriptorSubRW>(99);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x00000063 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                [[maybe_unused]]
                auto dscptr3 = dset.get<DescriptorSubRW>(99, true);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x00000063 does not belong to the set."
                        )
                    )
                )
        );
    }

    TEST(DescriptorSetTest, ClearRemoveEmptySet) {
        RuntimeContext rctx({});

        // Data block array: this will be the block array that the set will
        // use to access "external data blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, ed_blkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        // Mandatory: we load the descriptors from the segment above (of course, none)
        dset.create_set();

        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: expected only its header with a 0x0000 checksum
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000"
                );

        // Clear an empty set: no effect
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000"
                );

        // Remove the set removes also the header
        dset.remove_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, ClearRemoveEmptySetNeverWritten) {
        RuntimeContext rctx({});

        // Data block array: this will be the block array that the set will
        // use to access "external data blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, ed_blkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        // Mandatory: we load the descriptors from the segment above (of course, none)
        dset.create_set();

        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Clear an empty set: no effect and no error
        // The does_require_write() is still true because the header is still pending
        // to be written
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Remove the set does not fail if nothing was written before
        dset.remove_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, AddThenRemove) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Clear the set
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Another descriptor but this time, do not write it
        auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        dset.add(std::move(dscptr2));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Clear the set with pending writes (the addition).
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Remove the set removes also the header
        dset.remove_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, AddThenClearWithOwnExternalData) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();
        EXPECT_EQ(dset.segment().length(), (uint32_t)0); // nothing yet

        dset.flush_writes();
        EXPECT_EQ(dset.segment().length(), (uint32_t)1); // room for the header

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = d_blkarr.allocator().alloc(130).add_end_of_segment()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fd0a fa80 0000 0124 0086 0020 00c0"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);
        EXPECT_EQ(dset.segment().length(), (uint32_t)2); // room for the header + added descriptor

        // Check that we are using the expected block counts:
        //  - floor(130 / 32) blocks for the external data
        //  - 1 block for suballocation to hold:
        //    - the remaining of the external data (1 subblock)
        //    - the descriptor set (8 subblock, 16 bytes in total)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(8 + 1));

        // Clear the set
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // The set's segment is not empty because clear_set()+flush_writes() does not
        // shrink (aka release) the segment by default
        EXPECT_EQ(dset.segment().length(), (uint32_t)2);

        // The caller must explicitly call release_free_space(). Note the even it
        // the set is empty, its segment will not because there is some room
        // for its header
        dset.release_free_space();
        EXPECT_EQ(dset.segment().length(), (uint32_t)1);

        // We check that the external blocks were deallocated. Only 1 block
        // should remain that holds the descriptor set (header only).
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(2));
    }

    TEST(DescriptorSetTest, AddThenRemoveWithOwnExternalData) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();
        EXPECT_EQ(dset.segment().length(), (uint32_t)0); // nothing yet

        dset.flush_writes();
        EXPECT_EQ(dset.segment().length(), (uint32_t)1); // room for the header

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = d_blkarr.allocator().alloc(130).add_end_of_segment()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fd0a fa80 0000 0124 0086 0020 00c0"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);
        EXPECT_EQ(dset.segment().length(), (uint32_t)2); // room for the header + added descriptor

        // Check that we are using the expected block counts:
        //  - floor(130 / 32) blocks for the external data
        //  - 1 block for suballocation to hold:
        //    - the remaining of the external data (1 subblock)
        //    - the descriptor set (8 subblock, 16 bytes in total)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(8 + 1));

        // Remove the set, we expect that this will release the allocated blocks
        // and shrink the block array, thus, it will also make the set's segment empty
        // (not even a header is needed)
        dset.remove_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
        EXPECT_EQ(dset.segment().length(), (uint32_t)0);

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));
    }

    TEST(DescriptorSetTest, AddUpdateThenRemoveDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Mark the descriptor as modified so the set requires a new write
        dset.mark_as_modified(id1);

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        // Clear the set
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Another descriptor, write it, then modify it but do not write it again
        auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        auto id2 = dset.add(std::move(dscptr2));
        dset.flush_writes();
        dset.mark_as_modified(id2);

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Clear the set with pending writes (the update).
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Remove the set removes also the header
        dset.remove_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, AddEraseThenRemoveDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Delete the descriptor
        dset.erase(id1);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Clear the set: no change
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Another descriptor, write it, then delete it but do not write it again
        auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        auto id2 = dset.add(std::move(dscptr2));
        dset.flush_writes();
        dset.erase(id2);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Clear the set with pending writes (the deletion).
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // A second clear does not change anything
        dset.clear_set();
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        dset.flush_writes();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Remove the set removes also the header
        dset.remove_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, IncompatibleExternalBlockArray) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr_1(32);
        VectorBlockArray d_blkarr_2(32);
        d_blkarr_1.allocator().initialize_from_allocated(std::list<Segment>());
        d_blkarr_2.allocator().initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = d_blkarr_1.blk_sz_order();

        // Create set with two different block arrays, one for the descriptor set
        // the other for the external data.
        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr_1, d_blkarr_2, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr_1.blk_sz_order())
        };

        // Descriptor uses the same block array for the external data than
        // the set so it is OK.
        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr_2);
        dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();

        // This descriptor uses other block array, which makes the add() to fail
        hdr.id += 1;
        auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr_1);

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.add(std::move(dscptr2));
                }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "descriptor {id: 0x80000002, type: 250, dsize: 0} "
                        "claims to use a block array for external data at 0x"
                        ),
                    HasSubstr(
                        " but the descriptor set is using one at 0x"
                        )
                    )
                )
        );

        // The set didn't accept the descriptor
        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);
    }

    TEST(DescriptorSetTest, AddMoveFailDueDuplicatedId) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        auto id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // This descriptor uses the same id than the previous one
        // so the add should fail
        auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.add(std::move(dscptr2));
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "descriptor {id: 0x80000001, type: 250, dsize: 0} "
                        "has an id that collides with descriptor "
                        "{id: 0x80000001, type: 250, dsize: 0} "
                        "that it is already owned by the set"
                        )
                    )
                )
        );

        // The set didn't accept the descriptor
        EXPECT_EQ(dset.count(), (uint32_t)1);

        // Create another descriptor with the same id and store it in a different set
        // No problem because the new set does not know about the former.
        auto dscptr3 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);

        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);
        dset2.create_set();

        dset2.add(std::move(dscptr3));

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.move_out(hdr.id, dset2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "descriptor {id: 0x80000001, type: 250, dsize: 0} "
                        "has an id that collides with descriptor "
                        "{id: 0x80000001, type: 250, dsize: 0} "
                        "that it is already owned by the set"
                        )
                    )
                )
        );

        // On a failed move_out(), both sets will protect their descriptors
        EXPECT_EQ((bool)(dset.get(id1)), (bool)true);
        EXPECT_EQ((bool)(dset2.get(id1)), (bool)true);
    }

    TEST(DescriptorSetTest, IdDoesNotExist) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);
        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        // Store 1 descriptor and write it
        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        auto id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.flush_writes();

        // Add another descriptor but do not write it.
        hdr.id += 1;
        auto dscptr2 = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        auto id2 = dset.add(std::move(dscptr2));

        EXPECT_EQ(dset.count(), (uint32_t)2);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Now delete both descriptors and do not write it
        dset.erase(id1);
        dset.erase(id2);

        auto id3 = hdr.id + 1; // this descriptor never existed

        // Try to erase an id that does not exist
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.erase(id1);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000001 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.erase(id2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000002 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.erase(id3);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000003 does not belong to the set."
                        )
                    )
                )
        );

        // Try to modify an id that does not exist
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.mark_as_modified(id1);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000001 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.mark_as_modified(id2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000002 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.mark_as_modified(id3);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000003 does not belong to the set."
                        )
                    )
                )
        );

        // Try to move out an id that does not exist
        Segment sg2(blk_sz_order);
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, rctx);
        dset2.create_set();

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.move_out(id1, dset2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000001 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.move_out(id2, dset2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000002 does not belong to the set."
                        )
                    )
                )
        );
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset.move_out(id3, dset2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "Descriptor 0x80000003 does not belong to the set."
                        )
                    )
                )
        );
    }

    TEST(DescriptorSetTest, Mixed) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        DescriptorSet dset(sg, d_blkarr, d_blkarr, rctx);

        dset.create_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        // Add a bunch of descriptors
        std::vector<uint32_t> ids;
        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr->set_data({c, c});

            auto id = dset.add(std::move(dscptr), true);
            ids.push_back(id);
            dset.flush_writes();
        }

        // Reduce the set
        for (size_t i = 10; i < ids.size(); ++i) {
            dset.erase(ids[i]);
            dset.flush_writes();
        }

        // Reduce the set even more
        for (size_t i = 4; i < 10; ++i) {
            dset.erase(ids[i]);
            dset.flush_writes();
        }

        // Adding the erased descriptors back again
        for (size_t i = 4; i < 10; ++i) {
            char c = char('A' + i);
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr->set_data({c, c});

            auto id = dset.add(std::move(dscptr), true);
            ids[i] = id;
            dset.flush_writes();
        }

        // Now expand the set even further
        for (size_t i = 10; i < ids.size(); ++i) {
            char c = char('A' + i);
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
            dscptr->set_data({c, c});

            auto id = dset.add(std::move(dscptr), true);
            ids[i] = id;
            dset.flush_writes();
        }

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 8e9f "
                "fa06 0100 0000 4141 fa06 0200 0000 4242 "
                "fa06 0300 0000 4343 fa06 0400 0000 4444 "
                "fa06 1b00 0000 4545 fa06 1c00 0000 4646 "
                "fa06 1d00 0000 4747 fa06 1e00 0000 4848 "
                "fa06 1f00 0000 4949 fa06 2000 0000 4a4a "
                "fa06 2100 0000 4b4b fa06 2200 0000 4c4c "
                "fa06 2300 0000 4d4d fa06 2400 0000 4e4e "
                "fa06 2500 0000 4f4f fa06 2600 0000 5050 "
                "fa06 2700 0000 5151 fa06 2800 0000 5252 "
                "fa06 2900 0000 5353 fa06 2a00 0000 5454 "
                "fa06 2b00 0000 5555 fa06 2c00 0000 5656 "
                "fa06 2d00 0000 5757 fa06 2e00 0000 5858 "
                "fa06 2f00 0000 5959 fa06 3000 0000 5a5a"
                );

        // Load another set from the previous set's segment to see that
        // both are consistent each other
        rctx.reset();
        DescriptorSet dset2(dset.segment(), d_blkarr, d_blkarr, rctx);
        dset2.load_set();

        // Check that the set was loaded correctly
        for (int i = 0; i < (int)ids.size(); ++i) {
            char c = char('A' + i);
            auto dscptr = dset2.get<DefaultDescriptor>(ids[i]);
            auto data = dscptr->get_data();
            EXPECT_EQ(data.size(), (size_t)2);
            EXPECT_EQ(data[0], (char)c);
            EXPECT_EQ(data[1], (char)c);
        }
    }
}
