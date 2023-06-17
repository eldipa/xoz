#include "xoz/extent.h"
#include "xoz/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;


// Check the size in bytes of the exts in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(exts, blk_sz_order, disk_sz, allocated_sz) do {                \
    EXPECT_EQ(calc_footprint_disk_size((exts)), (unsigned)(disk_sz));                          \
    EXPECT_EQ(calc_usable_space_size((exts), (blk_sz_order)), (unsigned)(allocated_sz));   \
} while (0)

// Check that the serialization of the extents in fp are of the
// expected size (call calc_footprint_disk_size) and they match
// byte-by-byte with the expected data (in hexdump)
#define XOZ_EXPECT_SERIALIZATION(fp, exts, data) do {           \
    EXPECT_EQ((fp).str().size(), calc_footprint_disk_size((exts)));    \
    EXPECT_EQ(hexdump((fp)), (data));                           \
} while (0)

// Load from fp the extents and serialize it back again into
// a temporal fp2 stream. Then compare both (they should be the same)
#define XOZ_EXPECT_DESERIALIZATION(fp, endpos) do {             \
    std::stringstream fp2;                                      \
    auto curg = (fp).tellg();                                   \
    auto curp = (fp).tellp();                                   \
    (fp).seekg(0);                                              \
    (fp).seekp(0);                                              \
                                                                \
    write_ext_arr(fp2, (endpos), load_ext_arr((fp), (endpos))); \
    EXPECT_EQ((fp).str(), fp2.str());                           \
    (fp).seekg(curg);                                           \
    (fp).seekp(curp);                                           \
    (fp).clear(); /* clear the flags */                         \
} while (0)

namespace {
    TEST(ExtentTest, BlockNumberBits) {
        // Block numbers are 26 bits long
        // This test check that the 25th bit is preserved (being 0th the lowest)
        // and the 26th is dropped (because it would require 27 bits)
        Extent ext1((1 << 25) | (1 << 26), 1, false);
        EXPECT_EQ(ext1.blk_nr(), (uint32_t)(1 << 25));

        // Suballoc'd does not change the above
        Extent ext2((1 << 25) | (1 << 26), 1, true);
        EXPECT_EQ(ext2.blk_nr(), (uint32_t)(1 << 25));

        // Check higher bits are preserved when hi_blk_nr() is used
        Extent ext3((1 << 25) | (1 << 26), 1, false);
        EXPECT_EQ(ext3.hi_blk_nr(), (uint16_t)((1 << 25) >> 16));

        // Check lower bits
        Extent ext4((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext4.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));

        // Suballoc'd does not change the above
        Extent ext5((1 << 15) | (1 << 3), 1, true);
        EXPECT_EQ(ext5.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));

        // Check higher and lower bits
        Extent ext6((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext6.hi_blk_nr(), (uint16_t)(0));
        EXPECT_EQ(ext6.lo_blk_nr(), (uint16_t)(((1 << 15) | (1 << 3))));
    }

    TEST(ExtentTest, BlockSuballoced) {
        Extent ext1(1, 0x8142, true);
        EXPECT_EQ(ext1.blk_bitmap(), (uint16_t)(0x8142));
        EXPECT_EQ(ext1.is_suballoc(), true);
    }

    TEST(ExtentTest, CalcSizeInvalidEmpty) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts;

        // An "uninitialized/empty" ExtentGroup is *not* a valid
        // empty ExtentGroup.
        EXPECT_THAT(
            [&]() { calc_footprint_disk_size(exts); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("ExtentGroup is literally empty: no extents and no inline data.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { calc_usable_space_size(exts, blk_sz_order); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("ExtentGroup is literally empty: no extents and no inline data.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { write_ext_arr(fp, endpos, exts); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("ExtentGroup is literally empty: no extents and no inline data.")
                    )
                )
        );

        EXPECT_EQ(fp.str().size(), (unsigned) 0);
    }

    TEST(ExtentTest, CalcSizeValidEmpty) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts = ExtentGroup::createEmpty();

        // Check sizes
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                2, /* disc size */
                0 /* allocated size */
                );

        // Write and check the dump
        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "00c0");

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
    }

    TEST(ExtentTest, CalcSizeInlineDataOnly) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts;

        exts.set_inline_data({0x41, 0x42});
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                4, /* disc size */
                2 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "00c2 4142");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        fp.str(""); // reset

        exts.set_inline_data({0x41, 0x42, 0x43, 0x44});
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                4 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "00c4 4142 4344");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        fp.str(""); // reset

        exts.set_inline_data({0x41, 0x42, 0x43});
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                4, /* disc size */
                3 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "43c3 4142");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        fp.str(""); // reset

        exts.set_inline_data({0x41});
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                2, /* disc size */
                1 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "41c1");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
    }

    TEST(ExtentTest, CalcSizeInlineDataBadSize) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts;

        exts.set_inline_data(std::vector<uint8_t>(1 << 6));

        // Inline data size has a limit
        EXPECT_THAT(
            [&]() { calc_footprint_disk_size(exts); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_THAT(
            [&]() { calc_usable_space_size(exts, blk_sz_order); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_THAT(
            [&]() { write_ext_arr(fp, endpos, exts); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_EQ(fp.str().size(), (unsigned) 0);

        // This check the maximum allowed
        exts.set_inline_data(std::vector<uint8_t>((1 << 6) - 1));
        exts.raw[0] = 0x41;
        exts.raw[exts.raw.size()-1] = 0x78;

        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                64, /* disc size */
                63 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        EXPECT_EQ(fp.str().size(), calc_footprint_disk_size(exts));
        EXPECT_EQ(hexdump(fp).substr(0, 14), "78ff 4100 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        fp.str("");

        // This check the maximum allowed minus 1
        exts.set_inline_data(std::vector<uint8_t>((1 << 6) - 2));
        exts.raw[0] = 0x41;
        exts.raw[exts.raw.size()-1] = 0x78;

        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                64, /* disc size */
                62 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        EXPECT_EQ(fp.str().size(), calc_footprint_disk_size(exts));
        EXPECT_EQ(hexdump(fp).substr(0, 14), "00fe 4100 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
    }

    TEST(ExtentTest, CalcSizeOneExtentFullBlockOnly) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts;

        exts.add_extent(Extent(0xab, 0, false)); // 0 full block (large extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                0 << blk_sz_order /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0000 ab00 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(0x00abcdef, 0, false)); // 0 full block (large extent) (diff addr)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                0 << blk_sz_order /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "ab00 efcd 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(0xab, 1, false)); // 1 full block (small extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                4, /* disc size */
                1 << blk_sz_order /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0008 ab00");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(1, 3, false)); // 3 full blocks (small extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                4, /* disc size */
                3 << blk_sz_order /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0018 0100");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(0xab, 16, false)); // 16 full blocks (large extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                16 << blk_sz_order /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0000 ab00 1000");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(0xab, (1 << 15), false)); // 32k full blocks (large extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                (1 << 15) << blk_sz_order /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0000 ab00 0080");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
    }

    TEST(ExtentTest, CalcSizeOneExtentSubAllocOnly) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts;

        exts.add_extent(Extent(0xab, 0, true));    // 0 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                0 /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0080 ab00 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(0xab, 0b00001001, true));    // 2 sub-alloc'd blocks
        EXPECT_EQ(calc_footprint_disk_size(exts), (unsigned) 6);
        EXPECT_EQ(calc_usable_space_size(exts, blk_sz_order), (unsigned) (2 << (blk_sz_order - 4)));
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                2 << (blk_sz_order - 4)  /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0080 ab00 0900");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(1, 0b11111111, true));    // 8 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                8 << (blk_sz_order - 4)  /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0080 0100 ff00");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);

        exts.clear_extents();
        fp.str("");

        exts.add_extent(Extent(1, 0b1111111111111111, true));    // 16 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                16 << (blk_sz_order - 4)  /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts, "0080 0100 ffff");
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
    }

    TEST(ExtentTest, CalcSizeSeveralExtentsAndInline) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp;
        ExtentGroup exts;

        exts.add_extent(Extent(1, 16, false)); // 16 full blocks (large extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                6, /* disc size */
                16 << blk_sz_order   /* allocated size */
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0000 0100 1000"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
        fp.str("");

        exts.add_extent(Extent(2, 0, true));    // 0 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                12, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0)
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0004 0100 1000 "
                "0080 0200 0000"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
        fp.str("");

        exts.add_extent(Extent(3, 1, false)); // 1 full block (small extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                16, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order)
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0004 0100 1000 "
                "0084 0200 0000 "
                "0008 0300"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
        fp.str("");

        exts.add_extent(Extent(4, 0b00001001, true));    // 2 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                22, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4))
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0004 0100 1000 "
                "0084 0200 0000 "
                "000c 0300 "
                "0080 0400 0900"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
        fp.str("");

        exts.add_extent(Extent(5, 0, false)); // 0 full block (large extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                28, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0)
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0004 0100 1000 "
                "0084 0200 0000 "
                "000c 0300 "
                "0084 0400 0900 "
                "0000 0500 0000"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
        fp.str("");

        exts.set_inline_data({0xaa, 0xbb, 0xcc, 0xdd}); // 4 bytes of inline data
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                34, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (4)
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0004 0100 1000 "
                "0084 0200 0000 "
                "000c 0300 "
                "0084 0400 0900 "
                "0004 0500 0000 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
        fp.str("");

        exts.add_extent(Extent(6, 8, false)); // 8 full blocks (small extent)
        XOZ_EXPECT_SIZES(exts, blk_sz_order,
                38, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (4) +
                (8 << blk_sz_order)
                );

        write_ext_arr(fp, endpos, exts);
        XOZ_EXPECT_SERIALIZATION(fp, exts,
                "0004 0100 1000 "
                "0084 0200 0000 "
                "000c 0300 "
                "0084 0400 0900 "
                "0004 0500 0000 "
                "0044 0600 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, endpos);
    }
}
