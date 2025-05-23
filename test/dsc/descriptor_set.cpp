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

#include "xoz/dsc/spy.h"

#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing::ElementsAre;

using ::testing_xoz::PlainDescriptor;
using ::testing_xoz::PlainWithImplContentDescriptor;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

namespace {
const size_t FP_SZ = 224;
}

typedef ::xoz::dsc::internals::DescriptorInnerSpyForTesting DSpy;

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check that the serialization of the obj in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION(fp, dsc, data) do {                                 \
    EXPECT_EQ(hexdump((fp), 0, DSpy(dsc).calc_struct_footprint_size()), (data));         \
    EXPECT_EQ(are_all_zeros((fp), DSpy(dsc).calc_struct_footprint_size()), (bool)true);  \
} while (0)

// Calc checksum over the fp (bytes) and expect to be the same as the descriptor's checksum
// Note: this requires a load_struct_from/write_struct_into call before to make
// the descriptor's checksum updated
#define XOZ_EXPECT_CHECKSUM(fp, dsc) do {    \
    EXPECT_EQ(inet_checksum((uint8_t*)(fp).data(), DSpy(dsc).calc_struct_footprint_size()), (dsc).checksum); \
} while (0)

// Load from fp the obj and serialize it back again into
// a temporal fp2 stream. Then compare both (they should be the same)
#define XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), (rctx), (cblkarr));   \
    checksum2 = dsc2_ptr->checksum;                                      \
    dsc2_ptr->checksum = 0;                                              \
    auto dset = dsc2_ptr->cast<DescriptorSet>(true);                     \
    if (dset) { dset->load_set(); }                                      \
    dsc2_ptr->write_struct_into(IOSpan(buf2), (rctx));                   \
    checksum3 = dsc2_ptr->checksum;                                      \
    EXPECT_EQ((fp), buf2);                                               \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, disk_sz, idata_sz, cdata_sz, obj_data_sz) do {      \
    EXPECT_EQ(DSpy(dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ(DSpy(dsc).calc_internal_data_space_size(), (unsigned)(idata_sz));                                  \
    EXPECT_EQ(DSpy(dsc).calc_segm_data_space_size(0), (unsigned)(cdata_sz));      \
    EXPECT_EQ(DSpy(dsc).calc_declared_hdr_csize(0), (unsigned)(obj_data_sz));       \
} while (0)

#define XOZ_EXPECT_DSC_SERIALIZATION(blkarr, sg, data) do {         \
    EXPECT_EQ(hexdump(IOSegment((blkarr), (sg))), (data));          \
} while (0)


#define XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

#define XOZ_EXPECT_SET(dset, cnt, pending) do {                      \
    EXPECT_EQ((dset)->count(), (uint32_t)(cnt));                         \
    /* TODO we changed *when* we are required to do a write, this must be reviewed once we have the dset final version
     * EXPECT_EQ((dset)->does_require_write(), (bool)(pending));*/   \
} while (0)

#define XOZ_EXPECT_SET_SERIALIZATION(blkarr, dset, data) do {       \
    auto sg = (dset)->segment();                                     \
    EXPECT_EQ(hexdump(IOSegment((blkarr), sg)), (data));            \
} while (0)


#define XOZ_EXPECT_REPO_SERIALIZATION(xfile, at, len, data) do {           \
    EXPECT_EQ(hexdump((xfile).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(DescriptorSetTest, EmptySet) {
        RuntimeContext rctx({});

        // Data block array: this will be the block array that the set will
        // use to access "content data blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, cblkarr and sg_blkarr.
        // But currently DescriptorSet only accept one single blkarray as parameter
        // so work for both purposes.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        // 0 descriptors by default
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Write down the set: expected nothing because the set is empty.
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Load another set from the previous set's segment to see that
        // both are consistent each other
        auto dset2 = DescriptorSet::create(dset->segment(), d_blkarr, rctx);

        // Header already written before, so no need to write it back (because it didn't change)
        EXPECT_EQ(dset2->count(), (uint32_t)0);
        EXPECT_EQ(dset2->does_require_write(), (bool)false);

        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );
    }


    TEST(DescriptorSetTest, AddUpdateEraseDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);
        EXPECT_EQ(dset->get(id1)->get_owner(), std::addressof(*dset));

        // Write down the set: we expect to see that single descriptor there
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        auto dset2 = DescriptorSet::create(dset->segment(), d_blkarr, rctx);


        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->does_require_write(), (bool)false);

        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 fa00 fa00"
                );

        // Mark the descriptor as modified so the set requires a new write
        dset->mark_as_modified(id1);

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);
        EXPECT_EQ(dset->get(id1)->get_owner(), std::addressof(*dset));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        // Retrieve the descriptor object, change it a little, mark it as modified
        // and check that the set correctly updated the content (serialization)
        auto dscptr2 = dset->get<PlainDescriptor>(id1);
        dscptr2->set_idata({'A', 'B'});

        dset->mark_as_modified(id1);

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);
        EXPECT_EQ(dset->get(id1)->get_owner(), std::addressof(*dset));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3b47 fa04 4142"
                );

        // Delete it
        dset->erase(id1);

        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // The deleted descriptors are left as padding.
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000"
                );

        // Release free space
        dset->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, GrowShrinkDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        dscptr->set_idata({'A', 'B'});

        uint32_t id1 = dset->add(std::move(dscptr));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3b47 fa04 4142"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Replace descriptor's data
        auto dscptr2 = dset->get<PlainDescriptor>(id1);
        dscptr2->set_idata({'C', 'D'});

        dset->mark_as_modified(id1);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3d49 fa04 4344"
                );

        // Grow descriptor's data
        dscptr2->set_idata({'A', 'B', 'C', 'D'});

        dset->mark_as_modified(id1);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7e8f fa08 4142 4344"
                );


        // Shrink descriptor's data
        dscptr2->set_idata({'E', 'F'});

        dset->mark_as_modified(id1);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 3f4b fa04 4546 0000"
                );


        // Shrink descriptor's data to zero
        dscptr2->set_idata({});

        dset->mark_as_modified(id1);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00 0000 0000"
                );

        dset->full_sync(true);
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->get(id1)->get_owner(), std::addressof(*dset));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);


        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );

        // Move the descriptor from dset to dset2
        dset->move_out(id1, dset2);

        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->does_require_write(), (bool)true);
        EXPECT_EQ(dset2->get(id1)->get_owner(), std::addressof(*dset2));

        // The dset set is empty but it still has the same space allocated,
        // overridden with zeros.
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        dset->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        dset2->full_sync(false);
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);


        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );

        // Modify the descriptor living in dset
        auto dscptr2 = dset->get<PlainDescriptor>(id1);
        dscptr2->set_idata({'A', 'B'});

        dset->mark_as_modified(id1);

        // Move the descriptor from dset to dset2
        dset->move_out(id1, dset2);

        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 3b47 fa04 4142"
                );

        dset->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, MoveThenModifyDescriptor) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);


        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );

        // Move the descriptor from dset to dset2
        dset->move_out(id1, dset2);

        // Modify the descriptor living in dset2
        auto dscptr2 = dset2->get<PlainDescriptor>(id1);
        dscptr2->set_idata({'A', 'B'});

        dset2->mark_as_modified(id1);


        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 3b47 fa04 4142"
                );

        dset->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        dset->full_sync(false);

        // Any descriptor set has a header of 4 bytes but the set is empty so no header
        // is written
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0); // this block is for suballocation
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {
                {
                    .s = {
                        .pending = false,
                        .future_csize = 0,
                    },
                    .csize = 130,
                    .segm = d_blkarr.allocator().alloc(130).add_end_of_segment(), // <-- allocation here
                }
            }
        };

        // Check that the block array grew due the descriptor's content (alloc 130 bytes)
        // This requires 5 blocks, one for suballocation, with 1 subblocks allocated
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1));

        auto dscptr = std::make_unique<PlainWithImplContentDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7e69 fa80 0000 8200 0024 0084 0080 00c0"
                );
        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Check that the array grew further (in subblocks) due the write of the set (including set's header)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 7));

        // Delete the descriptor: its content blocks should be released too
        dset->erase(id1);
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        dset->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Check that the array shrank to 0 bytes (no desc, and no header due the empty set)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        dset->full_sync(false);

        // nothing, no header yet
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0); // this block is for suballocation
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {
                {
                    .s = {
                        .pending = false,
                        .future_csize = 0,
                    },
                    .csize = 130,
                    .segm = d_blkarr.allocator().alloc(130).add_end_of_segment(), // <-- allocation here
                }
            }
        };

        // Check that the block array grew due the descriptor's content (alloc 130 bytes)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1));

        auto dscptr = std::make_unique<PlainWithImplContentDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7e69 fa80 0000 8200 0024 0084 0080 00c0"
                );
        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Check that the array grew further (in subblocks) due the write of the set
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 7));

        // Create another set
        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);
        dset2->full_sync(false);

        // Check for the new descriptor set that no header is written (set is empty)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 2 + 7));

        // Move the descriptor from dset to dset2: while the descriptor is deleted from dset,
        // its external blocks should not be deallocated because the descriptor "moved" to
        // the other set.
        dset->move_out(id1, dset2);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 7e69 fa80 0000 8200 0024 0084 0080 00c0"
                );

        dset->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        dset2->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 7e69 fa80 0000 8200 0024 0084 0080 00c0"
                );

        // Expected no change: what the dset2 grew, the dset shrank and the external blocks
        // should not had changed at all
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1 /* TODO */ + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(1 + 7 + 2));


        // Delete the descriptor: its content blocks should be released too
        dset2->erase(id1);
        dset2->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        dset2->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );

        // Check that the array shrank to 0 bytes (no external blocks + no data in the set,
        // and no headers)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);


        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0, // let the descriptor set assign a new id each

            .isize = 0,
            .cparts = {}
        };

        {
            // Add descriptor 1, 2, 3 to dset
            // Note: we write the set each time we add a descriptor to make
            // the output determinisitc otherwise, if multiples descriptors
            // are pending to be added, there is no deterministic order
            // in which they will be written.
            dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
            dset->full_sync(false);

            auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr2->set_idata({'A', 'B'});
            uint32_t id2 = dset->add(std::move(dscptr2));
            dset->full_sync(false);

            auto dscptr3 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr3->set_idata({'C', 'D'});
            dset->add(std::move(dscptr3));
            dset->full_sync(false);

            // Then, add a bunch of descriptors to dset2
            // Note: we add a bunch but we don't write the set until the end.
            // This tests that multiples descriptors can be added at once and because
            // all the descriptors are the same, it doesn't matter
            // the order and their output will still be deterministic.
            for (int i = 0; i < 2; ++i) {
                dset2->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
            }
            dset2->full_sync(false);

            EXPECT_EQ(dset->get(id2)->get_owner(), std::addressof(*dset));

            dset->move_out(id2, dset2);
            dset->full_sync(false);
            dset2->full_sync(false);

            EXPECT_EQ(dset2->get(id2)->get_owner(), std::addressof(*dset2));

            for (int i = 0; i < 3; ++i) {
                dset2->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
            }
            dset2->full_sync(false);
        }

        EXPECT_EQ(dset->count(), (uint32_t)2);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        EXPECT_EQ(dset2->count(), (uint32_t)6);
        EXPECT_EQ(dset2->does_require_write(), (bool)false);

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
        dset->full_sync(true);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 374a fa00 0000 0000 fa04 4344"
                );


        // Find the last descriptor. It is the one that has 2 bytes of data ({'C', 'D'})
        uint32_t last_dsc_id = 0;
        for (auto it = dset->begin(); it != dset->end(); ++it) {
            if (DSpy(*(*it)).calc_internal_data_space_size() == 2) {
                last_dsc_id = (*it)->id();
            }
        }

        // Delete it and release the free space
        dset->erase(last_dsc_id);
        dset->full_sync(false);
        dset->full_sync(true);

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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0, // let the descriptor set assign a new id each

            .isize = 0,
            .cparts = {}
        };

        {
            // Add descriptor 1, 2, 3 to dset-> All except the last
            // are added *and* written; the last is added only
            // to test that even if still pending to be written
            // it can be accessed
            dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
            dset->full_sync(false);

            auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr2->set_idata({'A', 'B', 'C', 'D'});
            dset->add(std::move(dscptr2));
            dset->full_sync(false);

            auto dscptr3 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr3->set_idata({'C', 'D'});
            dscptr3->full_sync(false); // ensure we get the correct sizes (for testing)
            dset->add(std::move(dscptr3));
            // leave the set unwritten so dscptr3 is unwritten as well
        }

        EXPECT_EQ(dset->count(), (uint32_t)3);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        std::list<uint32_t> sizes;

        // Test that we can get the descriptors (order is no guaranteed)
        sizes.clear();
        for (auto it = dset->begin(); it != dset->end(); ++it) {
            sizes.push_back(DSpy(*(*it)).calc_internal_data_space_size());
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
        for (auto it = dset->cbegin(); it != dset->cend(); ++it) {
            sizes.push_back(DSpy(*(*it)).calc_internal_data_space_size());
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0, // see above

            .isize = 0,
            .cparts = {}

        };

        // Let the set assign a temporal id
        hdr.id = 0x0;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));

        // The set should honor our temporal id
        hdr.id = 0x81f11f1f;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));

        // Let the set assign a persistent id for us, even if the id is a temporal one
        hdr.id = 0x0;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr), true);
        hdr.id = 0x81f11f10;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr), true);

        // The set should honor our persistent id, even if assign_persistent_id is true
        hdr.id = 0xff1;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
        hdr.id = 0xff2;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr), true);

        // Add a descriptor with a temporal id and then assign it a persistent id
        hdr.id = 0x80a0a0a0;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
        dset->assign_persistent_id(hdr.id);

        // Add a descriptor with a persistent id and then assign it a persistent id
        // This should have no effect
        hdr.id = 0xaff1;
        dset->add(std::make_unique<PlainDescriptor>(hdr, d_blkarr));
        dset->assign_persistent_id(hdr.id);

        // Let's collect all the ids
        std::list<uint32_t> ids;

        for (auto it = dset->begin(); it != dset->end(); ++it) {
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
        EXPECT_EQ(rctx.idmgr.is_registered(1), (bool)true);
        EXPECT_EQ(rctx.idmgr.is_registered(2), (bool)true);
        EXPECT_EQ(rctx.idmgr.is_registered(0xff1), (bool)true);
        EXPECT_EQ(rctx.idmgr.is_registered(0xff2), (bool)true);
        EXPECT_EQ(rctx.idmgr.is_registered(0xff3), (bool)true);
        EXPECT_EQ(rctx.idmgr.is_registered(0xaff1), (bool)true);
    }

    class Dummy : public Descriptor {
    public:
        Dummy(const struct Descriptor::header_t& hdr, BlockArray& cblkarr) : Descriptor(hdr, cblkarr, 0) {}
        void read_struct_specifics_from(IOBase&) override {}
        void write_struct_specifics_into(IOBase&) override {}
        void update_isize([[maybe_unused]] uint64_t& isize) override { }
        static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr) {
            return std::make_unique<Dummy>(hdr, cblkarr);
        }
    };

    TEST(DescriptorSetTest, DownCast) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Down cast to Descriptor subclass again
        // If the downcast works, cast<X> does neither throws nor return null
        auto dscptr2 = dset->get<PlainDescriptor>(id1);
        EXPECT_EQ((bool)dscptr2, (bool)true);

        // If the downcast fails, throw an exception (it does not return null either)
        EXPECT_THAT(
            ensure_called_once([&]() {
                [[maybe_unused]]
                auto dscptr3 = dset->get<Dummy>(id1);
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
        auto dscptr4 = dset->get<Dummy>(id1, true);
        EXPECT_EQ((bool)dscptr4, (bool)false);

        // Getting a non-existing descriptor (id not found) is an error
        // and it does not matter if ret_null is true or not.
        EXPECT_THAT(
            ensure_called_once([&]() {
                [[maybe_unused]]
                auto dscptr3 = dset->get<Dummy>(99);
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
                auto dscptr3 = dset->get<Dummy>(99, true);
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
        // use to access "content blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, cblkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Write down the set: nothing should be written, the set is empty
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Clear an empty set: no effect
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Remove the set removes also the header
        dset->destroy();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
    }

    TEST(DescriptorSetTest, ClearRemoveEmptySetNeverWritten) {
        RuntimeContext rctx({});

        // Data block array: this will be the block array that the set will
        // use to access "content blocks" *and* to access its own
        // segment. In DescriptorSet's parlance, cblkarr and sg_blkarr.
        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // 0 descriptors by default
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Clear an empty set: no effect and no error
        // The does_require_write() is still true because the header is still pending
        // to be written
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        // Remove the set does not fail if nothing was written before
        dset->destroy();
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Clear the set
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Another descriptor but this time, do not write it
        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr2));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Clear the set with pending writes (the addition).
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Remove the set removes also the header
        dset->destroy();
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        EXPECT_EQ(dset->segment().length(), (uint32_t)0); // nothing yet

        dset->full_sync(false);
        EXPECT_EQ(dset->segment().length(), (uint32_t)0);

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {
                {
                    .s = {
                        .pending = false,
                        .future_csize = 0,
                    },
                    .csize = 130,
                    .segm = d_blkarr.allocator().alloc(130).add_end_of_segment(), // <-- allocation here
                }
            }
        };

        auto dscptr = std::make_unique<PlainWithImplContentDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7e69 fa80 0000 8200 0024 0084 0080 00c0"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);
        EXPECT_EQ(dset->segment().length(), (uint32_t)1); // room for the header + added descriptor

        // Check that we are using the expected block counts:
        //  - floor(130 / 32) blocks for the content
        //  - 1 block for suballocation to hold:
        //    - the remaining of the content (1 subblock)
        //    - the descriptor set (9 subblock, 16 bytes in total)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(9 + 1));

        // Clear the set
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // The set's segment is not empty because clear_set()+full_sync(false) does not
        // shrink (aka release) the segment by default
        EXPECT_EQ(dset->segment().length(), (uint32_t)1);

        // The caller must explicitly call full_sync(true).
        // We expect to see an empty segment as the header should had be removed too
        dset->full_sync(true);
        EXPECT_EQ(dset->segment().length(), (uint32_t)0);

        // We check that the external blocks were deallocated.
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(0));
    }

    TEST(DescriptorSetTest, AddThenRemoveWithOwnExternalData) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        EXPECT_EQ(dset->segment().length(), (uint32_t)0); // nothing yet

        dset->full_sync(false);
        EXPECT_EQ(dset->segment().length(), (uint32_t)0); // nothing again

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {
                {
                    .s = {
                        .pending = false,
                        .future_csize = 0,
                    },
                    .csize = 130,
                    .segm = d_blkarr.allocator().alloc(130).add_end_of_segment(), // <-- allocation here
                }
            }
        };

        auto dscptr = std::make_unique<PlainWithImplContentDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 7e69 fa80 0000 8200 0024 0084 0080 00c0"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);
        EXPECT_EQ(dset->segment().length(), (uint32_t)1); // room for the header + added descriptor

        // Check that we are using the expected block counts:
        //  - floor(130 / 32) blocks for the content
        //  - 1 block for suballocation to hold:
        //    - the remaining of the content (1 subblock)
        //    - the descriptor set (9 subblock, 16 bytes in total)
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();
        EXPECT_EQ(d_blkarr.blk_cnt(), (uint32_t)(130 / 32) + 1);
        EXPECT_EQ(d_blkarr.allocator().stats().current.in_use_subblk_cnt, (uint32_t)(9 + 1));

        // Remove the set, we expect that this will release the allocated blocks
        // and shrink the block array, thus, it will also make the set's segment empty
        // (not even a header is needed)
        dset->destroy();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
        EXPECT_EQ(dset->segment().length(), (uint32_t)0);

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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Mark the descriptor as modified so the set requires a new write
        dset->mark_as_modified(id1);

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        // Clear the set
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Another descriptor, write it, then modify it but do not write it again
        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        auto id2 = dset->add(std::move(dscptr2));
        dset->full_sync(false);
        dset->mark_as_modified(id2);

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Clear the set with pending writes (the update).
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Remove the set removes also the header
        dset->destroy();
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
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Write down the set: we expect to see that single descriptor there
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        // Delete the descriptor
        dset->erase(id1);

        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Clear the set: no change
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Another descriptor, write it, then delete it but do not write it again
        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        auto id2 = dset->add(std::move(dscptr2));
        dset->full_sync(false);
        dset->erase(id2);

        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Clear the set with pending writes (the deletion).
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // A second clear does not change anything
        dset->clear_set();
        EXPECT_EQ(dset->count(), (uint32_t)0);
        EXPECT_EQ(dset->does_require_write(), (bool)false);

        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0000 0000"
                );

        // Remove the set removes also the header
        dset->destroy();
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
        // the other for the content.
        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr_2, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        // Descriptor uses the same block array for the content than
        // the set so it is OK.
        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr_2);
        dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);

        // This descriptor uses other block array, which makes the add() to fail
        hdr.id += 1;
        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr_1);

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset->add(std::move(dscptr2));
                }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "descriptor {id: 0x80000002, type: 250, isize: 0} "
                        "claims to use a block array for content at 0x"
                        ),
                    HasSubstr(
                        " but the descriptor set is using one at 0x"
                        )
                    )
                )
        );

        // The set didn't accept the descriptor
        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)false);
    }

    TEST(DescriptorSetTest, AddMoveFailDueDuplicatedId) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        auto id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // This descriptor uses the same id than the previous one
        // so the add should fail
        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset->add(std::move(dscptr2));
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "descriptor {id: 0x80000001, type: 250, isize: 0} "
                        "has an id that collides with descriptor "
                        "{id: 0x80000001, type: 250, isize: 0} "
                        "that it is already owned by the set"
                        )
                    )
                )
        );

        // The set didn't accept the descriptor
        EXPECT_EQ(dset->count(), (uint32_t)1);

        // Create another descriptor with the same id and store it in a different set
        // No problem because the new set does not know about the former.
        auto dscptr3 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);

        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);

        dset2->add(std::move(dscptr3));

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset->move_out(hdr.id, dset2);
                }),
            ThrowsMessage<std::invalid_argument>(
                AllOf(
                    HasSubstr(
                        "descriptor {id: 0x80000001, type: 250, isize: 0} "
                        "has an id that collides with descriptor "
                        "{id: 0x80000001, type: 250, isize: 0} "
                        "that it is already owned by the set"
                        )
                    )
                )
        );

        // On a failed move_out(), both sets will protect their descriptors
        EXPECT_EQ((bool)(dset->get(id1)), (bool)true);
        EXPECT_EQ((bool)(dset2->get(id1)), (bool)true);
    }

    TEST(DescriptorSetTest, IdDoesNotExist) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        // Store 1 descriptor and write it
        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        auto id1 = dset->add(std::move(dscptr));

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        dset->full_sync(false);

        // Add another descriptor but do not write it.
        hdr.id += 1;
        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        auto id2 = dset->add(std::move(dscptr2));

        EXPECT_EQ(dset->count(), (uint32_t)2);
        EXPECT_EQ(dset->does_require_write(), (bool)true);

        // Now delete both descriptors and do not write it
        dset->erase(id1);
        dset->erase(id2);

        auto id3 = hdr.id + 1; // this descriptor never existed

        // Try to erase an id that does not exist
        EXPECT_THAT(
            ensure_called_once([&]() {
                dset->erase(id1);
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
                dset->erase(id2);
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
                dset->erase(id3);
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
                dset->mark_as_modified(id1);
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
                dset->mark_as_modified(id2);
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
                dset->mark_as_modified(id3);
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
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);

        EXPECT_THAT(
            ensure_called_once([&]() {
                dset->move_out(id1, dset2);
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
                dset->move_out(id2, dset2);
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
                dset->move_out(id3, dset2);
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
        RuntimeContext rctx(DescriptorMapping({{0xfa, PlainDescriptor::create}}));

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);


        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .isize = 0,
            .cparts = {}
        };

        // Add a bunch of descriptors
        std::vector<uint32_t> ids;
        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr->set_idata({c, c});

            auto id = dset->add(std::move(dscptr), true);
            ids.push_back(id);
            dset->full_sync(false);
        }

        // Reduce the set
        for (size_t i = 10; i < ids.size(); ++i) {
            dset->erase(ids[i]);
            dset->full_sync(false);
        }

        // Reduce the set even more
        for (size_t i = 4; i < 10; ++i) {
            dset->erase(ids[i]);
            dset->full_sync(false);
        }

        // Adding the erased descriptors back again
        for (size_t i = 4; i < 10; ++i) {
            char c = char('A' + i);
            auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr->set_idata({c, c});

            auto id = dset->add(std::move(dscptr), true);
            ids[i] = id;
            dset->full_sync(false);
        }

        // Now expand the set even further
        for (size_t i = 10; i < ids.size(); ++i) {
            char c = char('A' + i);
            auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
            dscptr->set_idata({c, c});

            auto id = dset->add(std::move(dscptr), true);
            ids[i] = id;
            dset->full_sync(false);
        }

        dset->full_sync(true);
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
        rctx.idmgr.reset();
        auto dset2 = DescriptorSet::create(dset->segment(), d_blkarr, rctx);

        // Check that the set was loaded correctly
        for (int i = 0; i < (int)ids.size(); ++i) {
            char c = char('A' + i);
            auto dscptr = dset2->get<PlainDescriptor>(ids[i]);
            auto data = dscptr->get_idata();
            EXPECT_EQ(data.size(), (size_t)2);
            EXPECT_EQ(data[0], (char)c);
            EXPECT_EQ(data[1], (char)c);
        }
    }

    TEST(DescriptorSetTest, DscEmptySet) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.idmgr.request_temporal_id());

        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        XOZ_EXPECT_SET(dset, 0, true);

        // Write the dset to disk. This will trigger the write of the set *but*
        // because the set is empty, nothing is written and the set is still pending
        // for writing.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 0, true);

        // Check sizes
        // 2 bytes for the descriptor's own metadata/header, 2 bytes for dset's reserved field
        // and 2 more bytes for the set's reserved field, hence 6 bytes in total.
        XOZ_EXPECT_SIZES(*dset,
                6, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0108 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);

        // Load the set again, and check it
        // Note: does_require_write() is true because the set loaded was empty
        // so technically its header still needs to be written
        auto dsc2 = Descriptor::load_struct_from(IOSpan(fp), rctx, d_blkarr);
        auto dset2 = dsc2->cast<DescriptorSet>();
        dset2->load_set();
        XOZ_EXPECT_SET(dset2, 0, true);

        // Write it back, we expect the same serialization
        dset2->full_sync(false);
        dset2->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset2, 0, true);

        XOZ_EXPECT_SIZES(*dset2,
                6, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset2,
                "0108 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset2);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset2, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, DscAddDescWithoutWrite) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.idmgr.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x800000a1,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(DSpy(*dscptr).calc_struct_footprint_size(), (uint32_t)2);
        dset->add(std::move(dscptr));

        // 1 descriptor and pending to write
        XOZ_EXPECT_SET(dset, 1, true);

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 1, false);

        // Check sizes
        XOZ_EXPECT_SIZES(*dset,
                14,  /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                2,   /* descriptor data size: 2 bytes for dset's reserved uint16_t */
                6,  /* segment data size: 2 bytes (dscptr) + 4 bytes (dset header) */
                6   /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                // dset (descriptor) header (from Descriptor)
                "0184 0000 "

                // csize
                "0600 "

                // segment's extents
                "0084 00fc "

                // segment's inline
                "00c0 "

                // dset's reserved uint16_t
                "0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);

        // Load the set again, and check it: expected 1 descriptor and no need to write the set
        auto dsc2 = Descriptor::load_struct_from(IOSpan(fp), rctx, d_blkarr);
        auto dset2 = dsc2->cast<DescriptorSet>();
        dset2->load_set();
        XOZ_EXPECT_SET(dset2, 1, false);

        // Write it back, we expect the same serialization
        dset2->full_sync(false);
        dset2->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset2, 1, false);

        XOZ_EXPECT_SIZES(*dset2,
                14,  /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                2,   /* descriptor data size: 2 bytes for dset's reserved uint16_t */
                6,  /* segment data size: r28 bytes (dscptr) + 4 bytes (dset header) */
                6   /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset2,
                // dset (descriptor) header (from Descriptor)
                "0184 0000 "
                // csize
                "0600 "

                // segment's extents
                "0084 00fc "

                // segment's inline
                "00c0 "

                // dset's reserved uint16_t
                "0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset2);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset2, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, DscAddWriteClearWrite) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.idmgr.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x800000a1,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(DSpy(*dscptr).calc_struct_footprint_size(), (uint32_t)2);
        auto id1 = dset->add(std::move(dscptr));

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 1, false);

        // Check sizes
        XOZ_EXPECT_SIZES(*dset,
                14,  /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                2,   /* descriptor data size: 2 bytes for dset's reserved uint16_t */
                6,  /* segment data size: 2 bytes (dscptr) + 4 bytes (dset header) */
                6   /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                // dset (descriptor) header (from Descriptor)
                "0184 0000 "

                // Single content part
                "0600 "  // csize: 6 bytes = (2*2) bytes of set hdr + 2 bytes of plain dsc
                // segment's extents
                "0084 00fc "
                // segment's inline
                "00c0 "

                // dset's reserved uint16_t
                "0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);

        dset->erase(id1);

        // 0 descriptor and pending to write
        XOZ_EXPECT_SET(dset, 0, true);

        // Write the dset to disk. This will trigger the write of the set leaving it empty
        XOZ_RESET_FP(fp, FP_SZ);
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 0, true);

        // Check sizes
        XOZ_EXPECT_SIZES(*dset,
                6,   /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                4,   /* descriptor data size: 2 bytes for dset's reserved uint16_t and 2 pf dset's reserved */
                0 ,  /* segment data size: 0 bytes */
                0    /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                // dset (descriptor) header (from Descriptor)
                "0108 "

                // dset's reserved uint16_t
                "0000 "

                // dset's reserved uint16_t
                "0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }

#if 0
    // The following test is disabled because we don't have a public constructor
    // to set u16data/reserved data on dset creation.
    // Currently we don't have a semantic for that data.
    TEST(DescriptorSetTest, DscEmptySetNonDefault) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor. Use a non-zero u16data
        auto dset = DescriptorSet::create(d_blkarr, rctx, 0x41);
        dset->id(rctx.idmgr.request_temporal_id());

        // 0 descriptors by default, however the set requires a write because
        // its header is pending of being written.
        XOZ_EXPECT_SET(dset, 0, true);

        // Write the dset to disk. This will trigger the write of the set *but*
        // because the set is empty, nothing is written and the set is still pending
        // for writing.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 0, true);

        // Check sizes
        // 2 bytes for the descriptor's own metadata/header, 2 bytes for dset's reserved field
        // and 2 more bytes for the set's reserved field, hence 6 bytes in total.
        XOZ_EXPECT_SIZES(*dset,
                6, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0108 0000 4100"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);

        // Load the set again, and check it
        // Note: does_require_write() is true because the set loaded was empty
        // so technically its header still needs to be written
        auto dsc2 = Descriptor::load_struct_from(IOSpan(fp), rctx, d_blkarr);
        auto dset2 = dsc2->cast<DescriptorSet>();
        dset2->load_set();
        XOZ_EXPECT_SET(dset2, 0, true);

        // Write it back, we expect the same serialization
        dset2->full_sync(false);
        dset2->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset2, 0, true);

        XOZ_EXPECT_SIZES(*dset2,
                6, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset2,
                "0108 0000 4100"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset2);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset2, rctx, d_blkarr);
    }
#endif

    TEST(DescriptorSetTest, DscDestroyHolderImpliesRemoveSet) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.idmgr.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x800000a1,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(DSpy(*dscptr).calc_struct_footprint_size(), (uint32_t)2);
        dset->add(std::move(dscptr));

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);

        XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(d_blkarr, 0, -1,
                "0000 fa00 fa00 0000 0000 0000 0000 0000"
                );

        // Calling destroy should remove the set (and if we force a release
        // at the allocator and the block array level we should get the unused space
        // free)
        dset->destroy();
        d_blkarr.allocator().release();
        d_blkarr.release_blocks();

        XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(d_blkarr, 0, -1,
                ""
                );

        // should fail
        EXPECT_THAT(
            ensure_called_once([&]() { dset->destroy(); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "DescriptorSet not loaded. Missed call to load_set()?"
                        )
                    )
                )
        );
    }

    TEST(DescriptorSetTest, SingleSubset) {
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
        uint32_t id2 = dset->add(std::move(dscptr2));

        // Add the subset to the main set
        uint32_t id3 = dset->add(std::move(subdset));

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
        //EXPECT_EQ(dset->find(id1)->get_owner(), std::addressof(*xsubdset));
        //EXPECT_EQ(dset->find(id2)->get_owner(), std::addressof(*dset));
        //EXPECT_EQ(dset->find(id3)->get_owner(), std::addressof(*dset));

        // Write down the set: we expect to see all the descriptors of dset and xsubdset
        // because full_sync is recursive.
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 03a9 fa00 0184 0000 0600 0084 00e0 00c0 0000"
                );
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "0000 fa00 fa00"
                );


        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);

        // Move child from dset to dset2
        dset->move_out(id3, dset2);

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->get(id2)->get_owner(), std::addressof(*dset));

        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->get(id3)->get_owner(), std::addressof(*dset2));

        // Move the xsubdset's id1 desc to dset2 (parent)
        xsubdset->move_out(id1, dset2);
        EXPECT_EQ(dset2->count(), (uint32_t)2);
        EXPECT_EQ(dset2->get(id1)->get_owner(), std::addressof(*dset2));
        EXPECT_EQ(dset2->get(id3)->get_owner(), std::addressof(*dset2));

        // On dset->full_sync, check dset changed but no dset2 nor xsubdset
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                // the set has only 1 desc (fa00), the rest is just padding
                // that could be reclaimed
                "0000 fa00 fa00 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "" // yields empty but the dset2 is not empty!
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "0000 fa00 fa00" // yields non-empty but xsubdset is empty!
                );

        // If we sync xsubdset, its parent is not synch'd
        xsubdset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "" // still incorrect (out of sync)
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "0000 0000 0000" // correct, in-sync, xsubdset is empty (those zeros are just padding)
                );

        // Sync dset2 and its children releasing unused space
        dset2->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 fb08 fa00 0108 0000 0000" // correct, in sync
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "" // correct, no padding
                );

        // Again, no change is expected
        dset2->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 fb08 fa00 0108 0000 0000" // correct, in sync
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "" // correct, no padding
                );

        // Move the desc back, sync and then clear dset2 which should clear xsubdset as well
        dset2->move_out(id1, *xsubdset);
        dset2->full_sync(false);
        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(xsubdset->count(), (uint32_t)1);

        dset2->clear_set();
        dset2->full_sync(true);
        EXPECT_EQ(dset2->count(), (uint32_t)0);

        // We cannot check xsubdset->count() because the xsubdset was destroyed during
        // dset2->clear_set()
        //EXPECT_EQ(xsubdset->count(), (uint32_t)0);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                ""
                );

        // Create another subset, make it child of dset and move dset's only desc to the subset
        // Sync and check
        Segment subsg2(blk_sz_order);
        uint32_t id4 = dset->add((DescriptorSet::create(subsg2, d_blkarr, rctx)));

        auto xsubdset2 = dset->get<DescriptorSet>(id4);
        dset->move_out(id2, *xsubdset2);

        dset->full_sync(false);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 09a8 0184 0000 0600 0084 00e0 00c0 0000 0000"
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset2,
                "0000 fa00 fa00"
                );

        // Now, destroy dset. We expect a recursive destroy.
        dset->destroy();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset2,
                ""
                );
    }

    class DeferWriteDescriptor : public Descriptor {
    private:
        std::vector<char> idata;
        std::vector<char> defer_idata;
    public:
        DeferWriteDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr) : Descriptor(hdr, cblkarr, 0) {
            idata.resize(hdr.isize);
        }

        void read_struct_specifics_from(IOBase& io) override { io.readall(idata); }
        void write_struct_specifics_into(IOBase& io) override { io.writeall(idata); }

        void update_isize(uint64_t& isize) override {
            isize = assert_u8(idata.size());
        }

        static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, [[maybe_unused]] RuntimeContext& rctx) {
            return std::make_unique<DeferWriteDescriptor>(hdr, cblkarr);
        }

        void set_idata(const std::vector<char>& data) {
            [[maybe_unused]]
            uint8_t isize = assert_u8(data.size());
            assert(does_present_isize_fit(isize));

            defer_idata = data;
            notify_descriptor_changed();
        }

        const std::vector<char>& get_idata() const {
            return idata;
        }

        void flush_writes() override {
            idata = defer_idata;
        }
    };


    TEST(DescriptorSetTest, SingleSubsetWithDeferWrites) {
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

        auto dscptr1 = std::make_unique<DeferWriteDescriptor>(hdr, d_blkarr);
        dscptr1->set_idata({'A', 'B'});
        uint32_t id1 = subdset->add(std::move(dscptr1));

        auto dscptr2 = std::make_unique<DeferWriteDescriptor>(hdr, d_blkarr);
        uint32_t id2 = dset->add(std::move(dscptr2));

        // Add the subset to the main set
        //
        // dset -> [id2]
        //     \-> subset[id3] -> [id1]
        uint32_t id3 = dset->add(std::move(subdset));

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

        // Write down the set: we expect to see all the descriptors of dset and xsubdset
        // because full_sync is recursive *including* a flush of any pending write
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 05b9 fa00 0184 0000 0800 0084 00f0 00c0 0000"
                );
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "0000 3b47 fa04 4142"
                );


        Segment sg2(blk_sz_order);
        auto dset2 = DescriptorSet::create(sg2, d_blkarr, rctx);

        // Move child from dset to dset2
        //
        // dset -> [id2]
        // dset2 -> subset[id3] -> [id1]
        dset->move_out(id3, dset2);

        EXPECT_EQ(dset->count(), (uint32_t)1);
        EXPECT_EQ(dset->get(id2)->get_owner(), std::addressof(*dset));

        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->get(id3)->get_owner(), std::addressof(*dset2));

        // Move the xsubdset's id1 desc to dset2 (parent)
        //
        // dset -> [id2]
        // dset2 -> subset[id3]
        //      \-> [id1]
        xsubdset->move_out(id1, dset2);
        EXPECT_EQ(dset2->count(), (uint32_t)2);
        EXPECT_EQ(dset2->get(id1)->get_owner(), std::addressof(*dset2));
        EXPECT_EQ(dset2->get(id3)->get_owner(), std::addressof(*dset2));

        // On dset->full_sync, check dset changed but no dset2 nor xsubdset
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                // the set has only 1 desc (fa00), the rest is just padding
                // that could be reclaimed
                "0000 fa00 fa00 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "" // yields empty but the dset2 is not empty!
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "0000 3b47 fa04 4142" // yields non-empty but xsubdset is empty!
                );

        // If we sync xsubdset, its parent is not synch'd
        xsubdset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "" // still incorrect (out of sync)
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "0000 0000 0000 0000" // correct, in-sync, xsubdset is empty (those zeros are just padding)
                );

        // Sync dset2 and its children releasing unused space
        dset2->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 3c4f fa04 4142 0108 0000 0000" // correct, in sync
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "" // correct, no padding
                );

        // Again, no change is expected
        dset2->full_sync(true);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                "0000 3c4f fa04 4142 0108 0000 0000" // correct, in sync
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                "" // correct, no padding
                );

        // Move the desc back, sync and then clear dset2 which should clear xsubdset as well
        //
        // dset -> [id2]
        // dset2 -> subset[id3] -> [id1]
        dset2->move_out(id1, *xsubdset);
        dset2->full_sync(false);
        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(xsubdset->count(), (uint32_t)1);

        dset2->clear_set();
        dset2->full_sync(true);
        EXPECT_EQ(dset2->count(), (uint32_t)0);

        // We cannot check xsubdset->count() because the xsubdset was destroyed during
        // dset2->clear_set()
        //EXPECT_EQ(xsubdset->count(), (uint32_t)0);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset2,
                ""
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset,
                ""
                );

        // Create another subset, make it child of dset and move dset's only desc to the subset
        // Sync and check
        Segment subsg2(blk_sz_order);
        uint32_t id4 = dset->add((DescriptorSet::create(subsg2, d_blkarr, rctx)));

        // dset -> sub2[id4] -> [id2]
        // dset2 -> ,empty,
        auto xsubdset2 = dset->get<DescriptorSet>(id4);
        dset->move_out(id2, *xsubdset2);

        xsubdset2->get<DeferWriteDescriptor>(id2)->set_idata({'C', 'D', 'E', 'F'});
        dset->full_sync(false);

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 0fb8 0184 0000 0a00 0084 02f0 00c0 0000 0000"
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset2,
                "0000 8293 fa08 4344 4546"
                );

        // Now, destroy dset. We expect a recursive destroy.
        dset->destroy();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, xsubdset2,
                ""
                );
    }

    class AppDescriptorSet : public DescriptorSet {
        public:
        constexpr static uint16_t TYPE = 0x1ff;

        static std::unique_ptr<AppDescriptorSet> create(const uint16_t cookie, BlockArray& cblkarr, RuntimeContext& rctx) {
            auto dset = std::make_unique<AppDescriptorSet>(TYPE, cblkarr, rctx);
            dset->cookie = cookie;
            dset->load_set();
            return dset;
        }

        static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                                  RuntimeContext& rctx) {
            assert(hdr.type == AppDescriptorSet::TYPE);
            auto dsc = std::make_unique<AppDescriptorSet>(hdr, cblkarr, rctx);
            return dsc;
        }

        AppDescriptorSet(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, RuntimeContext& rctx) : DescriptorSet(hdr, cblkarr, 1, rctx), cookie(0) {}
        AppDescriptorSet(const uint16_t TYPE, BlockArray& cblkarr, RuntimeContext& rctx) : DescriptorSet(TYPE, cblkarr, 1, rctx), cookie(0) {}

        uint16_t get_cookie() const { return cookie; }

        protected:
        void read_struct_specifics_from(IOBase& io) override {
            DescriptorSet::read_struct_specifics_from(io);
            cookie = io.read_u16_from_le();
        }

        void write_struct_specifics_into(IOBase& io) override {
            DescriptorSet::write_struct_specifics_into(io);
            io.write_u16_to_le(cookie);
        }

        void update_isize(uint64_t& isize) override {
            DescriptorSet::update_isize(isize);
            isize += 2; // count for app's own cookie
        }

        private:
        uint16_t cookie;
    };

    TEST(DescriptorSetTest, SubclassDescriptorSet) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}, {AppDescriptorSet::TYPE, AppDescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor subclass of DescriptorSet
        const uint16_t cookie = 0x4142;
        auto dset = AppDescriptorSet::create(cookie, d_blkarr, rctx);
        dset->id(rctx.idmgr.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(DSpy(*dscptr).calc_struct_footprint_size(), (uint32_t)6);

        auto id1 = dset->add(std::move(dscptr), true);

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);

        XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(d_blkarr, 0, -1,
                "0000 fb02 fa02 0100 0000 0000 0000 0000"
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                // First 4 bytes of the Descriptor header
                "ff89 0000 "

                // Serialization of the single content part
                "0a00 "     // the csize field: 10 bytes: 2*2 bytes of set header + 6 of the only descriptor there
                "0084 c0ff 00c0 " // the segment, inline-ended

                // Part of the Descriptor header, this field is the AppDescriptorSet's TYPE
                "ff01 "

                // DescriptorSet's specific idata
                "0000 "

                // AppDescriptorSet's specific odata
                "4241" // cookie
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Reset the runtime as we were loading the xoz file from scratch
        rctx.idmgr.reset();

        // Load the dset again, check that it is mapped to the correct AppDescriptorSet subclass
        auto dsetptr2 = Descriptor::load_struct_from(IOSpan(fp), rctx, d_blkarr);
        auto dset2 = dsetptr2->cast<AppDescriptorSet>();
        dset2->load_set();

        // Check
        EXPECT_EQ(dset2->count(), (uint32_t)1);
        EXPECT_EQ(dset2->get(id1)->get_owner(), std::addressof(*dset2));
        EXPECT_EQ(dset2->is_descriptor_set(), (bool)true);
        EXPECT_EQ((*dset2).type(), (uint16_t)AppDescriptorSet::TYPE);
        EXPECT_EQ(dset2->get_cookie(), (uint16_t)0x4142);

        // Pretend now to be an "older" version of the app where AppDescriptorSet didn't exist
        // We should still loading it as a set (otherwise we would loose access to its descriptors)
        RuntimeContext rctx2({{0x01, DescriptorSet::create}}, true);

        // Load the dset again, check that it is mapped to DescriptorSet but not to AppDescriptorSet subclass
        auto dsetptr3 = Descriptor::load_struct_from(IOSpan(fp), rctx2, d_blkarr);
        auto dset3 = dsetptr3->cast<DescriptorSet>();
        dset3->load_set();
        EXPECT_EQ(dsetptr3->cast<AppDescriptorSet>(true), (AppDescriptorSet*)nullptr);

        // Check
        EXPECT_EQ(dset3->count(), (uint32_t)1);
        EXPECT_EQ(dset3->get(id1)->get_owner(), std::addressof(*dset3));
        EXPECT_EQ(dset3->is_descriptor_set(), (bool)true);
        EXPECT_EQ((*dset3).type(), (uint16_t)AppDescriptorSet::TYPE); // AppDescriptorSet TYPE is preserved

        // Make the "older" version of the app write the descriptor set.
        // It is not aware of AppDescriptorSet class but it should preserve the data
        // "from future versions of the app" (aka forward compatibility)
        XOZ_RESET_FP(fp, FP_SZ);

        auto id2 = dset3->add(std::move(std::make_unique<PlainDescriptor>(hdr, d_blkarr)), true);
        dset3->full_sync(true);
        dset3->write_struct_into(IOSpan(fp), rctx2);

        XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(d_blkarr, 0, -1,
                "0000 f705 fa02 0100 0000 fa02 0200 0000"
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset3,
                // First 4 bytes of the Descriptor header
                "ff89 0000 "

                // Serialization of the single content part
                "1000 "     // the csize field: 16 bytes = (2*2) bytes for dset hdr + (2*6) bytes for the 2 plain dscs
                "000c 00c0 " // the segment, inline-ended

                // Part of the Descriptor header, this field is the AppDescriptorSet's TYPE
                "ff01 "

                // DescriptorSet's specific idata
                "0000 "

                // AppDescriptorSet's specific odata
                "4241" // cookie
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset3);

        // Now, lets go to the future and make the "newer" version of the app,
        // aware of AppDescriptorSet class, to load it. We should recover all including out cookie.
        rctx.idmgr.reset();
        auto dsetptr4 = Descriptor::load_struct_from(IOSpan(fp), rctx, d_blkarr);
        auto dset4 = dsetptr4->cast<AppDescriptorSet>();
        dset4->load_set();

        // Check
        EXPECT_EQ(dset4->count(), (uint32_t)2);
        EXPECT_EQ(dset4->get(id1)->get_owner(), std::addressof(*dset4));
        EXPECT_EQ(dset4->get(id2)->get_owner(), std::addressof(*dset4));
        EXPECT_EQ(dset4->is_descriptor_set(), (bool)true);
        EXPECT_EQ((*dset4).type(), (uint16_t)AppDescriptorSet::TYPE);
        EXPECT_EQ(dset4->get_cookie(), (uint16_t)0x4142);
    }

    TEST(DescriptorSetTest, EmptyDescriptorSetContentReservedField) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);
        dset->_set_creserved(42);
        dset->id(0x8000ffff);

        // Write and check the dump
        dset->full_sync(true);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0108 0000 2a00"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, NonEmptyDescriptorSetContentReservedField) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);
        dset->_set_creserved(42);
        dset->id(0x8000ffff);

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr));

        // Write and check the dump
        dset->full_sync(true);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "2a00 2401 fa00"
                );
        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0184 0000 0600 0084 00e0 00c0 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, EmptyDescriptorSetIdataReservedField) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);
        dset->_set_ireserved(42);
        dset->id(0x8000ffff);

        // Write and check the dump
        dset->full_sync(true);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0108 2a00 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }


    TEST(DescriptorSetTest, NonEmptyDescriptorSetIDataReservedField) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);
        dset->_set_ireserved(42);
        dset->id(0x8000ffff);

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr));

        // Write and check the dump
        dset->full_sync(true);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );
        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0184 0000 0600 0084 00e0 00c0 2a00"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, EmptyDescriptorSetPDataField) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);
        dset->_set_pdata({'A', 'B'});
        dset->id(0x8000ffff);

        // Write and check the dump
        dset->full_sync(true);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );
        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "010c 0010 0000 4142"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, NonEmptyDescriptorSetPDataField) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);
        dset->_set_pdata({'A', 'B'});
        dset->id(0x8000ffff);

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        dset->add(std::move(dscptr));

        // Write and check the dump
        dset->full_sync(true);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 fa00 fa00"
                );
        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                "0188 0000 0600 0084 00e0 00c0 0010 4142"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dset);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, *dset, rctx, d_blkarr);
    }

    TEST(DescriptorSetTest, VeryNestedSetTree) {
        RuntimeContext rctx({});

        VectorBlockArray d_blkarr(32);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());
        const auto blk_sz_order = d_blkarr.blk_sz_order();

        Segment sg(blk_sz_order);
        auto dset = DescriptorSet::create(sg, d_blkarr, rctx);

        std::shared_ptr<DescriptorSet> last_dset;
        for (int i = 0; i  < 10240; ++i) {
            Segment subsg(blk_sz_order);
            auto subdset = DescriptorSet::create(subsg, d_blkarr, rctx);

            int id = 0;
            if (last_dset) {
                id = last_dset->add(std::move(subdset));
                last_dset = last_dset->get<DescriptorSet>(id);
            } else {
                id = dset->add(std::move(subdset));
                last_dset = dset->get<DescriptorSet>(id);
            }
        }

        // Add one descriptor to the dset and another to the subdset
        struct Descriptor::header_t hdr = {
            .type = 0xfa,

            .id = 0x0,

            .isize = 0,
            .cparts = {}
        };

        auto dscptr1 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id1 = last_dset->add(std::move(dscptr1));

        auto dscptr2 = std::make_unique<PlainDescriptor>(hdr, d_blkarr);
        uint32_t id2 = dset->add(std::move(dscptr2));

        // Count is not recursive: the set has only 2 direct descriptors
        EXPECT_EQ(dset->count(), (uint32_t)2);
        EXPECT_EQ(dset->does_require_write(), (bool)true);
        EXPECT_EQ(dset->get(id2)->get_owner(), std::addressof(*dset));

        // Check subset (get a reference to it again because the ref subset was left
        // invalid after the std::move in the dset->add above)
        EXPECT_EQ(last_dset->count(), (uint32_t)1);
        EXPECT_EQ(last_dset->get(id1)->get_owner(), std::addressof(*last_dset));

        // Write down the set: we expect to see all the descriptors of dset and last_dset
        // because full_sync is recursive.
        dset->full_sync(false);
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                "0000 cfec 0184 0000 1400 0080 fe27 c0ff 00c0 0000 fa00"
                );
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, last_dset,
                "0000 fa00 fa00"
                );

        // Now, destroy dset. We expect a recursive destroy.
        dset->destroy();
        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, dset,
                ""
                );

        XOZ_EXPECT_SET_SERIALIZATION(d_blkarr, last_dset,
                ""
                );
    }
}
