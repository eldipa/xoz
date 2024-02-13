#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/default.h"
#include "xoz/err/exceptions.h"
#include "xoz/repo/id_manager.h"
#include "xoz/blk/vector_block_array.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"


#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

namespace {
const size_t FP_SZ = 224;
}

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, disk_sz, data_sz, segm_data_sz, obj_data_sz) do {      \
    EXPECT_EQ((dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ((dsc).calc_data_space_size(), (unsigned)(data_sz));                                  \
    EXPECT_EQ((dsc).calc_external_data_space_size(), (unsigned)(segm_data_sz));      \
    EXPECT_EQ((dsc).calc_external_data_size(), (unsigned)(obj_data_sz));       \
} while (0)

// Check that the serialization of the obj in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION(fp, dsc, data) do {                                 \
    EXPECT_EQ(hexdump((fp), 0, (dsc).calc_struct_footprint_size()), (data));         \
    EXPECT_EQ(are_all_zeros((fp), (dsc).calc_struct_footprint_size()), (bool)true);  \
} while (0)

// Load from fp the obj and serialize it back again into
// a temporal fp2 stream. Then compare both (they should be the same)
#define XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), (idmgr), (ed_blkarr));   \
    dsc2_ptr->write_struct_into(IOSpan(buf2));                           \
    EXPECT_EQ((fp), buf2);                                               \
} while (0)


namespace {
    TEST(DescriptorTest, NoOwnsTempIdZeroData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff00"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff08 0102 0304"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMaxTypeWithoutExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0x1fe,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "fe09 0102 0304"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMinTypeWithExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0x1ff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2+4, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 ff01 0102 0304"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMaxTypeWithExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xffff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2+4, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 ffff 0102 0304"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdSomeDataMinTypeButWithExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xffff, // fake a type that requires ex_type

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2+4, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write
        dsc.write_struct_into(IOSpan(fp));

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
        idmgr.reset(0x80000001);

        auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), idmgr, ed_blkarr);
        dsc2_ptr->write_struct_into(IOSpan(buf2));
        XOZ_EXPECT_SERIALIZATION(buf2, *dsc2_ptr,
                "0a08 0102 0304"
                );
    }

    TEST(DescriptorTest, NoOwnsTempIdMaxLoData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data(data); // dsize = 64-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+64-2, /* struct size */
                64-2,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7c 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdOneMoreLoData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data(data); // dsize = 64


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+64, /* struct size */
                64,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff02 0000 0080 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 "
                "1415 1617 1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d "
                "2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsTempIdMaxHiData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(128-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data(data); // dsize = 128-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+128-2, /* struct size */
                128-2,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7e 0000 0080 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 "
                "3637 3839 3a3b 3c3d 3e3f 4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 "
                "5455 5657 5859 5a5b 5c5d 5e5f 6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 "
                "7273 7475 7677 7879 7a7b 7c7d"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NoOwnsPersistentIdMaxLoData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data(data); // dsize = 64-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+64-2, /* struct size */
                64-2,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7e 0100 0000 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }


    TEST(DescriptorTest, NoOwnsPersistentMaximumIdMaxLoData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x7fffffff,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data(data); // dsize = 64-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+64-2, /* struct size */
                64-2,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff7e ffff ff7f 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegm) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0000 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxTypeWithoutExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0x1fe,

            .id = 1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "fe83 0100 0000 0000 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMinTypeWithExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0x1ff,

            .id = 1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff83 0100 0000 0000 00c0 ff01"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxTypeWithExtendedType) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xffff,

            .id = 1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff83 0100 0000 0000 00c0 ffff"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdOneMoreLoDataEmptySegm) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data(data); // dsize = 64


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+64, /* struct size */
                64,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0080 0000 00c0 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmSomeObjData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = 1,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0100 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxNonLargeObjData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = (1 << 15) - 1,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                (1 << 15) - 1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 ff7f 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmOneMoreNonLargeObjData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = (1 << 15),
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                (1 << 15)  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0080 0100 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataEmptySegmMaxLargeObjData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = uint32_t(1 << 31) - 1,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                uint32_t(1 << 31) - 1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 ffff ffff 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, OwnsPersistentIdZeroDataSegmInlineSomeObjData) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 1,

            .dsize = 0,
            .esize = 1,
            .segm = Segment::create_empty_zero_inline()
        };

        hdr.segm.set_inline_data({0x1, 0x2, 0x3});

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+4, /* struct size */
                0,   /* descriptor data size */
                3,  /* segment data size */
                1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff82 0100 0000 0100 03c3 0102"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, NotEnoughRoomForRWNonOwnerTemporalId) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xff,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2}); // dsize = 2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+2, /* struct size */
                2,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        IOSpan io(fp);
        io.seek_wr(2+2 - 1, IOSpan::Seekdir::end); // point 1 byte off (available = 3 bytes)

        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(io); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for writing descriptor's data of "
                        "descriptor {id: 2147483649, type: 255, dsize: 2}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);
        idmgr.reset(0x80000001); // ensure that the descriptor loaded will have the same id than 'dsc'

        // Write a valid descriptor of data size 2
        dsc.write_struct_into(IOSpan(fp));

        // Now, truncate the file so the span will be shorter than the expected size
        fp.resize(2+2 - 1); // shorter by 1 byte

        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), idmgr, ed_blkarr); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for reading descriptor's data of "
                        "descriptor {id: 2147483649, type: 255, dsize: 2}"
                        )
                    )
                )
        );
    }

    TEST(DescriptorTest, NotEnoughRoomForRWOwnsWithPersistentId) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 15,

            .dsize = 0,
            .esize = 42,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);
        dsc.set_data({1, 2}); // dsize = 2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                2,   /* descriptor data size */
                0,  /* segment data size */
                42  /* obj data size */
                );

        IOSpan io(fp);
        io.seek_wr(2+4+2+2+2 - 1, IOSpan::Seekdir::end); // point 1 byte off (available = 11 bytes)

        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(io); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for writing descriptor's data of "
                        "descriptor {id: 15, type: 255, dsize: 2, esize: 42, owns: 0}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);

        // Write a valid descriptor of data size 2
        dsc.write_struct_into(IOSpan(fp));

        // Now, truncate the file so the span will be shorter than the expected size
        fp.resize(2+4+2+2+2 - 1); // shorter by 1 byte

        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), idmgr, ed_blkarr); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for reading descriptor's data of "
                        "descriptor {id: 15, type: 255, dsize: 2, esize: 42, owns: 0}"
                        )
                    )
                )
        );
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

    TEST(DescriptorTest, DescriptorReadOrWriteLess) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map {
            {0xff, DescriptorSubRW::create }
        };
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 15,

            .dsize = 0,
            .esize = 42,
            .segm = Segment::create_empty_zero_inline()
        };

        DescriptorSubRW dsc = DescriptorSubRW(hdr, ed_blkarr);
        dsc.set_data({1, 2}); // dsize = 2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2+2, /* struct size */
                2,   /* descriptor data size */
                0,  /* segment data size */
                42  /* obj data size */
                );

        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(IOSpan(fp)); }),
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
        DefaultDescriptor dsc2 = DefaultDescriptor(hdr, ed_blkarr);
        dsc2.set_data({1, 2});
        dsc2.write_struct_into(IOSpan(fp));

        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), idmgr, ed_blkarr); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "The descriptor subclass underflowed the read pointer and "
                        "processed 0 bytes (left 2 bytes unprocessed of 2 bytes available) and "
                        "left it at position 10 that it is before the end of the data section at position 12."
                        )
                    )
                )
        );
    }

    TEST(DescriptorTest, DescriptorWithExplicitZeroId) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = true,
            .type = 0xff,

            .id = 0,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Writing a descriptor with id = 0 is incorrect. No descriptor should
        // have id of 0 unless it has a temporal id *and* it requires the hi_dsize field
        // (not this case so an exception is expected)
        EXPECT_THAT(
            ensure_called_once([&]() { dsc.write_struct_into(IOSpan(fp)); }),
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Descriptor id is zero in descriptor "
                        "{id: 0, type: 255, dsize: 0, esize: 0, owns: 0}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);

        // this will make the write_struct_into to set the has_id to true...
        hdr.id = 0xffff;
        DefaultDescriptor dsc2 = DefaultDescriptor(hdr, ed_blkarr);
        dsc2.write_struct_into(IOSpan(fp));

        // ...and now we nullify the id field so it would look like a descriptor
        // that has_id but it has a id = 0
        fp[2] = fp[3] = 0;
        XOZ_EXPECT_SERIALIZATION(fp, dsc2,
                "ff82 0000 0000 0000 00c0"
                );

        // Because the dsize of the descriptor is small, there is no reason to have
        // an id = 0.
        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp), idmgr, ed_blkarr); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Repository seems inconsistent/corrupt. "
                        "Descriptor id is zero, detected with partially loaded descriptor "
                        "{id: 0, type: 255, dsize: 0, esize: 0, owns: 0}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);
        idmgr.reset(0x80000001); // ensure that the descriptor loaded will have the same id than 'dsc3'

        // We repeat again has_id = true but we also make the descriptor very large so
        // we force to and id of 0 (because the temporal id is not stored)
        hdr.id = 0x80000001;
        DefaultDescriptor dsc3 = DefaultDescriptor(hdr, ed_blkarr);

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers
        dsc3.set_data(data);

        dsc3.write_struct_into(IOSpan(fp));

        // the id should be 0, see also how the hi_dsize bit is set (0080)
        XOZ_EXPECT_SERIALIZATION(fp, dsc3,
                "ff82 0000 0080 0000 00c0 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );


        // Load should be ok even if the id is 0 in the string. A temporal id should be then
        // set to the loaded descriptor.
        XOZ_EXPECT_DESERIALIZATION(fp, dsc3, idmgr, ed_blkarr);
    }

    TEST(DescriptorTest, DownCast) {
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();
        IDManager idmgr;
        VectorBlockArray ed_blkarr(1024);

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        initialize_descriptor_mapping(descriptors_map);

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xffff, // fake a type that requires ex_type

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        // The concrete Descriptor subclass
        DefaultDescriptor dsc = DefaultDescriptor(hdr, ed_blkarr);

        // Upper cast to Descriptor abstract class
        Descriptor* dsc2 = &dsc;

        // Down cast to Descriptor subclass again
        // If the downcast works, cast<X> does neither throws nor return null
        DefaultDescriptor* dsc3 = dsc2->cast<DefaultDescriptor>();
        EXPECT_NE(dsc3, (DefaultDescriptor*)nullptr);

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
}
