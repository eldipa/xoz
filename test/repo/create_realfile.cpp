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

#define DELETE(X) system("rm -f '" SCRATCH_HOME X "'")

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::file2mem;

#define XOZ_EXPECT_FILE_SERIALIZATION(path, at, len, data) do {           \
    EXPECT_EQ(hexdump(file2mem(path), (at), (len)), (data));              \
} while (0)

namespace {
    // Create a new repository with default settings.
    // Close it and check the dump of the file.
    //
    // The check of the dump is simplistic: it is only to validate
    // that the .xoz file was created and it is non-empty.
    TEST(RepositoryTest, CreateNewDefaults) {
        DELETE("CreateNewDefaults.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewDefaults.xoz";
        Repository repo = Repository::create(fpath, true);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 512 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Check repository's parameters
        // Because we didn't specified anything on Repository::create, it
        // should be using the defaults.
        //
        // Check those too.
        GlobalParameters gp;

        EXPECT_EQ(repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(repo.params().blk_sz, (uint32_t)512);

        EXPECT_EQ(repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(repo.params().blk_sz_order, (uint8_t)9);

        EXPECT_EQ(repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(repo.params().phy_repo_start_pos, (uint64_t)0);

        EXPECT_EQ(repo.params().blk_init_cnt, gp.blk_init_cnt);
        EXPECT_EQ(repo.params().blk_init_cnt, (uint32_t)1);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        // Close and check what we have on disk.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0002 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "09"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 512, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNewDefaultsThenOpen) {
        DELETE("CreateNewDefaultsThenOpen.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(fpath, true);
        new_repo.close();

        Repository repo(SCRATCH_HOME "CreateNewDefaultsThenOpen.xoz");

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 512 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Check repository's parameters
        // Because we didn't specified anything on Repository::create, it
        // should be using the defaults.
        GlobalParameters gp;

        EXPECT_EQ(repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(repo.params().blk_init_cnt, gp.blk_init_cnt);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        // Close and check that the file in disk still exists
        // Note: in CreateNewDefaults test we create-close-check, here
        // we do create-close-open-close-check.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0002 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "09"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 512, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNonDefaultsThenOpen) {
        DELETE("CreateNonDefaultsThenOpen.xoz");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        const char* fpath = SCRATCH_HOME "CreateNonDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(fpath, true, 0, gp);

        // Check repository's parameters after create
        EXPECT_EQ(new_repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(new_repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(new_repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(new_repo.params().blk_init_cnt, gp.blk_init_cnt);

        EXPECT_EQ(new_repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(new_repo.blk_cnt(), (uint32_t)3);

        new_repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0001 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0400 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );

        Repository repo(SCRATCH_HOME "CreateNonDefaultsThenOpen.xoz");

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Check repository's parameters after open
        EXPECT_EQ(repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(repo.params().blk_init_cnt, gp.blk_init_cnt);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0001 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0400 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNonDefaultsThenOpenCloseOpen) {
        DELETE("CreateNonDefaultsThenOpenCloseOpen.xoz");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        const char* fpath = SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz";
        Repository new_repo = Repository::create(fpath, true, 0, gp);
        new_repo.close();

        Repository repo(SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz");

        // Close and reopen again
        repo.close();
        repo.open(SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz");

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Check repository's parameters after open
        EXPECT_EQ(repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(repo.params().blk_init_cnt, gp.blk_init_cnt);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0001 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0400 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenRecreateAndOverride) {
        DELETE("CreateThenRecreateAndOverride.xoz");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateAndOverride.xoz";
        Repository new_repo = Repository::create(fpath, true, 0, gp);
        new_repo.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        Repository repo = Repository::create(SCRATCH_HOME "CreateThenRecreateAndOverride.xoz", false);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Check repository's parameters after open
        // Because the second Repository::create *did not* create a fresh
        // repository with a default params **but** it opened the previously
        // created repository.
        EXPECT_EQ(repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(repo.params().blk_init_cnt, gp.blk_init_cnt);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0001 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0400 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenRecreateButFail) {
        DELETE("CreateThenRecreateButFail.xoz");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateButFail.xoz";
        Repository new_repo = Repository::create(fpath, true, 0, gp);
        new_repo.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists but instead it will open it
        EXPECT_THAT(
            [&]() { Repository::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", true); },
            ThrowsMessage<OpenXOZError>(
                AllOf(
                    HasSubstr("Repository::create"),
                    HasSubstr("the file already exist and Repository::create is configured to not override it")
                    )
                )
        );

        // Try to open it again, this time with fail_if_exists == False.
        // Check that the previous failed creation **did not** corrupted the original
        // file
        Repository repo = Repository::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", false);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Check repository's parameters after open
        // Because the second Repository::create *did not* create a fresh
        // repository with a default params **but** it opened the previously
        // created repository.
        EXPECT_EQ(repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(repo.params().blk_init_cnt, gp.blk_init_cnt);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0001 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0400 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenExpand) {
        DELETE("CreateThenExpand.xoz");

        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const char* fpath = SCRATCH_HOME "CreateThenExpand.xoz";
        Repository repo = Repository::create(fpath, true, 0, gp);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Add 6 more blocks
        old_top_nr = repo.grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)4);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)9);

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 640 bytes, 10 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "8002 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0a00 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 640, -1,
                // trailer
                "454f 4600"
                );

        repo.open(SCRATCH_HOME "CreateThenExpand.xoz");

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 640 bytes, 10 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)9);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "8002 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0a00 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 640, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, CreateThenExpandThenRevert) {
        DELETE("CreateThenExpandThenRevert.xoz");

        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevert.xoz";
        Repository repo = Repository::create(fpath, true, 0, gp);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Now "revert" freeing those 3 blocks
        repo.shrink_by_blocks(3);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 64 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "4000 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 64, -1,
                // trailer
                "454f 4600"
                );
        repo.open(SCRATCH_HOME "CreateThenExpandThenRevert.xoz");

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 64 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "4000 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 64, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, CreateThenExpandCloseThenShrink) {
        DELETE("CreateThenExpandCloseThenShrink.xoz");

        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const char* fpath = SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz";
        Repository repo = Repository::create(fpath, true, 0, gp);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)3);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 256 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Close and check: the file should be grown
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0001 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        // Note the position: 256 is at the end of the last block
        // Also note the length: -1 means read to the end of the file
        // We should read the trailer only proving that the file grow
        // to that position *and* nothing else follows.
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );

        // Now "shrink" freeing those 3 blocks
        repo.open(SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz");
        repo.shrink_by_blocks(3);

        EXPECT_EQ(repo.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.blk_cnt(), (uint32_t)0);

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 64 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 64 bytes (order: 6)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Close and check again: the file should shrank
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 64,
                // header
                "584f 5a00 "            // magic XOZ\0
                "4000 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0100 0000 "            // blk_total_cnt
                "0100 0000 "            // blk_init_cnt
                "06"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000"
                );

        // The position 64 is where the trailer should be present
        // if the file shrank at the "logical" level.
        // With a length of -1 (read to the end of the file), we check
        // that also the file shrank at the "physical" level (in disk)
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 64, -1,
                // trailer
                "454f 4600"
                );
    }
}
