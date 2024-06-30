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
#include "xoz/mem/inet_checksum.h"


#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing::ElementsAre;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

namespace {
const size_t FP_SZ = 224;
}

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check that the serialization of the obj in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION(fp, dsc, data) do {                                 \
    EXPECT_EQ(hexdump((fp), 0, (dsc).calc_struct_footprint_size()), (data));         \
    EXPECT_EQ(are_all_zeros((fp), (dsc).calc_struct_footprint_size()), (bool)true);  \
} while (0)

// Calc checksum over the fp (bytes) and expect to be the same as the descriptor's checksum
// Note: this requires a load_struct_from/write_struct_into call before to make
// the descriptor's checksum updated
#define XOZ_EXPECT_CHECKSUM(fp, dsc) do {    \
    EXPECT_EQ(inet_checksum((uint8_t*)(fp).data(), (dsc).calc_struct_footprint_size()), (dsc).checksum); \
} while (0)

// Load from fp the obj and serialize it back again into
// a temporal fp2 stream. Then compare both (they should be the same)
#define XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, ed_blkarr) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), (rctx), (ed_blkarr));   \
    checksum2 = dsc2_ptr->checksum;                                      \
    dsc2_ptr->checksum = 0;                                              \
    dsc2_ptr->write_struct_into(IOSpan(buf2), (rctx));                   \
    checksum3 = dsc2_ptr->checksum;                                      \
    EXPECT_EQ((fp), buf2);                                               \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, disk_sz, data_sz, segm_data_sz, obj_data_sz) do {      \
    EXPECT_EQ((dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ((dsc).calc_data_space_size(), (unsigned)(data_sz));                                  \
    EXPECT_EQ((dsc).calc_external_data_space_size(), (unsigned)(segm_data_sz));      \
    EXPECT_EQ((dsc).calc_external_data_size(), (unsigned)(obj_data_sz));       \
} while (0)

#define XOZ_EXPECT_DSC_SERIALIZATION(blkarr, sg, data) do {         \
    EXPECT_EQ(hexdump(IOSegment((blkarr), (sg))), (data));          \
} while (0)


#define XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

#define XOZ_EXPECT_SET(dset, cnt, pending) do {                      \
    EXPECT_EQ((dset)->count(), (uint32_t)(cnt));                         \
    /* TODO EXPECT_EQ((dset)->does_require_write(), (bool)(pending));*/   \
} while (0)

namespace {
    TEST(DescriptorSetDescriptorTest, EmptySet) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.request_temporal_id());

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

    TEST(DescriptorSetDescriptorTest, AddDescWithoutWrite) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x800000a1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(dscptr->calc_struct_footprint_size(), (uint32_t)6);
        dset->add(std::move(dscptr));

        // 1 descriptor and pending to write
        XOZ_EXPECT_SET(dset, 1, true);

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 1, false);

        // Check sizes
        XOZ_EXPECT_SIZES(*dset,
                18,  /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                2,   /* descriptor data size: 2 bytes for dset's reserved uint16_t */
                10,  /* segment data size: 6 bytes (dscptr) + 4 bytes (dset header) */
                10   /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                // dset (descriptor) header (from Descriptor)
                "0184 0a00 "

                // segment's extents
                "0084 00f0 0080 0000 c00f "

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
        XOZ_EXPECT_SET(dset2, 1, false);

        // Write it back, we expect the same serialization
        dset2->full_sync(false);
        dset2->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset2, 1, false);

        XOZ_EXPECT_SIZES(*dset2,
                18,  /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                2,   /* descriptor data size: 2 bytes for dset's reserved uint16_t */
                10,  /* segment data size: 6 bytes (dscptr) + 4 bytes (dset header) */
                10   /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset2,
                // dset (descriptor) header (from Descriptor)
                "0184 0a00 "

                // segment's extents
                "0084 00f0 0080 0000 c00f "

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


    TEST(DescriptorSetDescriptorTest, AddWriteClearWrite) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x800000a1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(dscptr->calc_struct_footprint_size(), (uint32_t)6);
        auto id1 = dset->add(std::move(dscptr));

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SET(dset, 1, false);

        // Check sizes
        XOZ_EXPECT_SIZES(*dset,
                18,  /* struct size: (see XOZ_EXPECT_SERIALIZATION) */
                2,   /* descriptor data size: 2 bytes for dset's reserved uint16_t */
                10,  /* segment data size: 6 bytes (dscptr) + 4 bytes (dset header) */
                10   /* obj data size */
                );

        XOZ_EXPECT_SERIALIZATION(fp, *dset,
                // dset (descriptor) header (from Descriptor)
                "0184 0a00 "

                // segment's extents
                "0084 00f0 0080 0000 c00f "

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
    // TODO
    TEST(DescriptorSetDescriptorTest, EmptySetNonDefault) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor. Use a non-zero u16data
        auto dset = DescriptorSet::create(d_blkarr, rctx, 0x41);
        dset->id(rctx.request_temporal_id());

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

    TEST(DescriptorSetDescriptorTest, DestroyHolderImpliesRemoveSet) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        RuntimeContext rctx({{0x01, DescriptorSet::create}}, true);

        VectorBlockArray d_blkarr(16);
        d_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Create the dset descriptor
        auto dset = DescriptorSet::create(d_blkarr, rctx);
        dset->id(rctx.request_temporal_id());

        // Add a descriptor to the set
        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xfa,

            .id = 0x800000a1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(d_blkarr.blk_sz_order())
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, d_blkarr);
        EXPECT_EQ(dscptr->calc_struct_footprint_size(), (uint32_t)6);
        dset->add(std::move(dscptr));

        // Write the dset to disk. This will trigger the write of the set.
        dset->full_sync(false);
        dset->write_struct_into(IOSpan(fp), rctx);

        XOZ_EXPECT_BLOCK_ARRAY_SERIALIZATION(d_blkarr, 0, -1,
                "0000 fb40 fa80 0000 00c0 0000 0000 0000"
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
}
