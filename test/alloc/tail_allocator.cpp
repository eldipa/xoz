#include "xoz/blk/file_block_array.h"
#include "xoz/ext/extent.h"
#include "xoz/err/exceptions.h"
#include "xoz/alloc/tail_allocator.h"

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
    TEST(TailAllocatorTest, ResetAnEmptyAllocator) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        alloc.reset();

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);
    }

    TEST(TailAllocatorTest, ReleaseAnEmptyAllocator) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        alloc.release();

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);
    }

    TEST(TailAllocatorTest, AllocAndGrow) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        auto result1 = alloc.alloc(3);

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(0, 3, false));

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)3);

        auto result2 = alloc.alloc(2);

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(3, 2, false));

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)5);

        // A reset() dealloc all the extents and it implies a call to release()
        // so the blkarr should free any pending-to-free blocks
        // We expect then an empty block array at the end.
        alloc.reset();

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);
    }

    TEST(TailAllocatorTest, DeallocAndShrink) {
        std::stringstream cpy;

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        const uint16_t blk_allocated_test = 5;
        alloc.alloc(blk_allocated_test);

        // Write one block at time with some predefined payload
        // changing only the first bytes to distinguish which block is which
        for (int i = 0; i < blk_allocated_test; ++i) {
            wrbuf[0] = wrbuf[1] = wrbuf[2] = wrbuf[3] = char(0xaa + char(0x11 * i));
            EXPECT_EQ(blkarr.write_extent(Extent(i, 1, false), wrbuf), (uint32_t)64);
        }

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "dddd dddd 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "eeee eeee 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)5);

        bool ok = alloc.dealloc(Extent(3, 2, false));
        EXPECT_EQ(ok, (bool)true);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)3);

        alloc.release();
        blkarr.close();

        cpy.str(blkarr.expose_mem_fp().str());

        FileBlockArray blkarr2(std::move(cpy), blkarr.blk_sz());
        TailAllocator alloc2;
        alloc2.manage_block_array(blkarr2);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr2, 0, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(blkarr2.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr2.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(blkarr2.blk_cnt(), (uint32_t)3);

        ok = alloc2.dealloc(Extent(1, 2, false));
        EXPECT_EQ(ok, (bool)true);

        EXPECT_EQ(blkarr2.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr2.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr2.blk_cnt(), (uint32_t)1);

        alloc2.release();
        blkarr2.close();

        cpy.str(blkarr2.expose_mem_fp().str());

        FileBlockArray blkarr3(std::move(cpy), blkarr2.blk_sz());
        TailAllocator alloc3;
        alloc3.manage_block_array(blkarr3);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr3, 0, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        ok = alloc3.dealloc(Extent(0, 1, false));
        EXPECT_EQ(ok, (bool)true);

        EXPECT_EQ(blkarr3.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr3.past_end_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr3.blk_cnt(), (uint32_t)0);

        alloc3.release();
        blkarr3.close();

        cpy.str(blkarr3.expose_mem_fp().str());

        FileBlockArray blkarr4(std::move(cpy), blkarr3.blk_sz());
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr4, 0, -1, "");
    }

    TEST(TailAllocatorTest, DeallocButIgnored) {
        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        const uint16_t blk_allocated_test = 5;
        alloc.alloc(blk_allocated_test);

        // Write one block at time with some predefined payload
        // changing only the first bytes to distinguish which block is which
        for (int i = 0; i < blk_allocated_test; ++i) {
            wrbuf[0] = wrbuf[1] = wrbuf[2] = wrbuf[3] = char(0xaa + char(0x11 * i));
            EXPECT_EQ(blkarr.write_extent(Extent(i, 1, false), wrbuf), (uint32_t)64);
        }

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "dddd dddd 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "eeee eeee 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)5);

        // Valid Extent but it is not at the end of the file so the TailAllocator
        // will ignore the dealloc and return false
        bool ok = alloc.dealloc(Extent(3, 1, false));
        EXPECT_EQ(ok, (bool)false);

        // Therefore no block was freed and the blkarr content is unchanged.
        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)5);

        alloc.release();
        blkarr.close();

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "dddd dddd 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //-------------------------------------------------------------------------------
                "eeee eeee 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
    }

    TEST(TailAllocatorTest, OOBDealloc) {
        {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        const uint16_t blk_allocated_test = 3;
        alloc.alloc(blk_allocated_test);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)0);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)3);

        EXPECT_THAT(
                // Blk number past the end of the file
            [&]() { alloc.dealloc(Extent(3, 1, false)); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 1 blocks that starts at block 3 and "
                        "ends at block 3 completely falls out of bounds. "
                        "The blocks from 0 to 2 (inclusive) are within the bounds and allowed. "
                        "Detected on TailAllocator::dealloc"
                        )
                    )
                )
        );

        EXPECT_THAT(
                // Blk number (start) within the boundaries but
                // it extends beyond the limits
            [&]() { alloc.dealloc(Extent(2, 2, false)); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 2 blocks that starts at block 2 and "
                        "ends at block 3 partially falls out of bounds. "
                        "The blocks from 0 to 2 (inclusive) are within the bounds and allowed. "
                        "Detected on TailAllocator::dealloc"
                        )
                    )
                )
        );
        }

        {
            auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
            FileBlockArray& blkarr = *blkarr_ptr.get();
            TailAllocator alloc;
            alloc.manage_block_array(blkarr);

            const uint16_t blk_allocated_test = 3;
            alloc.alloc(blk_allocated_test);

            EXPECT_THAT(
                    // Blk number (start) lower than the minimum block
                    [&]() { alloc.dealloc(Extent(0, 2, false)); },
                    ThrowsMessage<ExtentOutOfBounds>(
                        AllOf(
                            HasSubstr(
                                "The extent of 2 blocks that starts at block 0 and "
                                "ends at block 1 partially falls out of bounds. "
                                "The blocks from 1 to 3 (inclusive) are within the bounds and allowed. "
                                "Detected on TailAllocator::dealloc"
                                )
                            )
                        )
                    );
        }
    }

    TEST(TailAllocatorTest, InvalidAllocOfZeroBlocks) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        EXPECT_THAT(
            [&]() { alloc.alloc(0); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot alloc 0 blocks")
                    )
                )
        );
    }

    TEST(TailAllocatorTest, InvalidDeallocOfZeroBlocks) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        EXPECT_THAT(
            [&]() { alloc.dealloc(Extent(4, 0, false)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc 0 blocks")
                    )
                )
        );
    }

    TEST(TailAllocatorTest, InvalidDeallocOfSuballocatedBlock) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        TailAllocator alloc;
        alloc.manage_block_array(blkarr);

        EXPECT_THAT(
            [&]() { alloc.dealloc(Extent(4, 4, true)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc suballoc extent")
                    )
                )
        );
    }
}
