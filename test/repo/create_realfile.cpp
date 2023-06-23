#include "xoz/repo.h"
#include "xoz/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <cstdlib>

#define SCRATCH_HOME "./scratch/mem/"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

#define DELETE(X) system("rm -f '" SCRATCH_HOME X "'")
#define HEXDUMP(X) system("hexdump -C '" SCRATCH_HOME X "' > '" SCRATCH_HOME X ".hex'")

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::file2mem;

#define XOZ_EXPECT_SERIALIZATION(path, at, len, data) do {           \
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
        DELETE("CreateNewDefaults.xoz.hex");

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

        // Close and check what we have on disk.
        repo.close();
        HEXDUMP("CreateNewDefaults.xoz");

        std::stringstream oss;
        oss << std::ifstream(SCRATCH_HOME "CreateNewDefaults.xoz.hex").rdbuf();
        auto hd_str = oss.str();

        // Part of the header
        EXPECT_THAT(hd_str, HasSubstr("00000000  58 4f 5a 00 0c 00 00 00  00 10 00 00 00 00 00 00  |XOZ.............|"));

        // Part of the trailer
        EXPECT_THAT(hd_str, HasSubstr("00001000  45 4f 46 00                                       |EOF."));
    }

    TEST(RepositoryTest, CreateNewDefaultsThenOpen) {
        DELETE("CreateNewDefaultsThenOpen.xoz");
        DELETE("CreateNewDefaultsThenOpen.xoz.hex");

        Repository new_repo = Repository::create(SCRATCH_HOME "CreateNewDefaultsThenOpen.xoz", true);
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

        // Close and check that the file in disk still exists
        // Note: in CreateNewDefaults test we create-close-check, here
        // we do create-close-open-close-check.
        repo.close();
        HEXDUMP("CreateNewDefaultsThenOpen.xoz");

        std::stringstream oss;
        oss << std::ifstream(SCRATCH_HOME "CreateNewDefaultsThenOpen.xoz.hex").rdbuf();
        auto hd_str = oss.str();

        // Part of the header
        EXPECT_THAT(hd_str, HasSubstr("00000000  58 4f 5a 00 0c 00 00 00  00 10 00 00 00 00 00 00  |XOZ.............|"));

        // Part of the trailer
        EXPECT_THAT(hd_str, HasSubstr("00001000  45 4f 46 00                                       |EOF."));
    }

    TEST(RepositoryTest, CreateNonDefaultsThenOpen) {
        DELETE("CreateNonDefaultsThenOpen.xoz");
        DELETE("CreateNonDefaultsThenOpen.xoz.hex");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        Repository new_repo = Repository::create(SCRATCH_HOME "CreateNonDefaultsThenOpen.xoz", true, 0, gp);

        // Check repository's parameters after create
        EXPECT_EQ(new_repo.params().blk_sz, gp.blk_sz);
        EXPECT_EQ(new_repo.params().blk_sz_order, gp.blk_sz_order);
        EXPECT_EQ(new_repo.params().phy_repo_start_pos, gp.phy_repo_start_pos);
        EXPECT_EQ(new_repo.params().blk_init_cnt, gp.blk_init_cnt);

        new_repo.close();

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

        repo.close();
    }

    TEST(RepositoryTest, CreateNonDefaultsThenOpenCloseOpen) {
        DELETE("CreateNonDefaultsThenOpenCloseOpen.xoz");
        DELETE("CreateNonDefaultsThenOpenCloseOpen.xoz.hex");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        Repository new_repo = Repository::create(SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz", true, 0, gp);
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

        repo.close();
    }

    TEST(RepositoryTest, CreateThenRecreateAndOverride) {
        DELETE("CreateThenRecreateAndOverride.xoz");
        DELETE("CreateThenRecreateAndOverride.xoz.hex");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        Repository new_repo = Repository::create(SCRATCH_HOME "CreateThenRecreateAndOverride.xoz", true, 0, gp);
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

        repo.close();
    }

    TEST(RepositoryTest, CreateThenRecreateButFail) {
        DELETE("CreateThenRecreateButFail.xoz");
        DELETE("CreateThenRecreateButFail.xoz.hex");

        // Custom non-default parameters
        GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 4
        };

        Repository new_repo = Repository::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", true, 0, gp);
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

        repo.close();
    }

    TEST(RepositoryTest, CreateThenExpand) {
        DELETE("CreateThenExpand.xoz");
        DELETE("CreateThenExpand.xoz.hex");

        Repository repo = Repository::create(SCRATCH_HOME "CreateThenExpand.xoz", true);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 2048 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Add 6 more blocks
        old_top_nr = repo.grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)4);

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 5120 bytes, 10 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Close and reopen and check again
        repo.close();
        repo.open(SCRATCH_HOME "CreateThenExpand.xoz");

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 5120 bytes, 10 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        repo.close();
        HEXDUMP("CreateThenExpand.xoz");

        std::stringstream oss;
        oss << std::ifstream(SCRATCH_HOME "CreateThenExpand.xoz.hex").rdbuf();
        auto hd_str = oss.str();

        // Part of the trailer
        // Note the position of the trailer that should
        // match the size of the expanded file
        EXPECT_THAT(hd_str, HasSubstr("0000a000  45 4f 46 00                                       |EOF."));

    }

    TEST(RepositoryTest, CreateThenExpandThenRevert) {
        DELETE("CreateThenExpandThenRevert.xoz");
        DELETE("CreateThenExpandThenRevert.xoz.hex");

        Repository repo = Repository::create(SCRATCH_HOME "CreateThenExpandThenRevert.xoz", true);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 2048 bytes, 4 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Now "revert" freeing those 3 blocks
        repo.shrink_by_blocks(3);

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 512 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        // Close and reopen and check again
        repo.close();
        repo.open(SCRATCH_HOME "CreateThenExpandThenRevert.xoz");

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Repository size: 512 bytes, 1 blocks"));
        EXPECT_THAT(stats_str, HasSubstr("Block size: 512 bytes (order: 9)"));
        EXPECT_THAT(stats_str, HasSubstr("Trailer size: 4 bytes"));

        repo.close();
        HEXDUMP("CreateThenExpandThenRevert.xoz");

        std::stringstream oss;
        oss << std::ifstream(SCRATCH_HOME "CreateThenExpandThenRevert.xoz.hex").rdbuf();
        auto hd_str = oss.str();

        // Part of the trailer
        // Note the position of the trailer that should
        // match the size of the expanded-but-then-reverted file
        EXPECT_THAT(hd_str, HasSubstr("00001000  45 4f 46 00                                       |EOF."));

    }
}
