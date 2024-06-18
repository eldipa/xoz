#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/err/exceptions.h"
#include "xoz/mem/inet_checksum.h"

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
const size_t FP_SZ = 64;
}

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(segm, disk_sz, allocated_sz, allocated_sz_without_inline) do {                  \
    EXPECT_EQ((segm).calc_struct_footprint_size(), (unsigned)(disk_sz));                    \
    EXPECT_EQ((segm).calc_data_space_size(), (unsigned)(allocated_sz));   \
    EXPECT_EQ((segm).calc_data_space_size(false), (unsigned)(allocated_sz_without_inline));   \
} while (0)

// Check that the serialization of the extents in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION(fp, segm, data) do {                       \
    EXPECT_EQ(hexdump((fp), 0, (segm).calc_struct_footprint_size()), (data));         \
    EXPECT_EQ(are_all_zeros((fp), (segm).calc_struct_footprint_size()), (bool)true);  \
} while (0)

// Calc checksum over the fp (bytes) and expect to be the given by parameter
#define XOZ_EXPECT_CHECKSUM(fp, segm, checksum) do {    \
    EXPECT_EQ(inet_checksum((uint8_t*)(fp).data(), (segm).calc_struct_footprint_size()), (checksum)); \
} while (0)

// Load from fp the extents and serialize it back again into
// a temporal fp2 stream. Then compare both (they should be the same)
// and both (load and write) compute the same checksum
#define XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm) do {                        \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    auto segm_len = (segm).length();                                     \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    Segment segm2 = Segment::load_struct_from(IOSpan(fp), (blk_sz_order), Segment::EndMode::ExplicitLen, segm_len, &checksum2); \
    segm2.write_struct_into(IOSpan(buf2), &checksum3);                   \
    EXPECT_EQ((fp), buf2);                                               \
    EXPECT_EQ((segm) == segm2, bool(true));                              \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)

#define XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm) do {           \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    Segment segm2 = Segment::load_struct_from(IOSpan(fp), (blk_sz_order), Segment::EndMode::AnyEnd, -1, &checksum2); \
    segm2.write_struct_into(IOSpan(buf2), &checksum3);                   \
    EXPECT_EQ((fp), buf2);                                               \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)

namespace {
    TEST(SegmentTest, ValidEmptyZeroLength) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // Check sizes
        XOZ_EXPECT_SIZES(segm,
                0, /* disc size */
                0, /* allocated size */
                0
                );

        // Write and check the dump
        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "");
        EXPECT_EQ(are_all_zeros(fp), (bool)true);

        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, ValidEmptyZeroInline) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm = Segment::create_empty_zero_inline(blk_sz_order);

        // Check sizes
        XOZ_EXPECT_SIZES(segm,
                2, /* disc size */
                0, /* allocated size */
                0
                );

        // Write and check the dump
        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c0");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Load, write it back and check both byte-strings
        // are the same
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, InlineDataOnly) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        segm.set_inline_data({0x41, 0x42});
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                2, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c2 4142");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        segm.set_inline_data({0x41, 0x42, 0x43, 0x44});
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                4, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c4 4142 4344");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        segm.set_inline_data({0x41, 0x42, 0x43});
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                3, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "43c3 4142");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        segm.set_inline_data({0x41});
        XOZ_EXPECT_SIZES(segm,
                2, /* disc size */
                1, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "41c1");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, InlineDataAsEndOfSegment) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);

        // Empty segment, add "end of segment"
        Segment segm(blk_sz_order);
        segm.add_end_of_segment();

        // Expect the same as an empty segment with 0-bytes inline data
        XOZ_EXPECT_SIZES(segm,
                2, /* disc size */
                0, /* allocated size */
                0
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "00c0");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Remove the inline data, add an extent
        // and add "end of segment" again
        segm.remove_inline_data();
        EXPECT_EQ(segm.has_end_of_segment(), (bool)false);

        segm.add_extent(Extent(0x2ff, 1, false)); // 1-block extent
        segm.add_end_of_segment();

        // Expect the same as a segment with one extent + 0-bytes inline data
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                1 << blk_sz_order, /* allocated size */
                1 << blk_sz_order
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0008 ff02 00c0");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Remove the extent and inline data, add a non-zero length inline data
        // Check that that is enough to consider the segment ended
        segm.remove_inline_data();
        segm.clear_extents();
        EXPECT_EQ(segm.has_end_of_segment(), (bool)false);

        segm.set_inline_data({0x41});
        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        // Now let's try to add the end of segment explicitly
        // Because there was a previous inline data already there
        // nothing changes
        segm.add_end_of_segment();

        // Expect the same as a segment with 1-byte inline data
        XOZ_EXPECT_SIZES(segm,
                2, /* disc size */
                1, /* allocated size */
                0
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "41c1");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);
    }

    TEST(SegmentTest, UnexpectedInlineDataAsEndOfSegmentMakesFail) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);

        Segment segm(blk_sz_order);
        segm.add_extent(Extent(0x2ff, 1, false)); // 1-block extent
        segm.add_end_of_segment();

        // Expect the same as a segment with one extent + 0-bytes inline data
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                1 << blk_sz_order, /* allocated size */
                1 << blk_sz_order
                );

        EXPECT_EQ(segm.has_end_of_segment(), (bool)true);

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);

        // Now we expect a segment of length 3 which obviously will not happen
        // (the segment has a length of 2)
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::ExplicitLen, 3); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Repository seems inconsistent/corrupt. "
                        "Expected to read a segment that of length 3 "
                        "but an inline-extent was found before and "
                        "made the segment shorter of length 2."

                        )
                    )
                )
        );

        // We want to load until the end of the io but the end-of-segment will be found earlier
        // and bytes will kept unread and that is an error.
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::IOEnd); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Repository seems inconsistent/corrupt. "
                        "Expected to read a segment that ends at the end of the io object "
                        "but an inline-extent was found before that, obtaining a segment "
                        "with a length of 2 and in the io still remains 58 bytes."
                        )
                    )
                )
        );

        // Now we try to load until the inline data but we shrink the fp (truncate)
        // such the inline data is missing
        // (but the first 1-block extent is intact so no half/partial read happens)
        fp.resize(4);
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::InlineEnd); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Repository seems inconsistent/corrupt. "
                        "Expected to read a segment that ends "
                        "in an inline-extent but such was not found "
                        "and the segment got a length of 1."
                        )
                    )
                )
        );
    }

    TEST(SegmentTest, InlineDataBadSize) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        segm.set_inline_data(std::vector<char>(Segment::MaxInlineSize + 1));

        // Inline data size has a limit
        EXPECT_THAT(
            [&]() { segm.calc_struct_footprint_size(); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.calc_data_space_size(); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_THAT(
            [&]() { segm.write_struct_into(IOSpan(fp), &checksum); },
            ThrowsMessage<WouldEndUpInconsistentXOZ>(
                AllOf(
                    HasSubstr("Inline data too large: it has 64 bytes but only up to 63 bytes are allowed.")
                    )
                )
        );
        EXPECT_EQ(are_all_zeros(fp), (bool)true);
        EXPECT_EQ(checksum, (uint32_t)0); // no checksum was added

        // This check the maximum allowed
        segm.set_inline_data(std::vector<char>((1 << 6) - 1));
        segm.inline_data()[0] = 0x41;
        segm.inline_data()[segm.inline_data().size()-1] = 0x78;

        XOZ_EXPECT_SIZES(segm,
                64, /* disc size */
                63, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        EXPECT_EQ(hexdump(fp, 0, 6), "78ff 4100 0000");
        EXPECT_EQ(are_all_zeros(fp, 6), true); // all zeros to the end
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        XOZ_RESET_FP(fp, FP_SZ);

        // This check the maximum allowed minus 1
        segm.set_inline_data(std::vector<char>((1 << 6) - 2));
        segm.inline_data()[0] = 0x41;
        segm.inline_data()[segm.inline_data().size()-1] = 0x78;

        XOZ_EXPECT_SIZES(segm,
                64, /* disc size */
                62, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        EXPECT_EQ(hexdump(fp, 0, 6), "00fe 4100 0000");
        EXPECT_EQ(are_all_zeros(fp, 6, 57), true); // all zeros to the end except the last byte
        EXPECT_EQ(hexdump(fp, 6+57), "78"); // chk last byte
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, OneExtentFullBlockOnly) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);


        // Extent that it is neither near (far from prev extent) nor
        // it cannot use smallcnt (blk_cnt == 0)
        // so it will require 6 bytes in total
        segm.add_extent(Extent(0x2ab, 0, false));
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                0 << blk_sz_order, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab02 0000");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // Extent that it is near enough to the previous extent (at blk_nr = 0)
        // but still without using smallcnt so it requires 4 bytes
        segm.add_extent(Extent(0x01, 0, false));
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                0 << blk_sz_order, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0104 0000");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // Go back to a "not near enough" extent but this time with
        // a block count that fits in smallcnt hence requiring 4 bytes
        segm.add_extent(Extent(0xfab, 1, false));
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                1 << blk_sz_order, /* allocated size */
                1 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0008 ab0f");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // Extent near to previous extent and using a smallcnt of 3: 2 bytes only
        segm.add_extent(Extent(1, 3, false));
        XOZ_EXPECT_SIZES(segm,
                2, /* disc size */
                3 << blk_sz_order, /* allocated size */
                3 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "011c");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // Extent (not near) with "just" enough blocks to fit a smallcnt
        segm.add_extent(Extent(0xfab, 15, false));
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                15 << blk_sz_order, /* allocated size */
                15 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0078 ab0f");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // Extent (not near) with "just" enough blocks to *not* fit a smallcnt
        // (block count is above the maximum for smallcnt)
        segm.add_extent(Extent(0xfab, 16, false));
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                16 << blk_sz_order, /* allocated size */
                16 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab0f 1000");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // Extent (not near) with the maximum block count possible
        segm.add_extent(Extent(0xfab, (1 << 15), false)); // 32k full blocks (large extent)
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                (1 << 15) << blk_sz_order, /* allocated size */
                (1 << 15) << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0000 ab0f 0080");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, OneExtentSubAllocOnly) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // An extent near to the prev extent (blk_nr = 0) so it does not
        // require 2 bytes for storing the full blk nr *but* because
        // it is a suballoc it required 2 bytes for the bitmask
        // raising a total of 4 bytes
        // (the bitmask is empty so the suballoc is not allocating anything)
        segm.add_extent(Extent(0xab, 0, true));
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                0, /* allocated size */
                0
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "ab84 0000");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // An extent not-near (far from prev extent) so it requires +2
        // bytes for the blk_nr with a total of 6 bytes (+2 hdr, +2 blk nr +2 bitmask)
        //
        // In this case the bitmask has 2 bits set: 2 subblocks alloc'd
        segm.add_extent(Extent(0xdab, 0b00001001, true));
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                2 << (blk_sz_order - 4),  /* allocated size */
                2 << (blk_sz_order - 4)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab0d 0900");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // The same but with its bitmask half full: 8 subblocks alloc'd
        segm.add_extent(Extent(0xdab, 0b11111111, true));
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                8 << (blk_sz_order - 4),  /* allocated size */
                8 << (blk_sz_order - 4)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab0d ff00");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // The same but with its bitmask totally full: 16 subblocks alloc'd
        segm.add_extent(Extent(0xdab, 0b1111111111111111, true));    // 16 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                16 << (blk_sz_order - 4),  /* allocated size */
                16 << (blk_sz_order - 4)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0080 ab0d ffff");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);

        segm.clear_extents();
        XOZ_RESET_FP(fp, FP_SZ);

        // The same full set Extent but near enough to not require a blk nr
        // (so 4 bytes only)
        segm.add_extent(Extent(0x6, 0b1111111111111111, true));    // 16 sub-alloc'd blocks
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                16 << (blk_sz_order - 4),  /* allocated size */
                16 << (blk_sz_order - 4)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm, "0684 ffff");
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, SeveralExtentsAndInline) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // Extent not-near the prev extent (+2 bytes) with a blk count
        // that does not fit in smallcnt (+2 bytes) so raising a total
        // of 6 bytes
        //
        // [                e00      e10        ] addr
        // [                 XX...XX            ] blks
        segm.add_extent(Extent(0xe00, 16, false)); // 16 blocks
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                16 << blk_sz_order,   /* allocated size */
                16 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append a extent near to the prev extent (blk_nr 0xe00).
        // It is immediately after the prev extent so the offset is 0
        // The extent is for suballoc so it requires the bitmask (+2 bytes)
        // despite alloc'ing 0 subblocks
        //
        // [                e00     e10        ] addr
        // [                 XX...XX|Y         ] blks
        segm.add_extent(Extent(0xe10, 0, true));    // 0 subblocks
        XOZ_EXPECT_SIZES(segm,
                6+4, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append an extent near the prev extent (blk_nr 0xe10) which was
        // 1 block length (for suballocation). It is immediately after the
        // previous extent (offset = 0)
        // The current extent has also 1 block so it fits in a smallcnt
        // with a total of 2 bytes only
        //
        // [                e00    e10 e11        ] addr
        // [                 XX...XX|Y|Z|         ] blks
        segm.add_extent(Extent(0xe11, 1, false)); // 1 block count, fits in smallcnt
        XOZ_EXPECT_SIZES(segm,
                6+4+2, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000 "
                "000c"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append an extent far from prev extent. This extent
        // is 1 block length for suballocation (with 2 subblocks set)
        // This gives a total of 6 bytes
        //
        // [     4           e00    e10 e11        ] addr
        // [     X           XX...XX|Y|Z|         ] blks
        segm.add_extent(Extent(4, 0b00001001, true));    // 2 subblocks
        XOZ_EXPECT_SIZES(segm,
                6+4+2+6, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4))
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000 "
                "000c "
                "0080 0400 0900"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append another extent, this has 0 block length
        // (smallcnt cannot be used so, +2)
        // and it is near the previous *but* backwards
        //
        // It is 1 block behind the previous extent: this is because
        // the current extent is 0-blocks length so between blk nr 3
        // and blk nr 4 there are 1 block "of gap" between the two extents
        //
        // [    34          e00    e10 e11        ] addr
        // [    0X           XX...XX|Y|Z|         ] blks
        segm.add_extent(Extent(3, 0, false)); // 0 full block (large extent)
        XOZ_EXPECT_SIZES(segm,
                6+4+2+6+4, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000 "
                "000c "
                "0080 0400 0900 "
                "0106 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Add inline: 2 for the header and +4 of the data (6 in total)
        segm.set_inline_data({char(0xaa), char(0xbb), char(0xcc), char(0xdd)}); // 4 bytes of inline data
        XOZ_EXPECT_SIZES(segm,
                6+4+2+6+4+6, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (4),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000 "
                "000c "
                "0080 0400 0900 "
                "0106 0000 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Adding another extent once we added inline data is not
        // allowed (the inline work kind of a closure)
        EXPECT_THAT(
            [&]() { segm.add_extent(Extent(6, 8, false)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Segment with inline data/end of segment cannot be extended."
                        )
                    )
                )
        );

        // Remove the inline data temporally
        auto inline_data_saved = segm.inline_data();
        segm.remove_inline_data();

        // Add an extent that it is near of the previous extent
        // (note how it does matter that the last thing added
        // to the segment was an inline-data, it does not count)
        //
        // The offset is 3 blocks (from blk nr 3 to blk nr 6).
        // The extent is 8 blocks length that fits in a smallcnt
        //
        // Total: 2 bytes
        segm.add_extent(Extent(6, 8, false)); // 8 full blocks
        segm.set_inline_data(inline_data_saved); // restore
        XOZ_EXPECT_SIZES(segm,
                6+4+2+6+4+6+2, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (4) +
                (8 << blk_sz_order),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (8 << blk_sz_order)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000 "
                "000c "
                "0080 0400 0900 "
                "0106 0000 "
                "0344 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm);
    }

    TEST(SegmentTest, ExtentAtZeroThenNear) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // The reference "prev" extent is Extent(0, 0, false) and the
        // first extent of the segment is exactly at blk nr 0.
        // So it is (really) near at a distance of 0 blks.
        //
        // The extent has a blk count
        // that does not fit in smallcnt (+2 bytes) so raising a total
        // of 4 bytes
        //
        // [ 00       10        ] addr
        // [ XX...XX            ] blks
        segm.add_extent(Extent(0x00, 16, false)); // 16 blocks
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                16 << blk_sz_order,   /* allocated size */
                16 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append a extent near to the prev extent (blk_nr 0x00).
        // It is immediately after the prev extent so the offset is 0
        // The extent is for suballoc so it requires the bitmask (+2 bytes)
        // despite alloc'ing 0 subblocks
        //
        // [ 00      10        ] addr
        // [ XX...XX|Y         ] blks
        segm.add_extent(Extent(0x10, 0, true));    // 0 subblocks
        XOZ_EXPECT_SIZES(segm,
                4+4, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 1000 "
                "0084 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);
    }

    TEST(SegmentTest, ExtentAtZeroThenNonNear) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // The reference "prev" extent is Extent(0, 0, false) and the
        // first extent of the segment is exactly at blk nr 0.
        // So it is (really) near at a distance of 0 blks.
        //
        // The extent has a blk count
        // that does not fit in smallcnt (+2 bytes) so raising a total
        // of 4 bytes
        //
        // [ 00       10        ] addr
        // [ XX...XX            ] blks
        segm.add_extent(Extent(0x00, 16, false)); // 16 blocks
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                16 << blk_sz_order,   /* allocated size */
                16 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append a extent non-near to the prev extent (blk_nr 0x00).
        // It is far (+2 bytes) and it is for suballoc so it requires the bitmask (+2 bytes)
        // despite alloc'ing 0 subblocks
        //
        // [ 00                 e10        ] addr
        // [ XX...XX             Y         ] blks
        segm.add_extent(Extent(0xe10, 0, true));    // 0 subblocks
        XOZ_EXPECT_SIZES(segm,
                4+6, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 1000 "
                "0080 100e 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);
    }

    TEST(SegmentTest, ExtentThenNearAtZero) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // The extent at 0x01 is near of the reference at 0x00 (jump of 1 blk)
        // Then it requires +2 bytes to encode the non-small blk cnt
        // [ 00  01       11        ] addr
        // [     XX...XX            ] blks
        segm.add_extent(Extent(0x01, 16, false)); // 16 blocks
        XOZ_EXPECT_SIZES(segm,
                4, /* disc size */
                16 << blk_sz_order,   /* allocated size */
                16 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0104 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append a extent near to the prev extent (blk_nr 0x01).
        // It is immediately before the prev extent so the offset is 0
        // of 1 blk length backwards
        // The extent is for suballoc so it requires the bitmask (+2 bytes)
        // despite alloc'ing 0 subblocks
        //
        // [ 00  01       11        ] addr
        // [ Y   XX...XX            ] blks
        segm.add_extent(Extent(0x00, 0, true));    // 0 subblocks
        XOZ_EXPECT_SIZES(segm,
                4+4, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0104 1000 "
                "0086 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);
    }

    TEST(SegmentTest, ExtentThenNonNearAtZero) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);

        // The extent at 0xe00 is non-near of the reference at 0x00 (+2 bytes)
        // Then it requires +2 bytes to encode the non-small blk cnt
        // [                e00      e10        ] addr
        // [                 XX...XX            ] blks
        segm.add_extent(Extent(0xe00, 16, false)); // 16 blocks
        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                16 << blk_sz_order,   /* allocated size */
                16 << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Append a extent non-near to the prev extent (blk_nr 0xe00) so +2 bytes
        // The extent is for suballoc so it requires the bitmask (+2 bytes)
        // despite alloc'ing 0 subblocks
        //
        // [ 00             e00      e10        ] addr
        // [ Y               XX...XX            ] blks
        segm.add_extent(Extent(0x00, 0, true));    // 0 subblocks
        XOZ_EXPECT_SIZES(segm,
                6+6, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0080 0000 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);
    }

    TEST(SegmentTest, ExtendWithAnotherSegmentAndInline) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        Segment segm(blk_sz_order);
        Segment src(blk_sz_order);

        // Extent not-near the prev extent (+2 bytes) with a blk count
        // that does not fit in smallcnt (+2 bytes) so raising a total
        // of 6 bytes
        //
        // [                e00      e10        ] addr
        // [                 XX...XX            ] blks
        src.add_extent(Extent(0xe00, 16, false)); // 16 blocks
        // Append a extent near to the prev extent (blk_nr 0xe00).
        // It is immediately after the prev extent so the offset is 0
        // The extent is for suballoc so it requires the bitmask (+2 bytes)
        // despite alloc'ing 0 subblocks
        //
        // [                e00     e10        ] addr
        // [                 XX...XX|Y         ] blks
        src.add_extent(Extent(0xe10, 0, true));    // 0 subblocks
        // Append an extent near the prev extent (blk_nr 0xe10) which was
        // 1 block length (for suballocation). It is immediately after the
        // previous extent (offset = 0)
        // The current extent has also 1 block so it fits in a smallcnt
        // with a total of 2 bytes only
        //
        // [                e00    e10 e11        ] addr
        // [                 XX...XX|Y|Z|         ] blks
        src.add_extent(Extent(0xe11, 1, false)); // 1 block count, fits in smallcnt
        // Append an extent far from prev extent. This extent
        // is 1 block length for suballocation (with 2 subblocks set)
        // This gives a total of 6 bytes
        //
        // [     4           e00    e10 e11        ] addr
        // [     X           XX...XX|Y|Z|         ] blks
        src.add_extent(Extent(4, 0b00001001, true));    // 2 subblocks
        // Append another extent, this has 0 block length
        // (smallcnt cannot be used so, +2)
        // and it is near the previous *but* backwards
        //
        // It is 1 block behind the previous extent: this is because
        // the current extent is 0-blocks length so between blk nr 3
        // and blk nr 4 there are 1 block "of gap" between the two extents
        //
        // [    34          e00    e10 e11        ] addr
        // [    0X           XX...XX|Y|Z|         ] blks
        src.add_extent(Extent(3, 0, false)); // 0 full block (large extent)
        // Add inline: 2 for the header and +4 of the data (6 in total)
        src.set_inline_data({char(0xaa), char(0xbb), char(0xcc), char(0xdd)}); // 4 bytes of inline data

        // Add all the extents and inline from the source segment
        segm.extend(src);
        XOZ_EXPECT_SIZES(segm,
                6+4+2+6+4+6, /* disc size */
                /* allocated size */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0) +
                (4),
                /* allocated size without inline */
                (16 << blk_sz_order) +
                (0) +
                (1 << blk_sz_order) +
                (2 << (blk_sz_order - 4)) +
                (0)
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0000 000e 1000 "
                "0084 0000 "
                "000c "
                "0080 0400 0900 "
                "0106 0000 "
                "00c4 aabb ccdd"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
        XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        XOZ_EXPECT_DESERIALIZATION_INLINE_ENDED(fp, blk_sz_order, segm);
        XOZ_RESET_FP(fp, FP_SZ);

        // Adding another extent once we added inline data is not
        // allowed (the inline work kind of a closure)
        EXPECT_THAT(
            [&]() { segm.add_extent(Extent(6, 8, false)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Segment with inline data/end of segment cannot be extended."
                        )
                    )
                )
        );

        // The same goes extending from another segment
        EXPECT_THAT(
            [&]() { segm.extend(src); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Segment with inline data/end of segment cannot be extended."
                        )
                    )
                )
        );
    }

    TEST(SegmentTest, FileOverflowNotEnoughRoom) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ / 2); // half file size, easier to test TODO test FP_SZ only
        Segment segm(blk_sz_order);

        // Large but perfectly valid inline data
        segm.set_inline_data(std::vector<char>(FP_SZ / 2));
        std::iota (std::begin(segm.inline_data()), std::end(segm.inline_data()), 0); // fill with numbers

        XOZ_EXPECT_SIZES(segm,
                34, /* disc size */
                32, /* allocated size */
                0
                );

        // The write however exceeds the file size
        EXPECT_THAT(
            [&]() { segm.write_struct_into(IOSpan(fp), &checksum); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 34 bytes but only 32 bytes are available. "
                        "Write segment structure into buffer failed."
                        )
                    )
                )
        );

        // Nothing was written
        EXPECT_EQ(are_all_zeros(fp), (bool)true);
        EXPECT_EQ(checksum, (uint32_t)0); // no checksum added

        XOZ_RESET_FP(fp, FP_SZ / 2);
        segm.remove_inline_data();


        // Very long but perfectly valid segment of 6 suballoc blocks
        for (int i = 0; i < 6; ++i) {
            // each extent should have a footprint of 6 bytes
            segm.add_extent(Extent(0x2ff + (0x2ff * i), 0xffff, true));
        }
        XOZ_EXPECT_SIZES(segm,
                36, /* 6 extents times 6 bytes each -- disc size */
                6 << blk_sz_order, /* allocated size */
                6 << blk_sz_order
                );

        // The write however exceeds the file size
        EXPECT_THAT(
            [&]() { segm.write_struct_into(IOSpan(fp), &checksum); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 36 bytes but only 32 bytes are available. "
                        "Write segment structure into buffer failed."
                        )
                    )
                )
        );

        // Nothing was written
        EXPECT_EQ(are_all_zeros(fp), (bool)true);
        EXPECT_EQ(checksum, (uint32_t)0); // no checksum added
    }

    TEST(SegmentTest, PartialReadError) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);

        // Write a 6-bytes single-extent segment
        Segment segm(blk_sz_order);
        segm.add_extent(Extent(0x2ff, 0x1f, false)); // size: 6 bytes

        XOZ_EXPECT_SIZES(segm,
                6, /* disc size */
                0x1f << blk_sz_order, /* allocated size */
                0x1f << blk_sz_order
                );

        checksum = 0;
        segm.write_struct_into(IOSpan(fp));

        // Try to read only 2 bytes: this should fail
        // because Segment::load_struct_from will know that
        // more bytes are needed to complete the extent
        fp.resize(2);
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 0 bytes are available. "
                        "The read operation set an initial size of 2 bytes "
                        "but they were consumed leaving only 0 bytes available. "
                        "This is not enough to proceed reading "
                        "(segment reading is incomplete: "
                        "cannot read LSB block number"
                        ")."
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);

        XOZ_RESET_FP(fp, FP_SZ);
        checksum = 0;
        segm.write_struct_into(IOSpan(fp));

        // The same but with 4 bytes
        fp.resize(4);
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 0 bytes are available. "
                        "The read operation set an initial size of 4 bytes "
                        "but they were consumed leaving only 0 bytes available. "
                        "This is not enough to proceed reading "
                        "(segment reading is incomplete: "
                        "cannot read block count"
                        ")."
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);

        // Let's add an another 4-bytes extent
        segm.add_extent(Extent(0x5ff, 1, false)); // size: 10 bytes

        XOZ_EXPECT_SIZES(segm,
                6 + 4, /* disc size */

                /* allocated size */
                (0x1f << blk_sz_order) +
                (1 << blk_sz_order),
                /* allocated size without inline */
                (0x1f << blk_sz_order) +
                (1 << blk_sz_order)
                );

        XOZ_RESET_FP(fp, FP_SZ);
        checksum = 0;
        segm.write_struct_into(IOSpan(fp));

        fp.resize(8);
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 2 bytes but only 0 bytes are available. "
                        "The read operation set an initial size of 8 bytes "
                        "but they were consumed leaving only 0 bytes available. "
                        "This is not enough to proceed reading "
                        "(segment reading is incomplete: "
                        "cannot read LSB block number"
                        ")."
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);

        // Let's add inline of 4 bytes (+2 header)
        segm.set_inline_data({char(0xaa), char(0xbb), char(0xcc), char(0xdd)}); // size: 16 bytes

        XOZ_EXPECT_SIZES(segm,
                6 + 4 + 6, /* disc size */

                /* allocated size */
                (0x1f << blk_sz_order) +
                (1 << blk_sz_order) +
                (4),
                /* allocated size without inline */
                (0x1f << blk_sz_order) +
                (1 << blk_sz_order)
                );

        XOZ_RESET_FP(fp, FP_SZ);
        checksum = 0;
        segm.write_struct_into(IOSpan(fp));

        // Segment::load_struct_from will read the inline header and it will
        // try to read 4 bytes *but* no available bytes exists
        fp.resize(12);
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 4 bytes but only 0 bytes are available. "
                        "The read operation set an initial size of 12 bytes "
                        "but they were consumed leaving only 0 bytes available. "
                        "This is not enough to proceed reading "
                        "(segment reading is incomplete: "
                        "inline data is partially read"
                        ")."
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);


        XOZ_RESET_FP(fp, FP_SZ);
        checksum = 0;
        segm.write_struct_into(IOSpan(fp));

        // The same but only 2 bytes are available, not enough for
        // completing the 4 bytes inline payload
        fp.resize(14);
        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr(
                        "Requested 4 bytes but only 2 bytes are available. "
                        "The read operation set an initial size of 14 bytes "
                        "but they were consumed leaving only 2 bytes available. "
                        "This is not enough to proceed reading "
                        "(segment reading is incomplete: "
                        "inline data is partially read"
                        ")."
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);
    }

    TEST(SegmentTest, CorruptedData) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);
        uint32_t checksum = 0;

        // Because is_suballoc is set and smallcnt > 0, it is expected
        // that the is_inline bi set bit it is not, hence the error
        fp = {'\x00', '\x90', '\x01', '\x00'};

        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Repository seems inconsistent/corrupt. "
                        "Extent with non-zero smallcnt block. Is inline flag missing?"
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);

        XOZ_RESET_FP(fp, FP_SZ);

        fp = {'\x01', '\x24', '\x01', '\x26'};

        EXPECT_THAT(
            ensure_called_once([&]() { Segment::load_struct_from(IOSpan(fp), blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum); }),
            ThrowsMessage<InconsistentXOZ>(
                AllOf(
                    HasSubstr(
                        "Repository seems inconsistent/corrupt. "
                        "Near extent block number wraparound: "
                        "current extent offset 1 and blk cnt 4 "
                        "in the backward direction and "
                        "previous extent at blk nr 1 and blk cnt 4."
                        )
                    )
                )
        );
        EXPECT_EQ(checksum, (uint32_t)0);
    }

    TEST(SegmentTest, InlineEnd) {
        const uint8_t blk_sz_order = 10;
        Segment segm(blk_sz_order);


        // Segment empty with a single extent as inline marking the end of the segment.
        segm.add_end_of_segment();

        // Write it into a larger buffer
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);

        uint32_t segm_sz = segm.calc_struct_footprint_size();
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // There is no problem in loading the segment: the laoding will stop immediately
        // after the loading of the inline data.
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, FP_SZ);
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::InlineEnd, -1, &checksum2);
            segm2.write_struct_into(IOSpan(buf2), &checksum);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);

            // What was read is exactly what it was required (the segment size in disc),
            // no more, no less.
            EXPECT_EQ(FP_SZ - io.remain_rd(), segm_sz);
            EXPECT_NE(io.remain_rd(), uint32_t(0));
        }

        // Shrink the fp buffer to segm_sz such the segment is still correctly
        // encoded including the inline but no more bytes follows (it fits perfectly)
        fp.resize(segm_sz);

        // Test everything again: we expect the same results. Reaching the end of the IO
        // during the loading is not a problem because we should had found the inline before
        // and that should had stop the parsing.
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;
            checksum = 0;


            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::InlineEnd, -1, &checksum2);
            segm2.write_struct_into(IOSpan(buf2), &checksum);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);
            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
        }


        // Let's try everthing again but know with a segment with 2 extents: 1 normal
        // and the other inline
        XOZ_RESET_FP(fp, FP_SZ);

        segm.remove_inline_data();
        segm.add_extent(Extent(0, 0, false));
        segm.add_end_of_segment();

        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);

        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 0000 00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        segm_sz = segm.calc_struct_footprint_size();

        // Test with a buffer larger than the segment size
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, FP_SZ);
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::InlineEnd, -1, &checksum2);
            segm2.write_struct_into(IOSpan(buf2), &checksum);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);
            // no more, no less.
            EXPECT_EQ(FP_SZ - io.remain_rd(), segm_sz);
            EXPECT_NE(io.remain_rd(), uint32_t(0));
        }

        // Shrink to fit
        fp.resize(segm_sz);

        // Test with a segment that fits perfectly the buffer
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::InlineEnd, -1, &checksum2);
            segm2.write_struct_into(IOSpan(buf2), &checksum);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);
            EXPECT_EQ(checksum, checksum2);

            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);
            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
        }


        // Reset everything and test what happen if we expect InlineEnd but the segment
        // does not end with an inline extent (the segment ends due an io end).
        // We expect a failure
        XOZ_RESET_FP(fp, FP_SZ);

        // Same but without inline
        segm.remove_inline_data();
        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);

        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        segm_sz = segm.calc_struct_footprint_size();

        // Shrink to fit so the segment could be theoretically parsed without trouble
        // but because we are expecting an inline it should fail
        fp.resize(segm_sz);

        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::InlineEnd, -1, &checksum2); }),
                ThrowsMessage<InconsistentXOZ>(
                    AllOf(
                        HasSubstr(
                                 "Expected to read a segment that ends "
                                 "in an inline-extent but such was not found and "
                                 "the segment got a length of 1."
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
            EXPECT_EQ(checksum2, uint32_t(0));
        }

        // Using an explicit segment length is invalid with InlineEnd mode
        {
            std::vector<char> buf2;
            uint32_t checksum2 = 0;
            XOZ_RESET_FP(buf2, fp.size());

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::InlineEnd, 1, &checksum2); }),
                ThrowsMessage<std::runtime_error>(
                    AllOf(
                        HasSubstr(
                                 "Explicit segment length not allowed"
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(4)); // nothing was read, invalid arg detected earlier
            EXPECT_EQ(checksum2, uint32_t(0));
        }
    }

    TEST(SegmentTest, IOEnd) {
        const uint8_t blk_sz_order = 10;
        Segment segm(blk_sz_order);

        // Segment empty with a single extent as inline marking the end of the segment.
        segm.add_end_of_segment();

        // Write it into a buffer that fits perfectly
        std::vector<char> fp;
        uint32_t checksum = 0;

        XOZ_RESET_FP(fp, FP_SZ);
        segm.write_struct_into(IOSpan(fp), &checksum);

        uint32_t segm_sz = segm.calc_struct_footprint_size();
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Shrink the fp buffer to segm_sz such the segment is still correctly
        // encoded including the inline but no more bytes follows (it fits perfectly)
        fp.resize(segm_sz);

        // There is no problem in loading the segment: the loading will stop at the end of
        // the IO (having an inline data does not affect this)
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::IOEnd, -1, &checksum);
            segm2.write_struct_into(IOSpan(buf2), &checksum2);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum2);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);

            // What was read is exactly what it was required (the segment size in disc),
            // no more, no less.
            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
        }

        // Expand the buffer
        fp.resize(segm_sz + 2);

        // Now we expect an error: while the segment has an inline that mark the end
        // of the segment, if we are using IOEnd we are expecting to find the end of the io
        // not an premature end due an inline data
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::IOEnd, -1, &checksum2); }),
                ThrowsMessage<InconsistentXOZ>(
                    AllOf(
                        HasSubstr(
                            "Expected to read a segment that ends "
                            "at the end of the io object but an inline-extent was found before that, "
                            "obtaining a segment with a length of 1 and "
                            "in the io still remains 2 bytes."
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(2));
            EXPECT_EQ(checksum2, uint32_t(0));
        }

        // Shrink back
        fp.resize(segm_sz);

        // Now the problem is that IOEnd is incompatible with an explicit segment length
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::IOEnd, 1, &checksum2); }),
                ThrowsMessage<std::runtime_error>(
                    AllOf(
                        HasSubstr(
                                 "Explicit segment length not allowed"
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(2)); // nothing was read, invalid arg detected earlier
            EXPECT_EQ(checksum2, uint32_t(0));
        }
    }

    TEST(SegmentTest, AnyEnd) {
        const uint8_t blk_sz_order = 10;
        Segment segm(blk_sz_order);

        // Segment empty with a single extent as inline marking the end of the segment.
        segm.add_end_of_segment();

        // Write it into a buffer that fits perfectly
        std::vector<char> fp;
        uint32_t checksum = 0;

        XOZ_RESET_FP(fp, FP_SZ);
        segm.write_struct_into(IOSpan(fp), &checksum);

        uint32_t segm_sz = segm.calc_struct_footprint_size();
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Shrink the fp buffer to segm_sz such the segment is still correctly
        // encoded including the inline but no more bytes follows (it fits perfectly)
        fp.resize(segm_sz);

        // There is no problem in loading the segment: the loading will stop at the end of
        // the IO (having an inline data does not affect this)
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum);
            segm2.write_struct_into(IOSpan(buf2), &checksum2);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            // What was read is exactly what it was required (the segment size in disc),
            // no more, no less.
            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
        }

        // Expand the buffer
        fp.resize(segm_sz + 2);

        // No error either: the AnyEnd will stop the loading either at the end of the io
        // or after finding an inline-extent (aka end of the segment).
        // In this case the latter is happening
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum);
            segm2.write_struct_into(IOSpan(buf2), &checksum2);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(2));
        }

        XOZ_RESET_FP(fp, FP_SZ);

        // Now let's use a segment without inline
        checksum = 0;
        segm.remove_inline_data();
        segm.add_extent(Extent(0, 0, false));
        segm.write_struct_into(IOSpan(fp), &checksum);

        segm_sz = segm.calc_struct_footprint_size();
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Shrink back so it fits perfectly
        fp.resize(segm_sz);

        // No error either: the AnyEnd will stop the loading either at the end of the io
        // or after finding an inline-extent (aka end of the segment).
        // In this case the former is happening
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            uint32_t checksum2 = 0;
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::AnyEnd, -1, &checksum);
            segm2.write_struct_into(IOSpan(buf2), &checksum2);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            EXPECT_EQ(checksum, checksum2);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
        }


        // Now the problem is that AnyEnd is incompatible with an explicit segment length
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());

            checksum = 0;
            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::AnyEnd, 1, &checksum); }),
                ThrowsMessage<std::runtime_error>(
                    AllOf(
                        HasSubstr(
                                 "Explicit segment length not allowed"
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(4)); // nothing was read, invalid arg detected earlier
            EXPECT_EQ(checksum, uint32_t(0));
        }
    }


    TEST(SegmentTest, ExplicitLen) {
        const uint8_t blk_sz_order = 10;
        Segment segm(blk_sz_order);

        // Segment empty with a single extent as inline marking the end of the segment.
        segm.add_end_of_segment();

        // Write it into a buffer that fits perfectly
        std::vector<char> fp;
        uint32_t checksum = 0;
        XOZ_RESET_FP(fp, FP_SZ);
        checksum = 0;
        segm.write_struct_into(IOSpan(fp), &checksum);

        uint32_t segm_sz = segm.calc_struct_footprint_size();
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "00c0"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Shrink the fp buffer to segm_sz plus 2 bytes: the segment should be loaded
        // correctly thanks its inline data
        fp.resize(segm_sz + 2);

        // Problem: we explicitly require a segment of 2 (ours has length of 1) and we
        // found an inline data so this is wrong
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            checksum = 0;

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::ExplicitLen, 2, &checksum); }),
                ThrowsMessage<InconsistentXOZ>(
                    AllOf(
                        HasSubstr(
                                "Expected to read a segment that of length 2 but "
                                "an inline-extent"
                                " was found before and "
                                "made the segment shorter of length 1."
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(2));
            EXPECT_EQ(checksum, uint32_t(0));
        }


        // Good: we require a segment of 1 and we have one so everything is ok.
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::ExplicitLen, 1);
            segm2.write_struct_into(IOSpan(buf2), &checksum);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            // What was read is exactly what it was required (the segment size in disc),
            // no more, no less.
            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(2));
        }


        XOZ_RESET_FP(fp, FP_SZ);

        // Now let's use a segment without inline
        checksum = 0;
        segm.remove_inline_data();
        segm.add_extent(Extent(0, 0, false));
        segm.write_struct_into(IOSpan(fp), &checksum);

        // Fit perfectly
        segm_sz = segm.calc_struct_footprint_size();
        fp.resize(segm_sz);
        XOZ_EXPECT_SERIALIZATION(fp, segm,
                "0004 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, segm, checksum);

        // Problem: we explicitly require a segment of 2 (ours has length of 1) and we
        // found the end of the io
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            checksum = 0;

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::ExplicitLen, 2, &checksum); }),
                ThrowsMessage<InconsistentXOZ>(
                    AllOf(
                        HasSubstr(
                                "Expected to read a segment that of length 2 but "
                                "the io end"
                                " was found before and "
                                "made the segment shorter of length 1."
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
            EXPECT_EQ(checksum, uint32_t(0));
        }


        // Good: we require a segment of 1 and we have one so everything is ok.
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            checksum = 0;

            IOSpan io(fp);
            Segment segm2 = Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::ExplicitLen, 1);
            segm2.write_struct_into(IOSpan(buf2), &checksum);

            // Same serialization
            EXPECT_EQ(fp, buf2);
            XOZ_EXPECT_CHECKSUM(buf2, segm2, checksum);

            // What was read is exactly what it was required (the segment size in disc),
            // no more, no less.
            EXPECT_EQ(buf2.size() - io.remain_rd(), segm_sz);
            EXPECT_EQ(io.remain_rd(), uint32_t(0));
        }


        // Now the problem a (uint32_t)-1 is understood as no-explicit segment length
        // that it is incompatible with the ExplicitLen mode.
        {
            std::vector<char> buf2;
            XOZ_RESET_FP(buf2, fp.size());
            checksum = 0;

            IOSpan io(fp);
            EXPECT_THAT(
                ensure_called_once([&]() { Segment::load_struct_from(io, blk_sz_order, Segment::EndMode::ExplicitLen, (uint32_t)(-1), &checksum); }),
                ThrowsMessage<std::runtime_error>(
                    AllOf(
                        HasSubstr(
                                 "Explicit segment length required"
                            )
                        )
                    )
                );
            EXPECT_EQ(io.remain_rd(), uint32_t(4)); // nothing was read, invalid arg detected earlier
            EXPECT_EQ(checksum, uint32_t(0));
        }
    }

    TEST(SegmentTest, OverlappingExtentsWithABlkOfZero) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;

        // Case:
        //  - i-th extent has 0 blks and the i+1 has more than 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(1, 0, false));
            segm.add_extent(Extent(1, 1, false));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    6, /* disc size */
                    1024, /* allocated size */
                    1024
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0104 0000 000c"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case (again):
        //  - i-th extent has 0 blks and the i+1 has more than 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(2, 3, false));
            segm.add_extent(Extent(1, 0, false));
            segm.add_extent(Extent(1, 1, false));
            segm.add_extent(Extent(3, 1, false));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    10, /* disc size */
                    5120, /* allocated size */
                    5120
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "021c 0106 0000 000c 010c"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case:
        //  - i-th extent has 0 blks and the i+1 has 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(1, 0, false));
            segm.add_extent(Extent(1, 0, false));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    8, /* disc size */
                    0, /* allocated size */
                    0
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0104 0000 0004 0000"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case:
        //  - i-th extent has 0 blks and the i+1 has 0
        //  - both at block 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(0, 0, false));
            segm.add_extent(Extent(0, 0, false));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    8, /* disc size */
                    0, /* allocated size */
                    0
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0004 0000 0004 0000"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }
    }


    TEST(SegmentTest, OverlappingExtentsSubAlloc) {
        const uint8_t blk_sz_order = 10;
        std::vector<char> fp;
        uint32_t checksum = 0;

        // Case:
        //  - non-overlapping masks
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(1, 0, true));
            segm.add_extent(Extent(1, 1, true));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    10, /* disc size */
                    64, /* allocated size */
                    64
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0184 0000 0080 0100 0100"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case (again):
        //  - non-overlapping masks
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(2, 3, true));
            segm.add_extent(Extent(1, 0, true));
            segm.add_extent(Extent(1, 1, true));
            segm.add_extent(Extent(3, 1, true));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    18, /* disc size */
                    256, /* allocated size */
                    256
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0284 0300 0086 0000 0080 0100 0100 0184 0100"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case:
        //  - non-overlapping masks
        //  - both mask 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(1, 0, true));
            segm.add_extent(Extent(1, 0, true));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    10, /* disc size */
                    0, /* allocated size */
                    0
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0184 0000 0080 0100 0000"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case:
        //  - non-overlapping masks
        //  - both mask non 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(1, 0xf000, true));
            segm.add_extent(Extent(1, 0x000f, true));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    10, /* disc size */
                    512, /* allocated size */
                    512
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0184 00f0 0080 0100 0f00"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case:
        //  - non-overlapping masks, both 0
        //  - both at block 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(0, 0, true));
            segm.add_extent(Extent(0, 0, true));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    10, /* disc size */
                    0, /* allocated size */
                    0
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0084 0000 0080 0000 0000"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }

        // Case:
        //  - non-overlapping masks, both non 0
        //  - both at block 0
        {
            XOZ_RESET_FP(fp, FP_SZ);
            Segment segm(blk_sz_order);

            segm.add_extent(Extent(0, 0xf000, true));
            segm.add_extent(Extent(0, 0x000f, true));

            // Check sizes
            XOZ_EXPECT_SIZES(segm,
                    10, /* disc size */
                    512, /* allocated size */
                    512
                    );

            // Write and check the dump
            checksum = 0;
            segm.write_struct_into(IOSpan(fp), &checksum);
            XOZ_EXPECT_SERIALIZATION(fp, segm,
                    "0084 00f0 0080 0000 0f00"
                    );
            XOZ_EXPECT_CHECKSUM(fp, segm, checksum);
            XOZ_EXPECT_DESERIALIZATION(fp, blk_sz_order, segm);
        }
    }
}
