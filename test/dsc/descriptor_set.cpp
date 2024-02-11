#include "xoz/dsc/descriptor_set.h"

#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/default.h"
#include "xoz/err/exceptions.h"
#include "xoz/repo/id_manager.h"
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

#define XOZ_EXPECT_SET_SERIALIZATION(blkarr, sg, data) do {         \
    EXPECT_EQ(hexdump(IOSegment((blkarr), (sg))), (data));          \
} while (0)


#define XOZ_EXPECT_REPO_SERIALIZATION(repo, at, len, data) do {           \
    EXPECT_EQ(hexdump((repo).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(DescriptorSetTest, EmptySet) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        // Data block array: this will be the block array that the set will
        // use to access "external data blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, ed_blkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        // Mandatory: we load the descriptors from the segment above (of course, none)
        dset.load_set();

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Write down the set: expected an empty set
        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                ""
                );

        // Load another set from the previous set's segment to see that
        // both are consistent each other
        DescriptorSet dset2(sg, d_blkarr, d_blkarr, idmgr);
        dset2.load_set();

        EXPECT_EQ(dset2.count(), (uint32_t)0);
        EXPECT_EQ(dset2.does_require_write(), (bool)false);

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                ""
                );
    }

    TEST(DescriptorSetTest, AddUpdateEraseDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        uint32_t id1 = dset.add(std::move(dscptr));

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        DescriptorSet dset2(sg, d_blkarr, d_blkarr, idmgr);

        dset2.load_set();

        EXPECT_EQ(dset2.count(), (uint32_t)1);
        EXPECT_EQ(dset2.does_require_write(), (bool)false);

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );

        // Mark the descriptor as modified so the set requires a new write
        dset.mark_as_modified(id1);

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );

        // Retrieve the descriptor object, change it a little, mark it as modified
        // and check that the set correctly updated the content (serialization)
        auto dscptr2 = dset.get<DefaultDescriptor>(id1);
        dscptr2->set_data({'A', 'B'});

        dset.mark_as_modified(id1);

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa04 4142"
                );

        // Delete it
        dset.erase(id1);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        // The deleted descriptors are left as padding.
        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "0000 0000"
                );

        // Release free space
        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                ""
                );
    }

    TEST(DescriptorSetTest, GrowShrinkDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        dscptr->set_data({'A', 'B'});

        uint32_t id1 = dset.add(std::move(dscptr));

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa04 4142"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Replace descriptor's data
        auto dscptr2 = dset.get<DefaultDescriptor>(id1);
        dscptr2->set_data({'C', 'D'});

        dset.mark_as_modified(id1);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa04 4344"
                );

        // Grow descriptor's data
        dscptr2->set_data({'A', 'B', 'C', 'D'});

        dset.mark_as_modified(id1);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa08 4142 4344"
                );


        // Shrink descriptor's data
        dscptr2->set_data({'E', 'F'});

        dset.mark_as_modified(id1);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa04 4546 0000"
                );


        // Shrink descriptor's data to zero
        dscptr2->set_data({});

        dset.mark_as_modified(id1);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00 0000 0000"
                );

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );
    }

    TEST(DescriptorSetTest, MoveDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        Segment sg2;
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, idmgr);

        dset2.load_set();

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                ""
                );

        // Move the descriptor from dset to dset2
        dset.move_out(id1, dset2);

        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)true);

        EXPECT_EQ(dset2.count(), (uint32_t)1);
        EXPECT_EQ(dset2.does_require_write(), (bool)true);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "0000"
                );

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                "fa00"
                );
    }

    TEST(DescriptorSetTest, MoveModifiedDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        Segment sg2;
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, idmgr);

        dset2.load_set();

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                ""
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

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "0000"
                );

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                "fa04 4142"
                );
    }

    TEST(DescriptorSetTest, MoveThenModifyDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa00"
                );

        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        Segment sg2;
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, idmgr);

        dset2.load_set();

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                ""
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

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "0000"
                );

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                "fa04 4142"
                );
    }

    TEST(DescriptorSetTest, OwnExternalDataDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(0));

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = d_blkarr.allocator().alloc(130)
        };

        // Check that the block array grew due the descriptor's external data (alloc 130 bytes)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(1));

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa80 0000 0024 0084 0080"
                );
        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Check that the array grew further (in subblocks) due the write of the set
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(1 + 5));

        // Delete the descriptor: its external data blocks should be released too
        dset.erase(id1);
        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(dset.count(), (uint32_t)0);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                ""
                );

        // Check that the array shrank to 0 (no external blocks + no data in the set)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(0));
    }

    TEST(DescriptorSetTest, OwnExternalDataMovedDescriptor) {
        IDManager idmgr;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(0));

        Segment sg;
        DescriptorSet dset(sg, d_blkarr, d_blkarr, idmgr);

        dset.load_set();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = d_blkarr.allocator().alloc(130)
        };

        // Check that the block array grew due the descriptor's external data (alloc 130 bytes)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(1));

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr);
        uint32_t id1 = dset.add(std::move(dscptr));

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "fa80 0000 0024 0084 0080"
                );
        EXPECT_EQ(dset.count(), (uint32_t)1);
        EXPECT_EQ(dset.does_require_write(), (bool)false);

        // Check that the array grew further (in subblocks) due the write of the set
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(1 + 5));

        // Create another set
        Segment sg2;
        DescriptorSet dset2(sg2, d_blkarr, d_blkarr, idmgr);
        dset2.load_set();

        // Move the descriptor from dset to dset2: while the descriptor is deleted from dset,
        // its external blocks should not be deallocated because the descriptor "moved" to
        // the other set.
        dset.move_out(id1, dset2);

        dset.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                "0000 0000 0000 0000 0000"
                );

        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                "fa80 0000 0024 0084 0080"
                );

        dset.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg,
                ""
                );

        dset2.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                "fa80 0000 0024 0084 0080"
                );

        // Expected no change: what the dset2 grew, the dset shrank and the external blocks
        // should not had changed at all.
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(1 + 5));


        // Delete the descriptor: its external data blocks should be released too
        dset2.erase(id1);
        dset2.write_set();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                "0000 0000 0000 0000 0000"
                );

        dset2.release_free_space();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, sg2,
                ""
                );

        // Check that the array shrank to 0 (no external blocks + no data in the set)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().in_use_subblk_cnt, (uint32_t)(0));
    }
}
