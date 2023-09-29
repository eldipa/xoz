#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/mem/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/default.h"
#include "xoz/exceptions.h"

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
#define XOZ_EXPECT_SIZES(dsc, blk_sz_order, disk_sz, data_sz, segm_data_sz, obj_data_sz) do {      \
    EXPECT_EQ((dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ((dsc).calc_data_space_size(), (unsigned)(data_sz));                                  \
    EXPECT_EQ((dsc).calc_obj_segm_data_space_size((blk_sz_order)), (unsigned)(segm_data_sz));      \
    EXPECT_EQ((dsc).calc_obj_data_size(), (unsigned)(obj_data_sz));       \
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
#define XOZ_EXPECT_DESERIALIZATION(fp, dsc) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp));            \
    dsc2_ptr->write_struct_into(IOSpan(buf2));                           \
    EXPECT_EQ((fp), buf2);                                               \
} while (0)


namespace {
    TEST(DescriptorTest, NoObjNoIdZeroData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NoObjNoIdSomeData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NoObjNoIdSomeDataMaxType) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0x1ff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data({1, 2, 3, 4}); // dsize = 4


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4, /* struct size */
                4,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff09 0102 0304"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NoObjNoIdMaxLoData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data(data); // dsize = 64-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NoObjNoIdOneMoreLoData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data(data); // dsize = 64


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NoObjNoIdMaxHiData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(128-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data(data); // dsize = 128-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NoObjWithIdMaxLoData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64-2);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data(data); // dsize = 64-2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }


    TEST(DescriptorTest, ObjZeroDataEmptySegm) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0000 0000 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjZeroDataEmptySegmMaxType) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0x3ff,

            .obj_id = 1,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff83 0100 0000 0000 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjOneMoreLoDataEmptySegm) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        std::vector<char> data(64);
        std::iota (std::begin(data), std::end(data), 0); // fill with numbers

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data(data); // dsize = 64


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2+64, /* struct size */
                64,   /* descriptor data size */
                0,  /* segment data size */
                0  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0080 0000 00c0 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 "
                "1819 1a1b 1c1d 1e1f 2021 2223 2425 2627 2829 2a2b 2c2d 2e2f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjZeroDataEmptySegmSomeObjData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = 1,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0000 0100 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjZeroDataEmptySegmMaxNonLargeObjData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = (1 << 15) - 1,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                (1 << 15) - 1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0000 ff7f 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjZeroDataEmptySegmOneMoreNonLargeObjData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = (1 << 15),
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                (1 << 15)  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0000 0080 0100 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjZeroDataEmptySegmMaxLargeObjData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = uint32_t(1 << 31) - 1,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+2+2, /* struct size */
                0,   /* descriptor data size */
                0,  /* segment data size */
                uint32_t(1 << 31) - 1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0000 ffff ffff 00c0"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, ObjZeroDataSegmInlineSomeObjData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 1,

            .dsize = 0,
            .size = 1,
            .segm = Segment::create_empty_zero_inline()
        };

        hdr.segm.set_inline_data({0x1, 0x2, 0x3});

        DefaultDescriptor dsc = DefaultDescriptor(hdr);

        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
                2+4+2+4, /* struct size */
                0,   /* descriptor data size */
                3,  /* segment data size */
                1  /* obj data size */
                );

        // Write and check the dump
        dsc.write_struct_into(IOSpan(fp));
        XOZ_EXPECT_SERIALIZATION(fp, dsc,
                "ff80 0100 0000 0100 03c3 0102"
                );

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, dsc);
    }

    TEST(DescriptorTest, NotEnoughRoomForRWNonObjectDescriptor) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = false,
            .type = 0xff,

            .obj_id = 0,

            .dsize = 0,
            .size = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data({1, 2}); // dsize = 2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
                        "non-object descriptor {obj-id: 0, type: 255, dsize: 2}"
                        )
                    )
                )
        );

        XOZ_RESET_FP(fp, FP_SZ);

        // Write a valid descriptor of data size 2
        dsc.write_struct_into(IOSpan(fp));

        // Now, truncate the file so the span will be shorter than the expected size
        fp.resize(2+2 - 1); // shorter by 1 byte

        EXPECT_THAT(
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp)); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for reading descriptor's data of "
                        "non-object descriptor {obj-id: 0, type: 255, dsize: 2}"
                        )
                    )
                )
        );
    }

    TEST(DescriptorTest, NotEnoughRoomForRWObjectDescriptor) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        deinitialize_descriptor_mapping();

        std::map<uint16_t, descriptor_create_fn> non_obj_descriptors;
        std::map<uint16_t, descriptor_create_fn> obj_descriptors;
        initialize_descriptor_mapping(non_obj_descriptors, obj_descriptors);

        struct Descriptor::header_t hdr = {
            .is_obj = true,
            .type = 0xff,

            .obj_id = 15,

            .dsize = 0,
            .size = 42,
            .segm = Segment::create_empty_zero_inline()
        };

        DefaultDescriptor dsc = DefaultDescriptor(hdr);
        dsc.set_data({1, 2}); // dsize = 2


        // Check sizes
        XOZ_EXPECT_SIZES(dsc, blk_sz_order,
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
                        "object descriptor {obj-id: 15, type: 255, dsize: 2, size: 42}"
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
            ensure_called_once([&]() { Descriptor::load_struct_from(IOSpan(fp)); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 1 bytes are available. "
                        "No enough room for reading descriptor's data of "
                        "object descriptor {obj-id: 15, type: 255, dsize: 2, size: 42}"
                        )
                    )
                )
        );
    }
}
