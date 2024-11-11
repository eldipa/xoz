#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/descriptor_set.h"
#include "test/plain.h"
#include "xoz/err/exceptions.h"
#include "xoz/file/runtime_context.h"
#include "xoz/blk/vector_block_array.h"
#include "xoz/mem/inet_checksum.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"


#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::PlainDescriptor;
using ::testing_xoz::PlainWithContentDescriptor;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

namespace {
const size_t FP_SZ = 224;
}

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, disk_sz, idata_sz, cdata_sz, obj_data_sz) do {      \
    EXPECT_EQ((dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ((dsc).calc_internal_data_space_size(), (unsigned)(idata_sz));                                  \
    EXPECT_EQ((dsc).calc_content_space_size(), (unsigned)(cdata_sz));      \
    EXPECT_EQ((dsc).get_hdr_csize(), (unsigned)(obj_data_sz));       \
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
    dsc2_ptr->write_struct_into(IOSpan(buf2), (rctx));                           \
    checksum3 = dsc2_ptr->checksum;                                      \
    EXPECT_EQ((fp), buf2);                                               \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)

#define XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc, rctx, cblkarr) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    uint32_t sz1 = (dsc).calc_struct_footprint_size();                   \
    auto d1 = hexdump((fp), 0, sz1);                                      \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), (rctx), (cblkarr));   \
    checksum2 = dsc2_ptr->checksum;                                      \
    dsc2_ptr->checksum = 0;                                              \
    auto dset = dsc2_ptr->cast<DescriptorSet>(true);                     \
    if (dset) { dset->load_set(); }                                      \
                                                                         \
    uint32_t sz2 = dsc2_ptr->calc_struct_footprint_size();               \
    EXPECT_EQ(sz1, sz2);                                                 \
                                                                         \
    dsc2_ptr->write_struct_into(IOSpan(buf2), (rctx));                   \
    checksum3 = dsc2_ptr->checksum;                                      \
    auto d2 = hexdump(buf2, 0, sz1);                                      \
                                                                         \
    EXPECT_EQ(d1, d2);                                                   \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)


namespace {
    TEST(DescriptorTest, NoOwnsTempIdZeroData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff00"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2, 3, 4}); // isize = 4
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff08 0102 0304"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMaxNonDSetTypeWithoutExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0x1e0 - 1,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2, 3, 4}); // isize = 4
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "df09 0102 0304"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMaxTypeWithoutExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0x01fe,

            .id = 0x80000001,

            .isize = 4,
            .csize = 0,
            .segm = Segment::EmptySegment(cblkarr.blk_sz_order())
        };

        DescriptorSet dsc(hdr, cblkarr, rctx);
        dsc.load_set();

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                6, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "fe09 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMinNonDSetTypeWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0x1e0 + 2048 + 1,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2, 3, 4}); // isize = 4
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2+4, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 e109 0102 0304"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMinTypeWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0x01ff,

            .id = 0x80000001,

            .isize = 4,
            .csize = 0,
            .segm = Segment::EmptySegment(cblkarr.blk_sz_order())
        };

        DescriptorSet dsc(hdr, cblkarr, rctx);
        dsc.load_set();

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                8, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 ff01 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMaxTypeWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xffff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2, 3, 4}); // isize = 4
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2+4, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 ffff 0102 0304"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMinTypeButWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xffff, // fake a type that requires ex_type

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2, 3, 4}); // isize = 4
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2+4, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_CHECKSUM(fp, dsc); // check here before the patch

        // Now patch the string to make the ex_type smaller than the EXTENDED_TYPE_VAL_THRESHOLD
        fp[3] = 0; fp[2] = 0xa; // the new type should be 10 or 0x0a

        // Check that we did the patch correctly
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 0a00 0102 0304"
                );

        // Load it and serializate it back again. We expect that the serialization
        // is shorter because ex_type is not needed.
        std::vector<char> buf2;
        XOZ_RESET_FP(buf2, FP_SZ);
        rctx.idmgr.reset(0x80000001);

        auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), rctx, cblkarr);

        auto checksum2 = dsc2_ptr->checksum;
        dsc2_ptr->checksum = 0;

        dsc2_ptr->write_struct_into(IOSpan(buf2), rctx);
        XOZ_EXPECT_SERIALIZATION(buf2, *dsc2_ptr,
                "0a08 0102 0304"
                );
        XOZ_EXPECT_CHECKSUM(buf2, *dsc2_ptr);

        // We do *not* expect to see the same checksum: on load, the checksum
        // matches what it is in the file (fp), on write, the checksum
        // matches what it is going to be written.
        //
        // Because we intentinally written to fp a descriptor encoded inefficently,
        // the load got its checksum but on the second write, the write *did*
        // a efficient encoding so its checksum  will be different from the former.
        EXPECT_NE(checksum2, dsc2_ptr->checksum);
    }

    TEST(DescriptorTest, NoOwnsTempIdMaxLoData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata(data); // isize = 64-2
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+64-2, /* struct size */
                64-2,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7c 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdOneMoreLoData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata(data); // isize = 64
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+64, /* struct size */
                64,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff02 0000 0080 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 "
                "1415 1617 1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d "
                "2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdMaxHiData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        std::vector<char> data(128-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata(data); // isize = 128-2
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+128-2, /* struct size */
                128-2,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7e 0000 0080 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 "
                "3637 3839 3a3b 3c3d 3e3f 4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 "
                "5455 5657 5859 5a5b 5c5d 5e5f 6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 "
                "7273 7475 7677 7879 7a7b 7c7d"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NoOwnsPersistentIdMaxLoData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata(data); // isize = 64-2
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+64-2, /* struct size */
                64-2,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7e 0100 0000 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }


    TEST(DescriptorTest, NoOwnsPersistentMaximumIdMaxLoData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x7fffffff,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata(data); // isize = 64-2
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+64-2, /* struct size */
                64-2,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7e ffff ff7f 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegm) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0000 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxNonDSetTypeWithoutExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0x01e0 - 1,

            .id = 1,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "df83 0100 0000 0000 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxTypeWithoutExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0x01fe,

            .id = 0x80000001,

            .isize = 4,
            .csize = 0,
            .segm = Segment::EmptySegment(cblkarr.blk_sz_order())
        };

        DescriptorSet dsc(hdr, cblkarr, rctx);
        dsc.load_set();

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                6, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "fe09 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMinNonDSetTypeWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0x01e0 + 2048 + 1,

            .id = 1,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff83 0100 0000 0000 00c0 e109"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMinTypeWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0x01ff,

            .id = 0x80000001,

            .isize = 4,
            .csize = 0,
            .segm = Segment::EmptySegment(cblkarr.blk_sz_order())
        };

        DescriptorSet dsc(hdr, cblkarr, rctx);
        dsc.load_set();

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                8, /* struct size */
                4,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 ff01 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxTypeWithExtendedType) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xffff,

            .id = 1,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff83 0100 0000 0000 00c0 ffff"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdOneMoreLoDataEmptySegm) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata(data); // isize = 64
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+64, /* struct size */
                64,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0080 0000 00c0 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmSomeObjData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = 1,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0100 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxNonLargeObjData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = (1 << 15) - 1,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                (1 << 15) - 1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 ff7f 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmOneMoreNonLargeObjData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = (1 << 15),
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                (1 << 15)  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0080 0100 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxLargeObjData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = uint32_t(1 << 31) - 1,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                uint32_t(1 << 31) - 1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 ffff ffff 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataSegmInlineSomeObjData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 1,

            .isize = 0,
            .csize = 1,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        hdr.segm.set_inline_data({0x1, 0x2, 0x3});

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+4, /* struct size */
                0,   /* internal data size */
                3,  /* segment data size */
                1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0100 03c3 0102"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);
    }

    TEST(DescriptorTest, NotEnoughRoomForRWNonOwnerTemporalId) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2}); // isize = 2
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2, /* struct size */
                2,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        IOSpan io(fp);
        io.seek_wr(2+2 - 1, IOSpan::Seekdir::end); // point 1 byte off (available = 3 bytes)

        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(io, rctx); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for writing descriptor's internal data of "
                        "descriptor {id: 0x80000001, type: 255, isize: 2}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);
        rctx.idmgr.reset(0x80000001); // ensure that the descriptor loaded will have the same id than 'dsc'

        // Write a valid descriptor of data size 2
        dsc.write_struct_into(IOSpan(fp), rctx);

        // Now, truncate the file so the span will be shorter than the expected size
        fp.resize(2+2 - 1); // shorter by 1 byte

        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), rctx, cblkarr); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for reading descriptor's internal data of "
                        "descriptor {id: 0x80000001, type: 255, isize: 2}"
                        )
                    )
                )
        );
    }

    TEST(DescriptorTest, NotEnoughRoomForRWOwnsWithPersistentId) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 15,

            .isize = 0,
            .csize = 42,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.set_idata({1, 2}); // isize = 2
        dsc.full_sync(false);


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                2,   /* internal data size */
                0,  /* segment data size */
                42  /* obj data size */
                );

        IOSpan io(fp);
        io.seek_wr(2+4+2+2+2 - 1, IOSpan::Seekdir::end); // point 1 byte off (available = 11 bytes)

        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(io, rctx); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for writing descriptor's internal data of "
                        "descriptor {id: 0x0000000f, type: 255, isize: 2, csize: 42, owns: 0}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);

        // Write a valid descriptor of data size 2
        dsc.write_struct_into(IOSpan(fp), rctx);

        // Now, truncate the file so the span will be shorter than the expected size
        fp.resize(2+4+2+2+2 - 1); // shorter by 1 byte

        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), rctx, cblkarr); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for reading descriptor's internal data of "
                        "descriptor {id: 0x0000000f, type: 255, isize: 2, csize: 42, owns: 0}"
                        )
                    )
                )
        );
    }

    class DescriptorSubRW : public Descriptor {
    private:
        std::vector<char> internal_data;
    public:
        DescriptorSubRW(const struct Descriptor::header_t& hdr, BlockArray& cblkarr) : Descriptor(hdr, cblkarr) {}
        void read_struct_specifics_from(IOBase&) override {
            return; // 0 read
        }
        void write_struct_specifics_into(IOBase&) override {
            return; // 0 write
        }
        void update_sizes(uint64_t& isize, [[maybe_unused]] uint64_t& csize) override {
            isize = assert_u8(internal_data.size());
        }

        static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, [[maybe_unused]] RuntimeContext& rctx) {
            return std::make_unique<DescriptorSubRW>(hdr, cblkarr);
        }

        void set_idata(const std::vector<char>& data) {
            [[maybe_unused]]
            uint8_t isize = assert_u8(data.size());
            assert(does_present_isize_fit(isize));

            internal_data = data;
            notify_descriptor_changed();
            update_header(); // no descriptor set that will call it so we need to call it ourselves
        }

        const std::vector<char>& get_idata() const {
            return internal_data;
        }
    };

    TEST(DescriptorTest, DescriptorReadOrWriteLess) {
        std::map<uint16_t, descriptor_create_fn> descriptors_map {
            {0xff, DescriptorSubRW::create }
        };
        RuntimeContext rctx(descriptors_map);

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 15,

            .isize = 0,
            .csize = 42,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        DescriptorSubRW dsc = DescriptorSubRW(hdr, cblkarr);
        dsc.set_idata({1, 2}); // isize = 2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                2,   /* internal data size */
                0,  /* segment data size */
                42  /* obj data size */
                );

        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(IOSpan(fp), rctx); }),
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "The descriptor subclass underflowed the write pointer and "
                        "processed 0 bytes (left 2 bytes unprocessed of 2 bytes available) and "
                        "left it at position 10 that it is before the end of the data section at position 12."
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);

        // Write a valid descriptor of data size 2
        PlainDescriptor dsc2 = PlainDescriptor(hdr, cblkarr);
        dsc2.set_idata({1, 2});
        dsc2.full_sync(false);
        dsc2.write_struct_into(IOSpan(fp), rctx);

        // Load a descriptor. Despite DescriptorSubRW does not read anything (see the class)
        // and there are 2 bytes to be read (in the data and by isize), no error happen
        // (not like in the case of write_struct_specifics_into).
        auto dscptr3 = Descriptor::load_struct_from(IOSpan(fp), rctx, cblkarr);
        auto dsc3 = dscptr3->cast<DescriptorSubRW>();

        // Check that the "bogus" descriptor didn't read the data
        EXPECT_EQ(dsc3->get_idata().size(), (uint32_t)0);

        // Both the writing and the loading should preserve opaque data
        XOZ_RESET_FP(fp, FP_SZ);

        // Check the write preserve the opaque data
        dsc3->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, *dsc3,
                "ff86 0f00 0000 2a00 00c0 0102"
                );

        rctx.idmgr.reset();
        auto dscptr4 = Descriptor::load_struct_from(IOSpan(fp), rctx, cblkarr);
        auto dsc4 = dscptr4->cast<DescriptorSubRW>();

        // Check sizes
        XOZ_EXPECT_SIZES(*dsc4,
                2+4+2+2+2, /* struct size */
                2,   /* internal data size */
                0,  /* segment data size */
                42  /* obj data size */
                );

        EXPECT_EQ(dsc4->get_idata().size(), (uint32_t)0);

        // Check the read and write preserve the opaque data
        XOZ_RESET_FP(fp, FP_SZ);
        dsc4->write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, *dsc4,
                "ff86 0f00 0000 2a00 00c0 0102"
                );
    }

    TEST(DescriptorTest, DescriptorWithExplicitZeroId) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = true,
            .type = 0xff,

            .id = 0,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Writing a descriptor with id = 0 is incorrect. No descriptor should
        // have id of 0 unless it has a temporal id *and* it requires the hi_dsize field
        // (not this case so an exception is expected)
        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(IOSpan(fp), rctx); }),
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Descriptor id is zero in descriptor "
                        "{id: 0x00000000, type: 255, isize: 0, csize: 0, owns: 0}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);

        // this will make the write_struct_into to set the has_id to true...
        hdr.id = 0xffff;
        PlainDescriptor dsc2 = PlainDescriptor(hdr, cblkarr);
        dsc2.full_sync(false);
        dsc2.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_CHECKSUM(fp, dsc2); // check before the patch

        // ...and now we nullify the id field so it would look like a descriptor
        // that has_id but it has a id = 0
        fp[2] = fp[3] = 0;
        XOZ_EXPECT_SERIALIZATION(fp, dsc2,
                "ff82 0000 0000 0000 00c0"
                );

        // Because the isize of the descriptor is small, there is no reason to have
        // an id = 0.
        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), rctx, cblkarr); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "xoz file seems inconsistent/corrupt. "
                        "Descriptor id is zero, detected with partially loaded descriptor "
                        "{id: 0x00000000, type: 255, isize: 0, csize: 0, owns: 0}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);
        rctx.idmgr.reset(0x80000001); // ensure that the descriptor loaded will have the same id than 'dsc3'

        // We repeat again has_id = true but we also make the descriptor very large so
        // we force to and id of 0 (because the temporal id is not stored)
        hdr.id = 0x80000001;
        PlainDescriptor dsc3 = PlainDescriptor(hdr, cblkarr);

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers
        dsc3.set_idata(data);
        dsc3.full_sync(false);

        dsc3.write_struct_into(IOSpan(fp), rctx);

        // the id should be 0, see also how the hi_dsize bit is set (0080)
        XOZ_EXPECT_SERIALIZATION(fp, dsc3,
                "ff82 0000 0080 0000 00c0 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc3);


        // Load should be ok even if the id is 0 in the string. A temporal id should be then
        // set to the loaded descriptor.
        XOZ_EXPECT_DESERIALIZATION(fp, dsc3, rctx, cblkarr);
    }

    TEST(DescriptorTest, DownCast) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xffff, // fake a type that requires ex_type

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        // The concrete Descriptor subclass
        PlainDescriptor dsc = PlainDescriptor(hdr, cblkarr);
        dsc.full_sync(false);

        // Upper cast to Descriptor abstract class
        Descriptor* dsc2 = &dsc;

        // Down cast to Descriptor subclass again
        // If the downcast works, cast<X> does neither throws nor return null
        PlainDescriptor* dsc3 = dsc2->cast<PlainDescriptor>();
        EXPECT_NE(dsc3, (PlainDescriptor*)nullptr);

        // Paranoiac check: modifications through downcasted pointer are visible from
        // the original descriptor.
        dsc3->set_idata({'A', 'B'});
        EXPECT_EQ(dsc.get_idata()[0], (char)'A');
        EXPECT_EQ(dsc.get_idata()[1], (char)'B');

        // If the downcast fails, throw an exception (it does not return null either)
        EXPECT_THAT(
            ensure_called_once([&]() {
                [[maybe_unused]]
                DescriptorSubRW* dsc4 = dsc2->cast<DescriptorSubRW>();
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
        DescriptorSubRW* dsc5 = dsc2->cast<DescriptorSubRW>(true);
        EXPECT_EQ(dsc5, (DescriptorSubRW*)nullptr);
    }

    TEST(DescriptorTest, ContentData) {
        RuntimeContext rctx({});

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        PlainWithContentDescriptor dsc = PlainWithContentDescriptor(hdr, cblkarr);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump: no content for now
        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff00"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);

        // Add for the first time some content. This should kick a allocation.
        // Call full_sync() to get accurate sizes.
        dsc.set_content({'A'});
        dsc.full_sync(false);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                10, // struct size: 6 of header + 4 of idata
                4,  // internal data size: 4 for the content_size field of PlainWithContentDescriptor
                1,  // segment data size: 'A'
                1   // obj data size: 'A'
                );

        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff88 0100 41c1 0100 0000" // Note the 0x41 there: the content is stored within the segment
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);

        // Set a larger content: This should kick a reallocation
        dsc.set_content({'A', 'B'});
        dsc.full_sync(false);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                12, // struct size: 8 of header + 4 of idata
                4,  // internal data size: 4 for the content_size field of PlainWithContentDescriptor
                2,  // segment data size: 'AB'
                2   // obj data size: 'AB'
                );

        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff88 0200 00c2 4142 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);

        // Set an even larger content: This should kick a reallocation *and* the content
        // will not longer being stored in the segment (inline section)
        dsc.set_content({'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J'});
        dsc.full_sync(false);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                14, // struct size: 10 of header + 4 of idata
                4,  // internal data size: 4 for the content_size field of PlainWithContentDescriptor
                64,  // segment data size: 1/16 of a block size (1 single subblock)
                10   // obj data size: 'ABCDEFGHIJ'
                );

        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff88 0a00 0084 0080 00c0 0a00 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);

        // check content
        EXPECT_EQ(hexdump(dsc.get_content()),
                "4142 4344 4546 4748 494a"
                );

        // Set to a smaller content: This should kick a reallocation (shrink)
        dsc.set_content({'G', 'H', 'I', 'J'});
        dsc.full_sync(false);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                14, // struct size: 10 of header + 4 of idata
                4,  // internal data size: 4 for the content_size field of PlainWithContentDescriptor
                4,  // segment data size: 1/16 of a block size (1 single subblock)
                4   // obj data size: 'ABCDEFGHIJ'
                );

        dsc.write_struct_into(IOSpan(fp), rctx);
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff88 0400 00c4 4748 494a 0400 0000" // use inline again
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, rctx, cblkarr);

        // Delete the content: This should kick a deallocation.
        dsc.del_content();
        dsc.full_sync(false);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump: no content for now
        dsc.write_struct_into(IOSpan(fp), rctx);
        EXPECT_EQ(hexdump((fp), 0, (dsc).calc_struct_footprint_size()),
                "ff00"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc, rctx, cblkarr);

        // Delete the content again: This should be a no-op
        dsc.del_content();
        dsc.full_sync(false);

        // Check sizes: no content for now
        XOZ_EXPECT_SIZES(dsc,
                2, /* struct size */
                0,   /* internal data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump: no content for now
        dsc.write_struct_into(IOSpan(fp), rctx);
        EXPECT_EQ(hexdump((fp), 0, (dsc).calc_struct_footprint_size()),
                "ff00"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc, rctx, cblkarr);
    }
}
