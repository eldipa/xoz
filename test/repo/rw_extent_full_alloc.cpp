#include "xoz/repo/repository.h"
#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"

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
    TEST(RWExtentFullAllocTest, OneBlock) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000"
                );

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        Extent ext(
                1, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        EXPECT_EQ(repo.write_extent(ext, wrbuf), (uint32_t)4);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4), (uint32_t)4);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }

    TEST(RWExtentFullAllocTest, OneBlockTwice) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F', 'G'};
        std::vector<char> rdbuf;

        EXPECT_EQ(repo.write_extent(ext, wrbuf), (uint32_t)7);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4142 4344 4546 4700 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // override first bytes but leave the rest untouched
        std::vector<char> wrbuf2 = {'D', 'E', 'B'};
        EXPECT_EQ(repo.write_extent(ext, wrbuf2), (uint32_t)3);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4445 4244 4546 4700 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(repo.read_extent(ext, rdbuf, 3), (uint32_t)3);
        EXPECT_EQ(wrbuf2, rdbuf);

        // override the expected buffer for comparison
        memcpy(wrbuf.data(), wrbuf2.data(), 3);

        EXPECT_EQ(repo.read_extent(ext, rdbuf, 7), (uint32_t)7);
        EXPECT_EQ(wrbuf, rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4445 4244 4546 4700 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }

    TEST(RWExtentFullAllocTest, OneBlockCompletely) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::vector<char> rdbuf;

        EXPECT_EQ(repo.write_extent(ext, wrbuf), (uint32_t)64);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(repo.read_extent(ext, rdbuf, 64), (uint32_t)64);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Call read_extent again but let read_extent to figure out how many bytes needs to read
        // (the size of the extent in bytes)
        rdbuf.clear();
        EXPECT_EQ(repo.read_extent(ext, rdbuf), (uint32_t)64);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "454f 4600"
                );
    }

    TEST(RWExtentFullAllocTest, TwoBlocks) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                2, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf(65); // blk_sz + 1
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..65

        std::vector<char> rdbuf;

        EXPECT_EQ(repo.write_extent(ext, wrbuf), (uint32_t)65);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "4000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(repo.read_extent(ext, rdbuf, 65), (uint32_t)65);
        EXPECT_EQ(wrbuf, rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "4000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }


    TEST(RWExtentFullAllocTest, MaxBlocks) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const auto max_blk_cnt = (1 << 16) - 1;

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(max_blk_cnt);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                max_blk_cnt, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf(max_blk_cnt * gp.blk_sz);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0x00..0xc0

        std::vector<char> rdbuf;

        EXPECT_EQ(repo.write_extent(ext, wrbuf), (uint32_t)(max_blk_cnt * gp.blk_sz));
        EXPECT_EQ(repo.read_extent(ext, rdbuf), (uint32_t)(max_blk_cnt * gp.blk_sz));
        EXPECT_EQ(wrbuf, rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, 64,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 4194240, -1,
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "454f 4600"
                );
    }


    TEST(RWExtentFullAllocTest, ZeroBlocks) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                0, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // Nothing is written (explicit max_data_sz)
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4), (uint32_t)0);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        wrbuf.resize(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        // neither this (implicit max_data_sz)
        EXPECT_EQ(repo.write_extent(ext, wrbuf), (uint32_t)0);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );


        // And nothing is read (explicit max_data_sz)
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4), (uint32_t)0);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        // neither is read in this way (implicit max_data_sz)
        EXPECT_EQ(repo.read_extent(ext, rdbuf), (uint32_t)0);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(std::vector<char>(), rdbuf);

        repo.close();

        // Because we never written anything to the block 1, the "old trailer"
        // is still there (as garbage data)
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }


    TEST(RWExtentFullAllocTest, NullBlockAndFail) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                0, // blk_nr (null block)
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::vector<char> rdbuf;

        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { repo.write_extent(ext, wrbuf); },
            ThrowsMessage<NullBlockAccess>(
                AllOf(
                    HasSubstr("The block 0x00 cannot be written")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { repo.read_extent(ext, rdbuf); },
            ThrowsMessage<NullBlockAccess>(
                AllOf(
                    HasSubstr("The block 0x00 cannot be read")
                    )
                )
        );

        EXPECT_EQ(std::vector<char>(), rdbuf);

        repo.close();

        // Block 0 was untouched (the XOZ magic is still there)
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 0, 4,
                "584f 5a00"
                );

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }

    TEST(RWExtentFullAllocTest, ExtentOutOfBoundsSoFail) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..65
        std::vector<char> rdbuf;

        Extent extOK(
                1, // blk_nr (ok)
                1, // blk_cnt (ok)
                false // is_suballoc
                );

        // write something in the block so we can detect if an invalid write
        // or invalid read take place later when we use "out of bounds" extents
        repo.write_extent(extOK, wrbuf);

        // Try to write something obviously different: we shouldn't!
        wrbuf = {'A', 'B', 'C'};

        Extent extOOBCompl(
                2, // blk_nr (out of bounds, the repo has only 1 block)
                1, // blk_cnt
                false // is_suballoc
                );


        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { repo.write_extent(extOOBCompl, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 1 blocks "
                              "that starts at block 2 and ends at block 2 "
                              "completely falls out of bounds. "
                              "The block 1 is the last valid before the end. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { repo.read_extent(extOOBCompl, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 1 blocks "
                              "that starts at block 2 and ends at block 2 "
                              "completely falls out of bounds. "
                              "The block 1 is the last valid before the end. "
                              "Detected on a read operation.")
                    )
                )
        );

        // On an out of bounds read, it is not specified the value of
        // the read buffer. The may be empty and fill of zeros. Check both.
        if (rdbuf.size() == 0) {
            EXPECT_EQ(std::vector<char>(), rdbuf);
        } else {
            // extent 1 block long: 64 bytes
            EXPECT_EQ((unsigned)64, rdbuf.size());
            EXPECT_EQ(std::vector<char>(64), rdbuf);
        }
        rdbuf.clear();

        Extent extOOBZero(
                2, // blk_nr (out of bounds, the repo has only 1 block)
                0, // blk_cnt (empty extent but still out of bounds)
                false // is_suballoc
                );


        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { repo.write_extent(extOOBZero, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 0 blocks (empty) "
                              "at block 2 "
                              "completely falls out of bounds. "
                              "The block 1 is the last valid before the end. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { repo.read_extent(extOOBZero, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 0 blocks (empty) "
                              "at block 2 "
                              "completely falls out of bounds. "
                              "The block 1 is the last valid before the end. "
                              "Detected on a read operation.")
                    )
                )
        );

        // On an out of bounds read, it is not specified the value of
        // the read buffer. However in this case we expect to have a 0 size.
        EXPECT_EQ(std::vector<char>(), rdbuf);
        rdbuf.clear();

        Extent extOOBPart(
                1, // blk_nr (ok, within the bounds but...)
                2, // blk_cnt (bad!, the extent spans beyond the end)
                false // is_suballoc
                );

        // Nothing is either read nor written
        EXPECT_THAT(
            [&]() { repo.write_extent(extOOBPart, wrbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 2 blocks "
                              "that starts at block 1 and ends at block 2 "
                              "partially falls out of bounds. "
                              "The block 1 is the last valid before the end. "
                              "Detected on a write operation.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { repo.read_extent(extOOBPart, rdbuf); },
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr("The extent of 2 blocks "
                              "that starts at block 1 and ends at block 2 "
                              "partially falls out of bounds. "
                              "The block 1 is the last valid before the end. "
                              "Detected on a read operation.")
                    )
                )
        );

        // On an out of bounds read, it is not specified the value of
        // the read buffer. The may be empty and fill of zeros. Check both.
        if (rdbuf.size() == 0) {
            EXPECT_EQ(std::vector<char>(), rdbuf);
        } else {
            // extent 2 blocks long: 64 * 2 = 128 bytes
            EXPECT_EQ((unsigned)128, rdbuf.size());
            EXPECT_EQ(std::vector<char>(128), rdbuf);
        }
        rdbuf.clear();

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "454f 4600"
                );
    }

    TEST(RWExtentFullAllocTest, OneBlockButWriteLessBytes) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F'};
        std::vector<char> rdbuf;

        // The buffer is 6 bytes long but we instruct write_extent()
        // to write only 4
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4), (uint32_t)4);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4), (uint32_t)4);
        EXPECT_EQ(subvec(wrbuf, 0, 4), rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }


    TEST(RWExtentFullAllocTest, OneBlockButWriteAtOffset) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        auto old_top_nr = repo.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        // Write but by an offset of 1
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4, 1), (uint32_t)4);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0041 4243 4400 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Read 6 bytes from offset 0 so we can capture what the write_extent
        // wrote
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 6), (uint32_t)6);
        EXPECT_EQ(wrbuf, subvec(rdbuf, 1, -1));

        // Write close to the end of the block
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4, 60), (uint32_t)4);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0041 4243 4400 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344"
                );

        // Read 4 bytes close at the end of the block
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4, 60), (uint32_t)4);
        EXPECT_EQ(wrbuf, rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0041 4243 4400 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 4344 "
                "454f 4600"
                );
    }

    TEST(RWExtentFullAllocTest, OneBlockBoundary) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        // Alloc 2 blocks but we will create an extent of 1.
        // The idea is to have room *after* the extent to detect
        // writes/reads out of bounds
        auto old_top_nr = repo.grow_by_blocks(2);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        Extent ext(
                1, // blk_nr
                1, // blk_cnt
                false // is_suballoc
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf = {'.'};

        // Write at a start offset *past* the end of the extent:
        // nothing should be written
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4, 65), (uint32_t)0);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Try now write past the end of the file
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4, 1024), (uint32_t)0);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
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
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 4, 62), (uint32_t)2);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                //First block (the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 4142 "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Read at a start offset *past* the end of the extent:
        // nothing should be read
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4, 65), (uint32_t)0);
        EXPECT_EQ(rdbuf.size(), (unsigned)0);
        rdbuf = {'.'};

        // Try now read past the end of the file
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4, 1024), (uint32_t)0);
        EXPECT_EQ(rdbuf.size(), (unsigned)0);
        rdbuf = {'.'};

        // Read at a start offset *before* the end of the extent
        // *but* with a length the would go *past* the end of the extent:
        // only the bytes that fall in the extent should be read
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 4, 62), (uint32_t)2);
        EXPECT_EQ(subvec(wrbuf, 0, 2), rdbuf);

        wrbuf.resize(128);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..128

        // Try again write and overflow, with start at 0 but a length too large
        EXPECT_EQ(repo.write_extent(ext, wrbuf, 128, 0), (uint32_t)64);
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                //First block (the extent)
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        EXPECT_EQ(repo.read_extent(ext, rdbuf, 128, 0), (uint32_t)64);
        EXPECT_EQ(subvec(wrbuf, 0, 64), rdbuf);

        repo.close();
        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                //First block (the extent)
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                //Second block (past the extent)
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "454f 4600"
                );
    }
}
