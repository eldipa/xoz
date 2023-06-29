#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;


// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(segm, blk_sz_order, disk_sz, allocated_sz) do {                \
    EXPECT_EQ((segm).calc_footprint_disk_size(), (unsigned)(disk_sz));                          \
    EXPECT_EQ((segm).calc_usable_space_size((blk_sz_order)), (unsigned)(allocated_sz));   \
} while (0)

// Check that the serialization of the extents in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION(fp, segm, data) do {           \
    EXPECT_EQ(hexdump((fp), 0, (segm).calc_footprint_disk_size()), (data));         \
    EXPECT_EQ(are_all_zeros((fp), (segm).calc_footprint_disk_size()), (bool)true);  \
} while (0)

// Load from fp the extents and serialize it back again into
// a temporal fp2 stream. Then compare both (they should be the same)
#define XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos) do {            \
    std::stringstream fp2(std::string(64, '\0'));                       \
    auto curg = (fp).tellg();                                           \
    auto curp = (fp).tellp();                                           \
    (fp).seekg(0);                                                      \
    (fp).seekp(0);                                                      \
    auto segm_sz = (segm).calc_footprint_disk_size();                   \
                                                                        \
    Segment segm = Segment::load_segment((fp), segm_sz, (endpos));      \
    segm.write(fp2);                                                    \
    EXPECT_EQ((fp).str(), fp2.str());                                   \
    (fp).seekg(curg);                                                   \
    (fp).seekp(curp);                                                   \
    (fp).clear(); /* clear the flags */                                 \
} while (0)

namespace {
    TEST(SegmentTest, InvalidEmpty) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp(std::string(64, '\0'));
        Segment segm;

        // An "uninitialized/empty" Segment is *not* a valid
        // empty Segment.
        EXPECT_THAT(
            [&]() { segm.calc_footprint_disk_size(); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Segment is literally empty: no extents and no inline data.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { segm.calc_usable_space_size(blk_sz_order); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Segment is literally empty: no extents and no inline data.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { segm.write(fp); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Segment is literally empty: no extents and no inline data.")
                    )
                )
        );

        EXPECT_EQ(are_all_zeros(fp), (bool)true);
    }

    TEST(SegmentTest, ValidEmpty) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp(std::string(64, '\0'));
        Segment segm = Segment::createEmpty();

        // Check sizes
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                2, /* disc size */
                0 /* allocated size */
                );

        // Write and check the dump
        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c0");

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
    }

    TEST(SegmentTest, InlineDataOnly) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp(std::string(64, '\0'));
        Segment segm;

        segm.set_inline_data({0x41, 0x42});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                2 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c2 4142");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        fp.str(std::string(64, '\0')); // reset

        segm.set_inline_data({0x41, 0x42, 0x43, 0x44});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                4 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c4 4142 4344");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        fp.str(std::string(64, '\0')); // reset

        segm.set_inline_data({0x41, 0x42, 0x43});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                3 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "43c3 4142");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        fp.str(std::string(64, '\0')); // reset

        segm.set_inline_data({0x41});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                2, /* disc size */
                1 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "41c1");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
    }

    TEST(SegmentTest, InlineDataBadSize) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp(std::string(64, '\0'));
        Segment segm;

        segm.set_inline_data(std::vector<uint8_t>(1 << 6));

        // Inline data size has a limit
        EXPECT_THAT(
            [&]() { segm.calc_footprint_disk_size(); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.calc_usable_space_size(blk_sz_order); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.write(fp); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_EQ(are_all_zeros(fp), (bool)true);

        // This check the maximum allowed
        segm.set_inline_data(std::vector<uint8_t>((1 << 6) - 1));
        segm.inline_data()[0] = 0x41;
        segm.inline_data()[segm.inline_data().size()-1] = 0x78;

        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                64, /* disc size */
                63 /* allocated size */
                );

        segm.write(fp);
        EXPECT_EQ(fp.str().size(), segm.calc_footprint_disk_size());
        EXPECT_EQ(hexdump(fp).substr(0, 14), "78ff 4100 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        fp.str(std::string(64, '\0')); // reset

        // This check the maximum allowed minus 1
        segm.set_inline_data(std::vector<uint8_t>((1 << 6) - 2));
        segm.inline_data()[0] = 0x41;
        segm.inline_data()[segm.inline_data().size()-1] = 0x78;

        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                64, /* disc size */
                62 /* allocated size */
                );

        segm.write(fp);
        EXPECT_EQ(fp.str().size(), segm.calc_footprint_disk_size());
        EXPECT_EQ(hexdump(fp).substr(0, 14), "00fe 4100 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
    }

    TEST(SegmentTest, OneExtentFullBlockOnly) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp(std::string(64, '\0'));
        Segment segm;

        segm.add_extent(Extent(0xab, 0, false)); // 0 full block (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                0 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab00 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(0x00abcdef, 0, false)); // 0 full block (large extent) (diff addr)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                0 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "ab00 efcd 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(0xab, 1, false)); // 1 full block (small extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                1 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0008 ab00");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(1, 3, false)); // 3 full blocks (small extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                3 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0018 0100");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(0xab, 16, false)); // 16 full blocks (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                16 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab00 1000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(0xab, (1 << 15), false)); // 32k full blocks (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                (1 << 15) << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab00 0080");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
    }

    TEST(SegmentTest, OneExtentSubAllocOnly) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp(std::string(64, '\0'));
        Segment segm;

        segm.add_extent(Extent(0xab, 0, true));    // 0 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                0 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab00 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(0xab, 0b00001001, true));    // 2 sub-alloc'd blocks
        EXPECT_EQ(segm.calc_footprint_disk_size(), (unsigned) 6);
        EXPECT_EQ(segm.calc_usable_space_size(blk_sz_order), (unsigned) (2 << (blk_sz_order - 4)));
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                2 << (blk_sz_order - 4)  /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab00 0900");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(1, 0b11111111, true));    // 8 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                8 << (blk_sz_order - 4)  /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 0100 ff00");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);

        segm.clear_extents();
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(1, 0b1111111111111111, true));    // 16 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                16 << (blk_sz_order - 4)  /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 0100 ffff");
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
    }

    TEST(SegmentTest, SeveralExtentsAndInline) {
        const uint8_t blk_sz_order = 10;
        const uint64_t endpos = (1 << 20);
        std::stringstream fp(std::string(64, '\0'));
        Segment segm;

        segm.add_extent(Extent(1, 16, false)); // 16 full blocks (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                16 << blk_sz_order   /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(2, 0, true));    // 0 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                12, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0)
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000 "
                "0080 0200 0000"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(3, 1, false)); // 1 full block (small extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                16, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order)
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000 "
                "0080 0200 0000 "
                "0008 0300"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(4, 0b00001001, true));    // 2 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                22, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4))
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000 "
                "0080 0200 0000 "
                "0008 0300 "
                "0080 0400 0900"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(5, 0, false)); // 0 full block (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                28, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0)
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000 "
                "0080 0200 0000 "
                "0008 0300 "
                "0080 0400 0900 "
                "0000 0500 0000"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
        fp.str(std::string(64, '\0')); // reset

        segm.set_inline_data({0xaa, 0xbb, 0xcc, 0xdd}); // 4 bytes of inline data
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                34, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (4)
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000 "
                "0080 0200 0000 "
                "0008 0300 "
                "0080 0400 0900 "
                "0000 0500 0000 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
        fp.str(std::string(64, '\0')); // reset

        segm.add_extent(Extent(6, 8, false)); // 8 full blocks (small extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
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

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 0100 1000 "
                "0080 0200 0000 "
                "0008 0300 "
                "0080 0400 0900 "
                "0000 0500 0000 "
                "0040 0600 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_DESERIALIZATION(fp, segm, endpos);
    }
}
