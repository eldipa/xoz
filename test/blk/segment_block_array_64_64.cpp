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

#define XOZ_EXPECT_SIZES(segm, disk_sz, allocated_sz) do {                  \
    EXPECT_EQ((segm).calc_struct_footprint_size(), (unsigned)(disk_sz));                    \
    EXPECT_EQ((segm).calc_data_space_size(), (unsigned)(allocated_sz));   \
} while (0)

namespace {
    // The base array's blocks of blkarr_blk_sz byte and the segment array of blkarr_blk_sz bytes too
    // makes a 1 to 1 ratio (growing 1 block the segment block array grows in
    // 1 block the base array)
    const uint32_t base_blkarr_blk_sz = 64;
    const uint8_t base_blkarr_blk_sz_order = 6;
    const uint32_t blkarr_blk_sz = 64;

    TEST(SegmentBlockArrayTest6464, OneBlock) {
        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order); // empty segment it will be interpreted as an empty block array below

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
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                ""
                );
    }

    TEST(SegmentBlockArrayTest6464, OneBlockTwice) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "4142 4344 4546 4700 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // override first bytes but leave the rest untouched
        std::vector<char> wrbuf2 = {'D', 'E', 'B'};
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf2), (uint32_t)3);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4445 4244 4546 4700 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 3), (uint32_t)3);
        EXPECT_EQ(wrbuf2, rdbuf);

        // override the expected buffer for comparison
        memcpy(wrbuf.data(), wrbuf2.data(), 3);

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 7), (uint32_t)7);
        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4445 4244 4546 4700 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(SegmentBlockArrayTest6464, OneBlockCompletely) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, blkarr_blk_sz), (uint32_t)blkarr_blk_sz);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Call read_extent again but let read_extent to figure out how many bytes needs to read
        // (the size of the extent in bytes)
        rdbuf.clear();
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf), (uint32_t)blkarr_blk_sz);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
    }

    TEST(SegmentBlockArrayTest6464, TwoBlocks) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "4000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, (blkarr_blk_sz + 1)), (uint32_t)(blkarr_blk_sz + 1));
        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "4000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }


    TEST(SegmentBlockArrayTest6464, MaxBlocks) {

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
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, last_blk_at, -1,
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf"
                );

        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, last_blk_at, -1, ""); // the block was removed
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, last_blk_at - blk_sz, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f"
                ); // no more than 1 block proving that the array shrank by 1 block

        sg_blkarr.release_blocks();
    }


    TEST(SegmentBlockArrayTest6464, ZeroBlocks) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        wrbuf.resize(blkarr_blk_sz);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..blkarr_blk_sz

        // neither this (implicit max_data_sz)
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // And nothing is read (explicit max_data_sz)
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        // neither is read in this way (implicit max_data_sz)
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        sg_blkarr.release_blocks();

        // Because we never written anything to the block 1, the "old trailer"
        // is still there (as garbage data)
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }


    TEST(SegmentBlockArrayTest6464, ExtentOutOfBoundsSoFail) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
    }

    TEST(SegmentBlockArrayTest6464, OneBlockButWriteLessBytes) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4), (uint32_t)4);
        EXPECT_EQ(subvec(wrbuf, 0, 4), rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }


    TEST(SegmentBlockArrayTest6464, OneBlockButWriteAtOffset) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "0041 4243 4400 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Read 6 bytes from offset 0 so we can capture what the write_extent
        // wrote
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 6), (uint32_t)6);
        EXPECT_EQ(wrbuf, subvec(rdbuf, 1, -1));

        // Write release_blocks to the end of the block
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, blkarr_blk_sz-4), (uint32_t)4);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0041 4243 4400 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );

        // Read 4 bytes release_blocks at the end of the block
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, 4, blkarr_blk_sz-4), (uint32_t)4);
        EXPECT_EQ(wrbuf, rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                "0041 4243 4400 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );
    }

    TEST(SegmentBlockArrayTest6464, OneBlockBoundary) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        Segment sg(base_blkarr_blk_sz_order);

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
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Try now write past the end of the file
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, 1024), (uint32_t)0);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // Write at a start offset *before* the end of the extent
        // *but* with a length the would go *past* the end of the extent:
        // only the bytes that fall in the extent should be written
        EXPECT_EQ(sg_blkarr.write_extent(ext, wrbuf, 4, (blkarr_blk_sz - 2)), (uint32_t)2);
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
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
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(sg_blkarr.read_extent(ext, rdbuf, (blkarr_blk_sz * 2), 0), (uint32_t)blkarr_blk_sz);
        EXPECT_EQ(subvec(wrbuf, 0, blkarr_blk_sz), rdbuf);

        sg_blkarr.release_blocks();
        XOZ_EXPECT_VECTOR_BLKARR_SERIALIZATION(sg_blkarr, 0, -1,
                //First block (the extent)
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(SegmentBlockArrayTest6464, ShrinkByDeallocExtents) {

        VectorBlockArray base_blkarr(base_blkarr_blk_sz);
        base_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Pre-grow the base block array. This simplifies the reasoning of when
        // an extent is added or not in the segment on calling sg_blkarr.grow_by_blocks
        auto tmp = base_blkarr.allocator().alloc(16 * base_blkarr_blk_sz); // large enough
        base_blkarr.allocator().dealloc(tmp); // TODO should grow_by_blocks add them to the fr alloc?

        Segment sg(base_blkarr_blk_sz_order);

        SegmentBlockArray sg_blkarr(sg, base_blkarr, blkarr_blk_sz);
        sg_blkarr.allocator().initialize_from_allocated(std::list<Segment>());

        // Grow once
        auto old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);
        XOZ_EXPECT_SIZES(sg,
                2, // 1 extent
                base_blkarr_blk_sz // allocated space (measured in base array blk size)
                );

        // Because growing 1 blk makes grow the underlying array grow by 1 blk too,
        // we expect a new non-suballoc extent in the segment of length 1 blk.
        EXPECT_EQ(sg.ext_cnt(), (uint32_t)1);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)1);

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)1);

        // Grow again, this will add more extents to the segment
        old_top_nr = sg_blkarr.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)1);
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 3
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)2);

        // Now shrink by 1 blk. Because the last extent has 2 blks, no real shrink
        // will happen.
        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 3
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)2);

        // Grow by 1 and shrink by 1. See how the grow does not change the segment
        // because it will use the pending-to-remove blk from the step above
        old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)2);
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 3
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)2);

        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 3
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)2);

        // Now shrink by 1 blk again. This plus the 1 blk shrunk before are enough
        // to release the last extent.
        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg,
                2, // 1 extent
                base_blkarr_blk_sz
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)1);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)1);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)1);

        // Grow again, this will add more extents to the segment
        old_top_nr = sg_blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 4
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)4);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)3);

        // Now shrink by 2 blk. Because the last extent has 3 blks, no real shrink
        // will happen.
        sg_blkarr.shrink_by_blocks(2);
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 4
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)4);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)3);

        // Now we release_blocks which should split the last extent of 4 blks
        // to release the 2 pending blks leaving 2 extents of 1 blk each
        sg_blkarr.release_blocks();
        XOZ_EXPECT_SIZES(sg,
                4, // 2 extent
                base_blkarr_blk_sz * 2
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)2);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)2);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)2);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)1);

        // Grow know by 1 block. Notice how this add another extent to the segment
        old_top_nr = sg_blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)2);
        XOZ_EXPECT_SIZES(sg,
                6, // 3 extent
                base_blkarr_blk_sz * 3
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)3);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)3);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)3);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)1);

        // Now shrink by 2 blk. Because the last extent has 1 blk and the next last extent
        // has also 1 blk, this shrink will remove both extents from the segment.
        sg_blkarr.shrink_by_blocks(2);
        XOZ_EXPECT_SIZES(sg,
                2, // 1 extent
                base_blkarr_blk_sz
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)1);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)1);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)1);

        // There is nothing else to release so no change is expected
        sg_blkarr.release_blocks();
        XOZ_EXPECT_SIZES(sg,
                2, // 1 extent
                base_blkarr_blk_sz
                );

        EXPECT_EQ(sg_blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(sg_blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.blk_cnt(), (uint32_t)1);
        EXPECT_EQ(sg_blkarr.capacity(), (uint32_t)1);

        EXPECT_EQ(sg.ext_cnt(), (uint32_t)1);
        EXPECT_EQ(sg.exts().back().is_suballoc(), false);
        EXPECT_EQ(sg.exts().back().blk_cnt(), (uint32_t)1);

        // Shrink further, leave the array/segment empty
        sg_blkarr.shrink_by_blocks(1);
        XOZ_EXPECT_SIZES(sg,
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

