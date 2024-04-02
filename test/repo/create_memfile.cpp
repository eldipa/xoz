#include "xoz/repo/repository.h"
#include "xoz/err/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <cstdlib>

#define SCRATCH_HOME "./scratch/mem/"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;


using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::file2mem;

#define XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, at, len, data) do {           \
    EXPECT_EQ(hexdump((repo).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    // Create a new repository with default settings.
    // Close it and check the dump of the file.
    //
    // The check of the dump is simplistic: it is only to validate
    // that the .xoz file was created and it is non-empty.
    TEST(RepositoryTest, MemCreateNewDefaults) {

        Repository repo = Repository::create_mem_based();

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Check repository's parameters
        // Because we didn't specified anything on Repository::create, it
        // should be using the defaults.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)512);

        // Close and check what we have on disk.
        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0002 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0000 0000 "            // unused
                "09"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 512, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, MemCreateNonDefaults) {

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        Repository repo = Repository::create_mem_based(gp);

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        //std::cout << stats_str; // for easy debug



        // Check repository's parameters after create
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
                // header
                "584f 5a00 "            // magic XOZ\0
                "8000 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0000 0000 "            // unused
                "07"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, MemCreateThenExpand) {

        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        Repository repo = Repository::create_mem_based(gp);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.expose_block_array().grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Add 6 more blocks
        old_top_nr = repo.expose_block_array().grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)4);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)9);

        ss.str("");
        PrintTo(repo, &ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0005 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0a00 0000 "            // blk_total_cnt
                "0000 0000 "            // unused
                "07"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 1280, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, MemCreateThenExpandThenRevert) {

        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        Repository repo = Repository::create_mem_based(gp);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.expose_block_array().grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Now "revert" freeing those 3 blocks
        repo.expose_block_array().shrink_by_blocks(3);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);

        ss.str("");
        PrintTo(repo, &ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
                // header
                "584f 5a00 "            // magic XOZ\0
                "8000 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0000 0000 "            // unused
                "07"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 128, -1,
                // trailer
                "454f 4600"
                );
    }
}

