#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
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

namespace {
const size_t FP_SZ = 64;
}

#define XOZ_RESET_FP(fp, sz) do {                                           \
    (fp).clear();                                                           \
    (fp).str(std::string((sz), '\0'));                                      \
    (fp).exceptions(std::ios_base::failbit | std::ios_base::badbit);        \
} while (0)

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
#define XOZ_EXPECT_DESERIALIZATION(fp, segm) do {                       \
    std::stringstream fp2;                                              \
    XOZ_RESET_FP(fp2, FP_SZ);                                           \
    auto curg = (fp).tellg();                                           \
    auto curp = (fp).tellp();                                           \
    (fp).seekg(0);                                                      \
    (fp).seekp(0);                                                      \
    auto segm_sz = (segm).calc_footprint_disk_size();                   \
                                                                        \
    Segment segm = Segment::load_segment((fp), segm_sz);                \
    segm.write(fp2);                                                    \
    EXPECT_EQ((fp).str(), fp2.str());                                   \
    (fp).seekg(curg);                                                   \
    (fp).seekp(curp);                                                   \
    (fp).clear(); /* clear the flags */                                 \
} while (0)

namespace {
    TEST(SegmentTest, ValidEmptyZeroBytes) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm;

        // Check sizes
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                0, /* disc size */
                0 /* allocated size */
                );

        // Write and check the dump
        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "");
        EXPECT_EQ(are_all_zeros(fp), (bool)true);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, ValidEmptyZeroInline) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm = Segment::create_empty_zero_inline();

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, InlineDataOnly) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm;

        segm.set_inline_data({0x41, 0x42});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                2 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c2 4142");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        segm.set_inline_data({0x41, 0x42, 0x43, 0x44});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                4 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c4 4142 4344");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        segm.set_inline_data({0x41, 0x42, 0x43});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                3 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "43c3 4142");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        segm.set_inline_data({0x41});
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                2, /* disc size */
                1 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "41c1");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, InlineDataAsEndOfSegment) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);

        // Empty segment, add "end of segment"
        Segment segm;
        segm.add_end_of_segment();

        // Expect the same as an empty segment with 0-bytes inline data
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                2, /* disc size */
                0 /* allocated size */
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c0");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Remove the inline data, add an extent
        // and add "end of segment" again
        segm.remove_inline_data();
        EXPECT_EQ(segm.has_end_of_segment(), (bool)false);

        segm.add_extent(Extent(1, 1, false)); // 1-block extent
        segm.add_end_of_segment();

        // Expect the same as a segment with one extent + 0-bytes inline data
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                1 << blk_sz_order /* allocated size */
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0008 0100 00c0");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Remove the extent and inline data, add a non-zero length inline data
        // Check that that is enough to consider the segment ended
        segm.clear_extents();
        segm.remove_inline_data();
        EXPECT_EQ(segm.has_end_of_segment(), (bool)false);

        segm.set_inline_data({0x41});
        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        // Now let's try to add the end of segment explicitly
        // Because there was a previous inline data already there
        // nothing changes
        segm.add_end_of_segment();

        // Expect the same as a segment with 1-byte inline data
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                2, /* disc size */
                1 /* allocated size */
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "41c1");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);
    }

    TEST(SegmentTest, InlineDataBadSize) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
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
        EXPECT_EQ(hexdump(fp, 0, 6), "78ff 4100 0000");
        EXPECT_EQ(are_all_zeros(fp, 6), true); // all zeros to the end
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        // This check the maximum allowed minus 1
        segm.set_inline_data(std::vector<uint8_t>((1 << 6) - 2));
        segm.inline_data()[0] = 0x41;
        segm.inline_data()[segm.inline_data().size()-1] = 0x78;

        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                64, /* disc size */
                62 /* allocated size */
                );

        segm.write(fp);
        EXPECT_EQ(hexdump(fp, 0, 6), "00fe 4100 0000");
        EXPECT_EQ(are_all_zeros(fp, 6, 57), true); // all zeros to the end except the last byte
        EXPECT_EQ(hexdump(fp, 6+57), "78"); // chk last byte
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, OneExtentFullBlockOnly) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm;

        segm.add_extent(Extent(0xab, 0, false)); // 0 full block (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                0 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab00 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(0x00abcdef, 0, false)); // 0 full block (large extent) (diff addr)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                0 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "ab00 efcd 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(0xab, 1, false)); // 1 full block (small extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                1 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0008 ab00");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(1, 3, false)); // 3 full blocks (small extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                4, /* disc size */
                3 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0018 0100");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(0xab, 16, false)); // 16 full blocks (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                16 << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab00 1000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(0xab, (1 << 15), false)); // 32k full blocks (large extent)
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                (1 << 15) << blk_sz_order /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab00 0080");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, OneExtentSubAllocOnly) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm;

        segm.add_extent(Extent(0xab, 0, true));    // 0 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                0 /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab00 0000");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(0xab, 0b00001001, true));    // 2 sub-alloc'd blocks
        EXPECT_EQ(segm.calc_footprint_disk_size(), (unsigned) 6);
        EXPECT_EQ(segm.calc_usable_space_size(blk_sz_order), (unsigned) (2 << (blk_sz_order - 4)));
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                2 << (blk_sz_order - 4)  /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab00 0900");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(1, 0b11111111, true));    // 8 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                8 << (blk_sz_order - 4)  /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 0100 ff00");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        segm.add_extent(Extent(1, 0b1111111111111111, true));    // 16 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                6, /* disc size */
                16 << (blk_sz_order - 4)  /* allocated size */
                );

        segm.write(fp);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 0100 ffff");
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, SeveralExtentsAndInline) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
        XOZ_RESET_FP(fp, FP_SZ);

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
        XOZ_EXPECT_DESERIALIZATION(fp, segm);
    }

    TEST(SegmentTest, FileOverflowNotEnoughRoom) {
        const uint8_t blk_sz_order = 10;
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ / 2); // half file size, easier to test TODO test FP_SZ only
        Segment segm;

        // Large but perfectly valid inline data
        segm.set_inline_data(std::vector<uint8_t>(FP_SZ / 2));
        std::iota (std::begin(segm.inline_data()), std::end(segm.inline_data()), 0); // fill with numbers

        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                34, /* disc size */
                32 /* allocated size */
                );

        // The read/write however exceeds the file size
        EXPECT_THAT(
            [&]() { Segment::load_segment(fp, segm.calc_footprint_disk_size()); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 34 bytes but only 32 bytes are available. "
                        "Read operation at position 0 failed (end position is at 32)"
                        )
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.write(fp); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 34 bytes but only 32 bytes are available. "
                        "Write operation at position 0 failed (end position is at 32)"
                        )
                    )
                )
        );

        // Nothing was written
        EXPECT_EQ(are_all_zeros(fp), (bool)true);

        XOZ_RESET_FP(fp, FP_SZ / 2);
        segm.remove_inline_data();


        // Very long but perfectly valid segment of 6 suballoc blocks
        for (int i = 0; i < 6; ++i) {
            // each extent should have a footprint of 6 bytes
            segm.add_extent(Extent(0x2ff + (0x2ff * i), 0xffff, true));
        }
        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                36, /* 6 extents times 6 bytes each -- disc size */
                6 << blk_sz_order /* allocated size */
                );

        // The read/write however exceeds the file size
        EXPECT_THAT(
            [&]() { Segment::load_segment(fp, segm.calc_footprint_disk_size()); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 36 bytes but only 32 bytes are available. "
                        "Read operation at position 0 failed (end position is at 32)"
                        )
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.write(fp); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 36 bytes but only 32 bytes are available. "
                        "Write operation at position 0 failed (end position is at 32)"
                        )
                    )
                )
        );

        // Nothing was written
        EXPECT_EQ(are_all_zeros(fp), (bool)true);

        XOZ_RESET_FP(fp, FP_SZ / 2);

        // The same but this time write some dummy bytes in the file
        // to generate an offset on the writes and a different offset
        // on the read
        char buf[1];
        fp.write("ABCD", 4); // a 4 bytes offset for writing
        fp.read(buf, 1); // a 1 byte offset for readings
        EXPECT_EQ(buf[0], (char)'A');

        XOZ_EXPECT_SIZES(segm, blk_sz_order,
                36, /* 6 extents times 6 bytes each -- disc size */
                6 << blk_sz_order /* allocated size */
                );

        // The read/write however exceeds the file size
        EXPECT_THAT(
            [&]() { Segment::load_segment(fp, segm.calc_footprint_disk_size()); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 36 bytes but only 31 bytes are available. "
                        "Read operation at position 1 failed (end position is at 32)"
                        )
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.write(fp); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 36 bytes but only 28 bytes are available. "
                        "Write operation at position 4 failed (end position is at 32)"
                        )
                    )
                )
        );

        // Nothing was written (except the dummy values)
        EXPECT_EQ(are_all_zeros(fp, 4), (bool)true);
    }

    TEST(SegmentTest, ReadSegmSizeNotMultipleOfTwo) {
        std::stringstream fp;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm;

        // Read size must be a multiple of 2
        EXPECT_THAT(
            [&]() { Segment::load_segment(fp, 3); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "the size to read 3 must be a multiple of 2."
                        )
                    )
                )
        );
    }
}
