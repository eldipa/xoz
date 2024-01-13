#include "xoz/repo/repository.h"
#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"
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

// Check that the serialization of the extents in fp are of the
// expected size (call calc_struct_footprint_size) and they match
// byte-by-byte with the expected data (in hexdump)
#define XOZ_EXPECT_REPO_SERIALIZATION(repo, at, len, data) do {           \
    EXPECT_EQ(hexdump((repo).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(TailAllocatorTest, AllocAndGrow) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000"
                );

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        auto result1 = alloc.alloc(3);

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(1, 3, false));

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        auto result2 = alloc.alloc(2);

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(4, 2, false));

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
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

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)6);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)5);
    }

    TEST(TailAllocatorTest, DeallocAndShrink) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        std::stringstream cpy;

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

        const uint16_t blk_allocated_test = 5;
        alloc.alloc(blk_allocated_test);

        // Write one block at time with some predefined payload
        // changing only the first bytes to distinguish which block is which
        for (int i = 0; i < blk_allocated_test; ++i) {
            wrbuf[0] = wrbuf[1] = wrbuf[2] = wrbuf[3] = char(0xaa + char(0x11 * i));
            EXPECT_EQ(repo.write_extent(Extent(i+1, 1, false), wrbuf), (uint32_t)64);
        }

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "dddd dddd 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "eeee eeee 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)6);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)5);

        bool ok = alloc.dealloc(Extent(4, 2, false));
        EXPECT_EQ(ok, (bool)true);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        repo.close();
        cpy.str(repo.expose_mem_fp().str());
        repo.open(std::move(cpy), 0);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000" /* after the close() a trailer is added that it is zero'd after the open() */
                );

        ok = alloc.dealloc(Extent(2, 2, false));
        EXPECT_EQ(ok, (bool)true);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)1);

        repo.close();
        cpy.str(repo.expose_mem_fp().str());
        repo.open(std::move(cpy), 0);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000" /* after the close() a trailer is added that it is zero'd after the open() */
                );

        ok = alloc.dealloc(Extent(1, 1, false));
        EXPECT_EQ(ok, (bool)true);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        repo.close();
        cpy.str(repo.expose_mem_fp().str());
        repo.open(std::move(cpy), 0);


        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1, "0000 0000");
    }

    TEST(TailAllocatorTest, DeallocButIgnored) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        std::stringstream cpy;

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

        const uint16_t blk_allocated_test = 5;
        alloc.alloc(blk_allocated_test);

        // Write one block at time with some predefined payload
        // changing only the first bytes to distinguish which block is which
        for (int i = 0; i < blk_allocated_test; ++i) {
            wrbuf[0] = wrbuf[1] = wrbuf[2] = wrbuf[3] = char(0xaa + char(0x11 * i));
            EXPECT_EQ(repo.write_extent(Extent(i+1, 1, false), wrbuf), (uint32_t)64);
        }

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "dddd dddd 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "eeee eeee 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)6);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)5);

        // Valid Extent but it is not at the end of the file so the TailAllocator
        // will ignore the dealloc and return false
        bool ok = alloc.dealloc(Extent(4, 1, false));
        EXPECT_EQ(ok, (bool)false);

        // Therefore no block was freed and the repo content is unchanged.
        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)6);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)5);

        repo.close();

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "aaaa aaaa 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "bbbb bbbb 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "cccc cccc 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "dddd dddd 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "eeee eeee 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "454f 4600"
                );
    }

    TEST(TailAllocatorTest, OOBDealloc) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

        const uint16_t blk_allocated_test = 3;
        alloc.alloc(blk_allocated_test);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        EXPECT_THAT(
                // Blk number past the end of the file
            [&]() { alloc.dealloc(Extent(4, 1, false)); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 1 blocks that starts at block 4 and "
                        "ends at block 4 completely falls out of bounds. "
                        "The blocks from 1 to 3 (inclusive) are within the bounds and allowed. "
                        "Detected on TailAllocator::dealloc"
                        )
                    )
                )
        );

        EXPECT_THAT(
                // Blk number (start) within the boundaries but
                // it extends beyond the limits
            [&]() { alloc.dealloc(Extent(3, 2, false)); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 2 blocks that starts at block 3 and "
                        "ends at block 4 partially falls out of bounds. "
                        "The blocks from 1 to 3 (inclusive) are within the bounds and allowed. "
                        "Detected on TailAllocator::dealloc"
                        )
                    )
                )
        );

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

    TEST(TailAllocatorTest, InvalidAllocOfZeroBlocks) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

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
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

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
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        TailAllocator alloc(repo);

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
