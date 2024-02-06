#include "xoz/blk/vector_block_array.h"
#include "xoz/blk/segment_block_array.h"
#include "xoz/ext/extent.h"
#include "xoz/err/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::subvec;

// Check that the serialization of the extents in fp are of the
// expected size (call calc_struct_footprint_size) and they match
// byte-by-byte with the expected data (in hexdump)
#define XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

#define XOZ_EXPECT_SIZES(segm, blk_sz_order, disk_sz, allocated_sz) do {                  \
    EXPECT_EQ((segm).calc_struct_footprint_size(), (unsigned)(disk_sz));                    \
    EXPECT_EQ((segm).calc_data_space_size((blk_sz_order)), (unsigned)(allocated_sz));   \
} while (0)

namespace {
    // The base array's blocks of 64 byte and the segment array of 16 bytes
    // makes a 1 to 4 ratio (growing 4 blocks the segment block array grows in
    // 1 block the base array)
    const uint32_t base_blkarr_blk_sz = 64;
    const uint32_t base_blkarr_subblk_sz = 4;
    const uint8_t base_blkarr_blk_sz_order = 6;
    const uint32_t blkarr_blk_sz = 16;

    TEST(SegmentBlockArrayTest6416, OneBlock) {
        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg; // empty segment it will be interpreted as an empty block array below

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);

        // Because sg is empty, the allocator() is empty. Note that if sg is not
        // empty it may not imply that it is fully allocated. Remember, the
        // SegmentBlockArray's allocator manages the chop/split and which pieces
        // are allocated or not is known only by the caller so we must
        // explicit tell the SegmentBlockArray's allocator about.
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000"
                );

        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                ""
                );
    }

    TEST(SegmentBlockArrayTest6416, OneBlockTwice) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F', 'G'};
        std::vector<char> rdbuf;

        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)7);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 4546 4700 0000 0000 0000 0000"
                );

        // override first bytes but leave the rest untouched
        std::vector<char> wrbuf2 = {'D', 'E', 'B'};
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf2), (uint32_t)3);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4445 4244 4546 4700 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 3), (uint32_t)3);
        EXPECT_EQ(wrbuf2, rdbuf);

        // override the expected buffer for comparison
        memcpy(wrbuf.data(), wrbuf2.data(), 3);

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 7), (uint32_t)7);
        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4445 4244 4546 4700 0000 0000 0000 0000"
                );
    }

    TEST(SegmentBlockArrayTest6416, OneBlockCompletely) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf(blkarr_blk_sz);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..blkarr_blk_sz

        std::vector<char> rdbuf;

        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)blkarr_blk_sz);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, blkarr_blk_sz), (uint32_t)blkarr_blk_sz);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Call read_extent again but let read_extent to figure out how many bytes needs to read
        // (the size of the extent in bytes)
        rdbuf.clear();
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf), (uint32_t)blkarr_blk_sz);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f"
                );
    }

    TEST(SegmentBlockArrayTest6416, TwoBlocks) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                2, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf((blkarr_blk_sz + 1)); // blk_sz + 1
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..(blkarr_blk_sz + 1)

        std::vector<char> rdbuf;

        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)(blkarr_blk_sz + 1));
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, (blkarr_blk_sz + 1)), (uint32_t)(blkarr_blk_sz + 1));
        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1000 0000 0000 0000 0000 0000 0000 0000"
                );
    }


    TEST(SegmentBlockArrayTest6416, MaxBlocks) {

        const auto max_blk_cnt = (1 << 16) - 1;
        const auto blk_sz = blkarr_blk_sz;
        const auto last_blk_at = (max_blk_cnt - 1) * blk_sz;

        VectorBlockArray sg_blkarr(blk_sz);

        auto old_top_nr = sg_blkarr.grow_by_blocks(max_blk_cnt);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                max_blk_cnt, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf(max_blk_cnt * blk_sz);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0x00..0xc0

        std::vector<char> rdbuf;

        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)(max_blk_cnt * blk_sz));
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf), (uint32_t)(max_blk_cnt * blk_sz));
        EXPECT_EQ(wrbuf, rdbuf);

        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, blkarr_blk_sz,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f"
                );
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, last_blk_at, -1,
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef"
                );

        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, last_blk_at, -1, ""); // the block was removed
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, last_blk_at - blk_sz, -1,
                "d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf"
                ); // no more than 1 block proving that the array shrank by 1 block

        sg_blkarr.release_blocks();
    }


    TEST(SegmentBlockArrayTest6416, ZeroBlocks) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // Nothing is written (explicit max_data_sz)
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        wrbuf.resize(blkarr_blk_sz);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..blkarr_blk_sz

        // neither this (implicit max_data_sz)
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // And nothing is read (explicit max_data_sz)
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        // neither is read in this way (implicit max_data_sz)
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        sg_blkarr.release_blocks();

        // Because we never written anything to the block 1, the "old trailer"
        // is still there (as garbage data)
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }


    TEST(SegmentBlockArrayTest6416, ExtentOutOfBoundsSoFail) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);

        std::vector<char> wrbuf(blkarr_blk_sz);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..(blkarr_blk_sz + 1)
        std::vector<char> rdbuf;

        Extent extOK(
                0, // blk_nr (ok)
                1, // blk_cnt (ok)
                false // is_suballoc
                );

        // write something in the block so we can detect if an invalid write
        // or invalid read take place later when we use "out of bounds" extents
        sg_blkarr.write_extent(extOK, wrbuf);

        // Try to write something obviously different: we shouldn't!
        wrbuf = {'A', 'B', 'C'};

        Extent extOOBCompl(
                2, // blk_nr (out of bounds, the sg_blkarr has only 1 block)
                1, // blk_cnt
                false // is_suballoc
                );


        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { sg_blkarr.write_extent(extOOBCompl, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 1 blocks "
                              "that starts at block 2 and ends at block 2 "
                              "completely falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { sg_blkarr.read_extent(extOOBCompl, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 1 blocks "
                              "that starts at block 2 and ends at block 2 "
                              "completely falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a read operation.")
                    )
                )
        );

        // On an out of bounds read, it is not specified the value of
        // the read buffer. The may be empty and fill of zeros. Check both.
        if (rdbuf.size() == 0) {
            EXPECT_EQ(std::vector<char>(), rdbuf);
        } else {
            // extent 1 block long: blkarr_blk_sz bytes
            EXPECT_EQ((unsigned)blkarr_blk_sz, rdbuf.size());
            EXPECT_EQ(std::vector<char>(blkarr_blk_sz), rdbuf);
        }
        rdbuf.clear();

        Extent extOOBZero(
                2, // blk_nr (out of bounds, the sg_blkarr has only 1 block)
                0, // blk_cnt (empty extent but still out of bounds)
                false // is_suballoc
                );


        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { sg_blkarr.write_extent(extOOBZero, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 0 blocks (empty) "
                              "at block 2 "
                              "completely falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { sg_blkarr.read_extent(extOOBZero, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 0 blocks (empty) "
                              "at block 2 "
                              "completely falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a read operation.")
                    )
                )
        );

        // On an out of bounds read, it is not specified the value of
        // the read buffer. However in this case we expect to have a 0 size.
        EXPECT_EQ(std::vector<char>(), rdbuf);
        rdbuf.clear();

        Extent extOOBPart(
                0, // blk_nr (ok, within the bounds but...)
                2, // blk_cnt (bad!, the extent spans beyond the end)
                false // is_suballoc
                );

        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { sg_blkarr.write_extent(extOOBPart, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 2 blocks "
                              "that starts at block 0 and ends at block 1 "
                              "partially falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { sg_blkarr.read_extent(extOOBPart, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 2 blocks "
                              "that starts at block 0 and ends at block 1 "
                              "partially falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a read operation.")
                    )
                )
        );

        // On an out of bounds read, it is not specified the value of
        // the read buffer. The may be empty and fill of zeros. Check both.
        if (rdbuf.size() == 0) {
            EXPECT_EQ(std::vector<char>(), rdbuf);
        } else {
            // extent 2 blocks long: blkarr_blk_sz * 2 = (blkarr_blk_sz * 2) bytes
            EXPECT_EQ((unsigned)(blkarr_blk_sz * 2), rdbuf.size());
            EXPECT_EQ(std::vector<char>((blkarr_blk_sz * 2)), rdbuf);
        }
        rdbuf.clear();

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f"
                );
    }

    TEST(SegmentBlockArrayTest6416, OneBlockButWriteLessBytes) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F'};
        std::vector<char> rdbuf;

        // The buffer is 6 bytes long but we instruct write_extent()
        // to write only 4
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4), (uint32_t)4);
        EXPECT_EQ(subvec(wrbuf, 0, 4), rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000"
                );
    }


    TEST(SegmentBlockArrayTest6416, OneBlockButWriteAtOffset) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // Write but by an offset of 1
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, 1), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0041 4243 4400 0000 0000 0000 0000 0000"
                );

        // Read 6 bytes from offset 0 so we can capture what the write_extent
        // wrote
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 6), (uint32_t)6);
        EXPECT_EQ(wrbuf, subvec(rdbuf, 1, -1));

        // Write release_blocks to the end of the block
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, blkarr_blk_sz-4), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0041 4243 4400 0000 0000 0000 4142 4344"
                );

        // Read 4 bytes release_blocks at the end of the block
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4, blkarr_blk_sz-4), (uint32_t)4);
        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0041 4243 4400 0000 0000 0000 4142 4344"
                );
    }

    TEST(SegmentBlockArrayTest6416, OneBlockBoundary) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Alloc 2 blocks but we will create an extent of 1.
        // The idea is to have room *after* the extent to detect
        // writes/reads out of bounds
        auto old_top_nr = sg_blkarr.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf = {'.'};

        // Write at a start offset *past* the end of the extent:
        // nothing should be written
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, (blkarr_blk_sz + 1)), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Try now write past the end of the file
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, 1024), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // Write at a start offset *before* the end of the extent
        // *but* with a length the would go *past* the end of the extent:
        // only the bytes that fall in the extent should be written
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, (blkarr_blk_sz - 2)), (uint32_t)2);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 4142 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Read at a start offset *past* the end of the extent:
        // nothing should be read
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4, (blkarr_blk_sz + 1)), (uint32_t)0);
        EXPECT_EQ(rdbuf.size(), (unsigned)0);
        rdbuf = {'.'};

        // Try now read past the end of the file
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4, 1024), (uint32_t)0);
        EXPECT_EQ(rdbuf.size(), (unsigned)0);
        rdbuf = {'.'};

        // Read at a start offset *before* the end of the extent
        // *but* with a length the would go *past* the end of the extent:
        // only the bytes that fall in the extent should be read
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4, (blkarr_blk_sz - 2)), (uint32_t)2);
        EXPECT_EQ(subvec(wrbuf, 0, 2), rdbuf);

        wrbuf.resize((blkarr_blk_sz * 2));
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..(blkarr_blk_sz * 2)

        // Try again write and overflow, with start at 0 but a length too large
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, (blkarr_blk_sz * 2), 0), (uint32_t)blkarr_blk_sz);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, (blkarr_blk_sz * 2), 0), (uint32_t)blkarr_blk_sz);
        EXPECT_EQ(subvec(wrbuf, 0, blkarr_blk_sz), rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(SegmentBlockArrayTest6416, ShrinkByDeallocExtents) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Pre-grow the base block array. This simplifies the reasoning of when
        // an extent is added or not in the segment on calling sg_blkarr.grow_by_blocks
        auto tmp = base_blkarr.allocator().alloc(16 * base_blkarr_blk_sz); // large enough
        base_blkarr.allocator().dealloc(tmp); // TODO should grow_by_blocks add them to the fr alloc?

        Segment sg;

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Grow once
        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                4, // 1 extent (suballoc)
                base_blkarr_subblk_sz * 4 // allocated space (measured in base array blk size)
                );

        // Because growing 1 blk makes grow the underlying array grow by 1/4 of a blk,
        // we expect a new suballoc extent in the segment of length 4 subblks.
        EXPECT_EQ(sg.ext_cnt(), (uint32_t)1);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)4);

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)1);

        // Grow again, this will add more extents to the segment
        old_top_nr = sg_blkarr.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)1);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 8)
                );

        // Because growing 2 blks makes grow the underlying array grow by 2/4 of a blk,
        // we expect a new suballoc extent in the segment of length 8 subblks.
        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)8);

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        // Now shrink by 1 blk that implies dealloc of 4 subblks. Because the last extent has 8 subblks,
        // no real shrink will happen.
        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 8)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)8);

        // Grow by 1 and shrink by 1. See how the grow does not change the segment
        // because it will use the pending-to-remove blk from the step above
        old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)2);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 8)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)8);

        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 8)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)8);

        // Now shrink by 1 blk again. This plus the 1 blk shrunk before are enough
        // to release the last extent.
        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                4, // 1 extent
                base_blkarr_subblk_sz * 4
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)1);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)1);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)4);

        // Grow again, this will add more extents to the segment
        old_top_nr = sg_blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent (both for suballoc)
                base_blkarr_subblk_sz * (4 + 12)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)4);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)12);

        // Now shrink by 2 blk. Because the last extent has 3 blks, no real shrink
        // will happen.
        sg_blkarr.shrink_by_blocks(2);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 12)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)12);

        // Now we release_blocks: if the last extent were for non-suballoc, we could do a split
        // but because it is for suballoc, no split is allowed.
        // In other words, the release_blocks will have zero effect.
        sg_blkarr.release_blocks();
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 12)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)12);

        // Grow know by 3 blocks. Notice how this uses the 2 blks "pending"
        // and add another extent to the segment with 1 blk.
        old_top_nr = sg_blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)2);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                12, // 3 extent
                base_blkarr_subblk_sz * (4 + 12 + 4)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)5);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)5);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)3);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)4);


        // Now shrink by 2 blks. Because the last extent has 1 blk and the next last extent
        // has more than 1 blk, this shrink will remove the last extent and it will leave some pending
        // blks.
        sg_blkarr.shrink_by_blocks(2);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 12)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)12);

        // There is nothing else to release so no change is expected
        sg_blkarr.release_blocks();
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                8, // 2 extent
                base_blkarr_subblk_sz * (4 + 12)
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), true);
        EXPECT_EQ(sg.exts().back().subblk_cnt(), (uint32_t)12);


        // Shrink further, leave the array/segment empty
        sg_blkarr.shrink_by_blocks(3);
        XOZ_EXPECT_SIZES(sg, base_blkarr_blk_sz_order,
                0, // 0 extent
                0
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)0);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)0);
    }

}

