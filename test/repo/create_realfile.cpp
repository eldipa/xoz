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
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        // Close and check that the file in disk still exists
        // Note: in CreateNewDefaults test we create-close-check, here
        // we do create-close-open-close-check.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 512, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNonDefaultsThenOpen) {
        DELETE("CreateNonDefaultsThenOpen.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateNonDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);

        // Check repository's parameters after create
        EXPECT_EQ(new_repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(new_repo.expose_block_array().blk_sz(), (uint32_t)128);

        new_repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        Repository repo(SCRATCH_HOME "CreateNonDefaultsThenOpen.xoz");

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Check repository's parameters after open
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNonDefaultsThenOpenCloseOpen) {
        DELETE("CreateNonDefaultsThenOpenCloseOpen.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);
        new_repo.close();

        {
            Repository repo(SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz");

            // Close and reopen again
            repo.close();
        }

        Repository repo(SCRATCH_HOME "CreateNonDefaultsThenOpenCloseOpen.xoz");

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Check repository's parameters after open
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenRecreateAndOverride) {
        DELETE("CreateThenRecreateAndOverride.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateAndOverride.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);
        new_repo.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        Repository repo = Repository::create(SCRATCH_HOME "CreateThenRecreateAndOverride.xoz", false);

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Check repository's parameters after open
        // Because the second Repository::create *did not* create a fresh
        // repository with a default params **but** it opened the previously
        // created repository.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenRecreateButFail) {
        DELETE("CreateThenRecreateButFail.xoz");

        // Custom non-default parameters
       struct Repository::default_parameters_t  gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateButFail.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);
        new_repo.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists but instead it will open it
        EXPECT_THAT(
            [&]() { Repository::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", true); },
            ThrowsMessage<OpenXOZError>(
                AllOf(
                    HasSubstr("the file already exist and FileBlockArray::create is configured to not override it")
                    )
                )
        );

        // Try to open it again, this time with fail_if_exists == False.
        // Check that the previous failed creation **did not** corrupted the original
        // file
        Repository repo = Repository::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", false);

        std::stringstream ss;
        PrintTo(repo, &ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Check repository's parameters after open
        // Because the second Repository::create *did not* create a fresh
        // repository with a default params **but** it opened the previously
        // created repository.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenExpand) {
        DELETE("CreateThenExpand.xoz");

        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateThenExpand.xoz";
        Repository repo = Repository::create(fpath, true, gp);

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
        //std::cout << stats_str; // for easy debug


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
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 1280, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(SCRATCH_HOME "CreateThenExpand.xoz");

        ss.str("");
        PrintTo(repo2, &ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)9);

        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 1280, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, CreateThenExpandThenRevert) {
        DELETE("CreateThenExpandThenRevert.xoz");

        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevert.xoz";
        Repository repo = Repository::create(fpath, true, gp);

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
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(SCRATCH_HOME "CreateThenExpandThenRevert.xoz");

        ss.str("");
        PrintTo(repo2, &ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)0);

        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, CreateThenExpandCloseThenShrink) {
        DELETE("CreateThenExpandCloseThenShrink.xoz");

        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz";
        Repository repo = Repository::create(fpath, true, gp);

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


        // Close and check: the file should be grown
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "            // magic XOZ\0
                "0002 0000 0000 0000 "  // repo_sz
                "0400 0000 0000 0000 "  // trailer_sz
                "0400 0000 "            // blk_total_cnt
                "0000 0000 "            // unused
                "07"                    // blk_sz_order
                "00 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 00c0 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Note the position: 256 is at the end of the last block
        // Also note the length: -1 means read to the end of the file
        // We should read the trailer only proving that the file grow
        // to that position *and* nothing else follows.
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128 * 4, -1,
                // trailer
                "454f 4600"
                );

        // Now "shrink" freeing those 3 blocks
        Repository repo2(SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz");
        repo2.expose_block_array().shrink_by_blocks(3);

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)0);

        ss.str("");
        PrintTo(repo2, &ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug


        // Close and check again: the file should shrank
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        // The position 128 is where the trailer should be present
        // if the file shrank at the "logical" level.
        // With a length of -1 (read to the end of the file), we check
        // that also the file shrank at the "physical" level (in disk)
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateTooSmallBlockSize) {
        DELETE("CreateTooSmallBlockSize.xoz");

        // Too small
        struct Repository::default_parameters_t gp = {
            .blk_sz = 64
        };

        const char* fpath = SCRATCH_HOME "CreateTooSmallBlockSize.xoz";
        EXPECT_THAT(
            [&]() { Repository::create(fpath, true, gp); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("The minimum block size is 128 but given 64.")
                    )
                )
        );
    }

    TEST(RepositoryTest, OpenTooSmallBlockSize) {
        DELETE("OpenTooSmallBlockSize.xoz");

        // Large enough
        struct Repository::default_parameters_t gp = {
            .blk_sz = 128
        };

        const char* fpath = SCRATCH_HOME "OpenTooSmallBlockSize.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);

        // Check repository's parameters after create
        EXPECT_EQ(new_repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(new_repo.expose_block_array().blk_sz(), (uint32_t)128);

        new_repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
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

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        // Now patch the file to make it look to be of a smaller block size
        // at 28th byte, the blk_sz_order is changed to 6 (64 bytes)
        std::fstream f(fpath, std::fstream::in | std::fstream::out | std::fstream::binary);
        f.seekp(28);
        char blk_sz_order = 6;
        f.write(&blk_sz_order, 1);
        f.close();


        // Open, this should fail
        EXPECT_THAT(
            [&]() { Repository repo(SCRATCH_HOME "OpenTooSmallBlockSize.xoz"); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("block size order 6 is out of range [7 to 16] (block sizes of 128 to 64K)")
                    )
                )
        );

    }
}
