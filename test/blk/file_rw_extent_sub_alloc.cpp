#include "xoz/blk/file_block_array.h"
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

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

#define XOZ_EXPECT_FILE_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(RWExtentSubAllocTest, OneSubBlock) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b0000000000000001, // blk_bitmap
                true// is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        EXPECT_EQ(blkarr.write_extent(ext, wrbuf), (uint32_t)4);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );

        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 4), (uint32_t)4);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );
    }

    TEST(RWExtentSubAllocTest, TwoSubBlocks) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b0010000000000001, // blk_bitmap
                true// is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
        std::vector<char> rdbuf;

        EXPECT_EQ(blkarr.write_extent(ext, wrbuf), (uint32_t)8);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );

        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 8), (uint32_t)8);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );
    }

    TEST(RWExtentSubAllocTest, TwoSubBlocksTwice) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b0010000000000001, // blk_bitmap
                true// is_suballoc
                );

        std::vector<char> wrbuf = {'W', 'X', 'Y', 'Z', 'E', 'F', 'G', 'H'};
        std::vector<char> rdbuf;

        EXPECT_EQ(blkarr.write_extent(ext, wrbuf), (uint32_t)8);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 5758 595a 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );

        std::vector<char> wrbuf2 = {'A', 'B'};

        EXPECT_EQ(blkarr.write_extent(ext, wrbuf2), (uint32_t)2);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 595a 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );

        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 8), (uint32_t)8);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 595a 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );

        EXPECT_EQ(wrbuf2, subvec(rdbuf, 0, 2));
        EXPECT_EQ(subvec(wrbuf, 2), subvec(rdbuf, 2));

        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 2), (uint32_t)2);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 595a 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );

        EXPECT_EQ(wrbuf2, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 4142 595a 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4546 4748"
                );
    }

    TEST(RWExtentSubAllocTest, AllSubBlocks) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b1111111111111111, // blk_bitmap
                true// is_suballoc
                );

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::vector<char> rdbuf;

        EXPECT_EQ(blkarr.write_extent(ext, wrbuf), (uint32_t)64);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 64), (uint32_t)64);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Call read_extent again but let read_extent to figure out how many bytes needs to read
        // (the size of the extent in bytes)
        rdbuf.clear();
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf), (uint32_t)64);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

    }

    TEST(RWExtentSubAllocTest, ZeroSubBlock) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b0000000000000000, // blk_bitmap
                true// is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // Nothing is written (explicit max_data_sz)
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 4), (uint32_t)0);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        wrbuf.resize(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        // neither this (implicit max_data_sz)
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf), (uint32_t)0);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // And nothing is read (explicit max_data_sz)
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 4), (uint32_t)0);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        // neither is read in this way (implicit max_data_sz)
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf), (uint32_t)0);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(RWExtentSubAllocTest, ExtentOutOfBoundsSoFail) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..65
        std::vector<char> rdbuf;

        Extent extOK(
                0, // blk_nr (ok)
                1, // blk_cnt (ok)
                false // is_suballoc
                );

        // write something in the block so we can detect if an invalid write
        // or invalid read take place later when we use "out of bounds" extents
        blkarr.write_extent(extOK, wrbuf);

        // Try to write something obviously different: we shouldn't!
        wrbuf = {'A', 'B', 'C'};

        Extent extOOBCompl(
                1, // blk_nr (out of bounds, the blkarr has only 1 block)
                0b0100000100010001, // blk_bitmap
                true // is_suballoc
                );


        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { blkarr.write_extent(extOOBCompl, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent for suballocation "
                              "[bitmap: 0100000100010001] "
                              "at block 1 "
                              "completely falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { blkarr.read_extent(extOOBCompl, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent for suballocation "
                              "[bitmap: 0100000100010001] "
                              "at block 1 "
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
            // extent bitmap with 4 bits set: 4 * (64/16) = 4 * 4 = 16 bytes
            EXPECT_EQ((unsigned)16, rdbuf.size());
            EXPECT_EQ(std::vector<char>(16), rdbuf);
        }
        rdbuf.clear();

        Extent extOOBZero(
                1, // blk_nr (out of bounds, the blkarr has only 1 block)
                0b000000000000, // blk_bitmap (empty but still out of bounds)
                true // is_suballoc
                );


        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { blkarr.write_extent(extOOBZero, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent for suballocation "
                              "(empty) "
                              "at block 1 "
                              "completely falls out of bounds. "
                              "The blocks from 0 to 0 (inclusive) are within the bounds and allowed. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { blkarr.read_extent(extOOBZero, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent for suballocation "
                              "(empty) "
                              "at block 1 "
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

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
    }

    TEST(RWExtentSubAllocTest, OneSubBlockButWriteLessBytes) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b0000000000000001, // blk_bitmap
                true // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // The buffer is 4 bytes long but we instruct write_extent()
        // to write only 2
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 2), (uint32_t)2);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 0000"
                );

        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 2), (uint32_t)2);
        EXPECT_EQ(subvec(wrbuf, 0, 2), rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 0000"
                );
    }

    TEST(RWExtentSubAllocTest, ThreeSubBlockButWriteAtOffset) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b0010001000000001, // blk_bitmap
                true // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // Write but by an offset of 1
        // Note how the 4 bytes are written in 2 subblocks
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 4, 1), (uint32_t)4);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0041 4243 0000 0000 0000 0000 0000 0000 4400 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Read 6 bytes from offset 0 so we can capture what the write_extent
        // wrote
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 6), (uint32_t)6);
        EXPECT_EQ(wrbuf, subvec(rdbuf, 1, -1));

        // Write close to the end of the block
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 4, 8), (uint32_t)4);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0041 4243 0000 0000 0000 0000 0000 0000 4400 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );

        // Read 4 bytes close at the end of the block
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 4, 8), (uint32_t)4);
        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0041 4243 0000 0000 0000 0000 0000 0000 4400 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );
    }

    TEST(RWExtentSubAllocTest, TwoSubBlockBoundary) {
       const uint32_t blk_sz = 64;

        auto blkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        // Alloc 2 blocks but we will create an extent of 1.
        // The idea is to have room *after* the extent to detect
        // writes/reads out of bounds
        auto old_top_nr = blkarr.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Extent ext(
                0, // blk_nr
                0b1000000000000001, // blk_bitmap
                true // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf = {'.'};

        // Write at a start offset *past* the end of the extent:
        // nothing should be written
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 4, 9), (uint32_t)0);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                //First block (the extent - suballoc'd)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Try now write past the end of the file
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 4, 1024), (uint32_t)0);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                //First block (the extent - suballoc'd)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // Write at a start offset *before* the end of the extent
        // *but* with a length the would go *past* the end of the extent:
        // only the bytes that fall in the extent should be written
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 4, 6), (uint32_t)2);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                //First block (the extent - suballoc'd)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Read at a start offset *past* the end of the extent:
        // nothing should be read
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 4, 9), (uint32_t)0);
        EXPECT_EQ(rdbuf.size(), (unsigned)0);
        rdbuf = {'.'};

        // Try now read past the end of the file
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 4, 1024), (uint32_t)0);
        EXPECT_EQ(rdbuf.size(), (unsigned)0);
        rdbuf = {'.'};

        // Read at a start offset *before* the end of the extent
        // *but* with a length the would go *past* the end of the extent:
        // only the bytes that fall in the extent should be read
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 4, 6), (uint32_t)2);
        EXPECT_EQ(subvec(wrbuf, 0, 2), rdbuf);

        wrbuf.resize(128);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..128

        // Try again write and overflow, with start at 0 but a length too large
        EXPECT_EQ(blkarr.write_extent(ext, wrbuf, 128, 0), (uint32_t)8);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                //First block (the extent - suballoc'd)
                "0001 0203 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0405 0607 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(blkarr.read_extent(ext, rdbuf, 128, 0), (uint32_t)8);
        EXPECT_EQ(subvec(wrbuf, 0, 8), rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                //First block (the extent - suballoc'd)
                "0001 0203 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0405 0607 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

    }
}

