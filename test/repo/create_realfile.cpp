#include "xoz/repo/repository.h"
#include "xoz/err/exceptions.h"
#include "xoz/dsc/default.h"

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


#define XOZ_EXPECT_TRAMPOLINE_SERIALIZATION(repo, at, len, data) do {    \
    auto trampoline_io = IOSegment((repo).expose_block_array(), (repo).trampoline_segment());   \
    EXPECT_EQ(hexdump(trampoline_io, (at), (len)), (data));             \
} while (0)

namespace {
    // Create a new repository with default settings.
    // Close it and check the dump of the file.
    //
    // The check of the dump is simplistic: it is only to validate
    // that the .xoz file was created and it is non-empty.
    TEST(RepositoryTest, CreateNewUsingDefaults) {
        DescriptorMapping dmap({});

        DELETE("CreateNewUsingDefaults.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewUsingDefaults.xoz";
        Repository repo = Repository::create(dmap, fpath, true);

        // Check repository's parameters
        // Because we didn't specified anything on Repository::create, it
        // should be using the defaults.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(128+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(128+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and check what we have on disk.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNewNotUsingDefaults) {
        DescriptorMapping dmap({});

        DELETE("CreateNewNotUsingDefaults.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateNewNotUsingDefaults.xoz";
        Repository repo = Repository::create(dmap, fpath, true, gp);

        // Check repository's parameters
        // Because we didn't specified anything on Repository::create, it
        // should be using the defaults.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.header_sz, uint64_t(256));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and check what we have on disk.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "08"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the header ----------

                // 128 bytes of padding to complete the block
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNewUsingDefaultsThenOpen) {
        DescriptorMapping dmap({});

        DELETE("CreateNewUsingDefaultsThenOpen.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewUsingDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(dmap, fpath, true);
        new_repo.close();

        Repository repo(dmap, SCRATCH_HOME "CreateNewUsingDefaultsThenOpen.xoz");

        // Check repository's parameters
        // Because we didn't specified anything on Repository::create, it
        // should be using the defaults.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(128+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(128+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and check that the file in disk still exists
        // Note: in CreateNewUsingDefaults test we create-close-check, here
        // we do create-close-open-close-check.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNotUsingDefaultsThenOpen) {
        DescriptorMapping dmap({});

        DELETE("CreateNotUsingDefaultsThenOpen.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateNotUsingDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(dmap, fpath, true, gp);

        // Check repository's parameters after create
        EXPECT_EQ(new_repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(new_repo.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats = new_repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.header_sz, uint64_t(256));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = new_repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        new_repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "08"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the header ----------

                // 128 bytes of padding to complete the block
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );

        Repository repo(dmap, SCRATCH_HOME "CreateNotUsingDefaultsThenOpen.xoz");

        // Check repository's parameters after open
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats2 = repo.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(256));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        auto root_set2 = repo.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)0);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "08"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the header ----------

                // 128 bytes of padding to complete the block
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateNotUsingDefaultsThenOpenCloseOpen) {
        DescriptorMapping dmap({});

        DELETE("CreateNotUsingDefaultsThenOpenCloseOpen.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateNotUsingDefaultsThenOpenCloseOpen.xoz";
        Repository new_repo = Repository::create(dmap, fpath, true, gp);
        new_repo.close();

        {
            Repository repo(dmap, SCRATCH_HOME "CreateNotUsingDefaultsThenOpenCloseOpen.xoz");

            // Close and reopen again
            repo.close();
        }

        Repository repo(dmap, SCRATCH_HOME "CreateNotUsingDefaultsThenOpenCloseOpen.xoz");

        // Check repository's parameters after open
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.header_sz, uint64_t(256));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "08"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the header ----------

                // 128 bytes of padding to complete the block
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenRecreateAndOverride) {
        DescriptorMapping dmap({});

        DELETE("CreateThenRecreateAndOverride.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateAndOverride.xoz";
        Repository new_repo = Repository::create(dmap, fpath, true, gp);
        new_repo.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        Repository repo = Repository::create(dmap, SCRATCH_HOME "CreateThenRecreateAndOverride.xoz", false);

        // Check repository's parameters after open
        // Because the second Repository::create *did not* create a fresh
        // repository with a default params **but** it opened the previously
        // created repository.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.header_sz, uint64_t(256));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "08"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the header ----------

                // 128 bytes of padding to complete the block
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenRecreateButFail) {
        DescriptorMapping dmap({});

        DELETE("CreateThenRecreateButFail.xoz");

        // Custom non-default parameters
       struct Repository::default_parameters_t  gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateButFail.xoz";
        Repository new_repo = Repository::create(dmap, fpath, true, gp);
        new_repo.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists but instead it will open it
        EXPECT_THAT(
            [&]() { Repository::create(dmap, SCRATCH_HOME "CreateThenRecreateButFail.xoz", true); },
            ThrowsMessage<OpenXOZError>(
                AllOf(
                    HasSubstr("the file already exist and FileBlockArray::create is configured to not override it")
                    )
                )
        );

        // Try to open it again, this time with fail_if_exists == False.
        // Check that the previous failed creation **did not** corrupted the original
        // file
        Repository repo = Repository::create(dmap, SCRATCH_HOME "CreateThenRecreateButFail.xoz", false);

        // Check repository's parameters after open
        // Because the second Repository::create *did not* create a fresh
        // repository with a default params **but** it opened the previously
        // created repository.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(256+4));
        EXPECT_EQ(stats.header_sz, uint64_t(256));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "08"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the header ----------

                // 128 bytes of padding to complete the block
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenExpandByAlloc) {
        DescriptorMapping dmap({});

        DELETE("CreateThenExpandByAlloc.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandByAlloc.xoz";
        Repository repo = Repository::create(dmap, fpath, true);

        const auto blk_sz = repo.expose_block_array().blk_sz();

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto sg1 = repo.expose_block_array().allocator().alloc(blk_sz * 3);
        EXPECT_EQ(sg1.calc_data_space_size(), (uint32_t)(blk_sz * 3));

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);


        // Add 6 more blocks
        auto sg2 = repo.expose_block_array().allocator().alloc(blk_sz * 6);
        EXPECT_EQ(sg2.calc_data_space_size(), (uint32_t)(blk_sz * 6));

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)9);

        // Close. From the repository's allocator perspective, the sg1 and sg2 segments
        // were and they still are allocated so we should see the space allocated
        // after the close.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0005 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0a00 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c85c "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 1280, -1,
                // trailer
                "454f 4600"
                );

        // We open the same file. We expect the repo's blk array to have
        // the same size as the previous one.
        Repository repo2(dmap, SCRATCH_HOME "CreateThenExpandByAlloc.xoz");

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)9);

        // From this second repository, its allocator has no idea that sg1 and sg2 were
        // allocated before. From its perspective, the whole space in the block array
        // has no owner and it is subject to be released on close()
        // (so we expect to see a shrink here)
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        // We open the same file again. We expect the repo's blk array to have
        // the same size as the previous one after the shrink (0 blks in total)
        Repository repo3(dmap, SCRATCH_HOME "CreateThenExpandByAlloc.xoz");

        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)0);

        // Nothing weird should happen. All the "unallocated" space of repo1
        // was released on repo2.close() so repo3.close() has nothing to do.
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenExpandByBlkArrGrow) {
        DescriptorMapping dmap({});

        DELETE("CreateThenExpandByBlkArrGrow.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandByBlkArrGrow.xoz";
        Repository repo = Repository::create(dmap, fpath, true);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.expose_block_array().grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);


        // Add 6 more blocks
        old_top_nr = repo.expose_block_array().grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)4);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)9);

        // Close. These 3+6 additional blocks are not allocated (not owned by any segment)
        // *but*, the repository's allocator does not know that. From its perspective
        // these blocks are *not* free (the allocator tracks free space only) so
        // it will believe that they *are* allocated/owned by someone, hence
        // they will *not* be released on repo.close() and that's what we expect.
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0005 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0a00 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c85c "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 1280, -1,
                // trailer
                "454f 4600"
                );

        // Open the file again. We expect to see that the file grew.
        Repository repo2(dmap, SCRATCH_HOME "CreateThenExpandByBlkArrGrow.xoz");

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)9);

        // Close. Because the allocator does not know that any of these blocks
        // are owned (which are not), it will assume that they are free
        // and repo2.close() will release them, shrinking the file in the process.
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        // We open the same file again. We expect the repo's blk array to have
        // the same size as the previous one after the shrink (0 blks in total)
        Repository repo3(dmap, SCRATCH_HOME "CreateThenExpandByBlkArrGrow.xoz");

        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)0);

        // Nothing weird should happen. All the "unallocated" space of repo1
        // was released on repo2.close() so repo3.close() has nothing to do.
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, CreateThenExpandThenRevertByAlloc) {
        DescriptorMapping dmap({});

        DELETE("CreateThenExpandThenRevertByAlloc.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevertByAlloc.xoz";
        Repository repo = Repository::create(dmap, fpath, true);

        const auto blk_sz = repo.expose_block_array().blk_sz();

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto sg1 = repo.expose_block_array().allocator().alloc(blk_sz * 3);
        EXPECT_EQ(sg1.calc_data_space_size(), (uint32_t)(blk_sz * 3));

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);

        // Now "revert" freeing those 3 blocks
        repo.expose_block_array().allocator().dealloc(sg1);

        // We expect the block array to *not* shrink (but the allocator *is* aware
        // that those blocks are free).
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);

        // Close. We expect to see those blocks released.
        // The allocator is aware that sg1 is free and therefore the blocks owned
        // by it are free. On allocator's release(), it will call to FileBlockArray's release()
        // which in turn it will shrink the file on repo.close()
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        // Reopen.
        Repository repo2(dmap, SCRATCH_HOME "CreateThenExpandThenRevertByAlloc.xoz");

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)0);

        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

    }


    TEST(RepositoryTest, CreateThenExpandThenRevertByBlkArrGrow) {
        DescriptorMapping dmap({});

        DELETE("CreateThenExpandThenRevertByBlkArrGrow.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevertByBlkArrGrow.xoz";
        Repository repo = Repository::create(dmap, fpath, true);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = repo.expose_block_array().grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);

        // Now "revert" freeing those 3 blocks
        repo.expose_block_array().shrink_by_blocks(3);

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);

        // Close. We expect to see those blocks released (because the blk array shrank)
        // This should be handled by repo's FileBlockArray release() only
        // (no need of repo's allocator to be involved)
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        // Reopen.
        Repository repo2(dmap, SCRATCH_HOME "CreateThenExpandThenRevertByBlkArrGrow.xoz");

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)0);

        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, CreateThenExpandCloseThenShrink) {
        DescriptorMapping dmap({});

        DELETE("CreateThenExpandCloseThenShrink.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz";
        Repository repo = Repository::create(dmap, fpath, true);

        const auto blk_sz = repo.expose_block_array().blk_sz();

        // The repository by default has 1 block so adding 9 more
        // will yield 10 blocks in total
        auto sg1 = repo.expose_block_array().allocator().alloc(blk_sz * 9);
        EXPECT_EQ(sg1.calc_data_space_size(), (uint32_t)(blk_sz * 9));

        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)9);

        auto stats1 = repo.expose_block_array().allocator().stats();

        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(9));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(1));

        EXPECT_EQ(stats1.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.dealloc_call_cnt, uint64_t(0));

        // Close and check: the file should be grown
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0005 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0a00 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c85c "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 1280, -1,
                // trailer
                "454f 4600"
                );

        // Reopen the file. The block array will have the same geometry but
        // the allocator will know that the allocated blocks (sg1) are not owned
        // by anyone
        Repository repo2(dmap, SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz");

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)9);

        // Allocate 9 additional blocks. From allocator perspective the 9 original blocks
        // were free to it should use them to fulfil the request of 9 "additional" blocks
        // Hence, the block array (and file) should *not* grow.
        auto sg2 = repo2.expose_block_array().allocator().alloc(blk_sz * 9);
        EXPECT_EQ(sg2.calc_data_space_size(), (uint32_t)(blk_sz * 9));

        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)9);

        auto stats2 = repo2.expose_block_array().allocator().stats();

        EXPECT_EQ(stats2.current.in_use_blk_cnt, uint64_t(9));
        EXPECT_EQ(stats2.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats2.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats2.current.in_use_ext_cnt, uint64_t(1));

        EXPECT_EQ(stats2.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats2.current.dealloc_call_cnt, uint64_t(0));

        // Expected no change respect the previous state
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0005 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0a00 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c85c "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 1280, -1,
                // trailer
                "454f 4600"
                );

        Repository repo3(dmap, SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz");

        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)10);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)9);

        // Alloc a single block. The allocator should try to allocate the lowest
        // extents leaving the blocks with higher blk number free and subject
        // to be released on repo3.close()
        auto sg3 = repo3.expose_block_array().allocator().alloc(blk_sz * 1);
        EXPECT_EQ(sg3.calc_data_space_size(), (uint32_t)(blk_sz * 1));

        auto stats3 = repo3.expose_block_array().allocator().stats();

        EXPECT_EQ(stats3.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats3.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats3.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats3.current.in_use_ext_cnt, uint64_t(1));

        EXPECT_EQ(stats3.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats3.current.dealloc_call_cnt, uint64_t(0));

        // Close and check again: the file should shrank, only 2 blk should survive
        // (the header and the allocated blk of above)
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "c058 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128 * 2, -1,
                // trailer
                "454f 4600"
                );

    }

    TEST(RepositoryTest, CreateTooSmallBlockSize) {
        DescriptorMapping dmap({});

        DELETE("CreateTooSmallBlockSize.xoz");

        // Too small
        struct Repository::default_parameters_t gp = {
            .blk_sz = 64
        };

        const char* fpath = SCRATCH_HOME "CreateTooSmallBlockSize.xoz";
        EXPECT_THAT(
            [&]() { Repository::create(dmap, fpath, true, gp); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("The minimum block size is 128 but given 64.")
                    )
                )
        );
    }

    TEST(RepositoryTest, OpenTooSmallBlockSize) {
        DescriptorMapping dmap({});

        DELETE("OpenTooSmallBlockSize.xoz");

        const char* fpath = SCRATCH_HOME "OpenTooSmallBlockSize.xoz";
        Repository new_repo = Repository::create(dmap, fpath, true);

        // Check repository's parameters after create
        EXPECT_EQ(new_repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(new_repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(new_repo.expose_block_array().blk_sz(), (uint32_t)128);

        new_repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3f58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, -1,
                // trailer
                "454f 4600"
                );

        // Now patch the file to make it look to be of a smaller block size
        // at 30th byte, the blk_sz_order is changed to 6 (64 bytes)
        // We also need to patch the checksum at (30 + 46)
        std::fstream f(fpath, std::fstream::in | std::fstream::out | std::fstream::binary);
        f.seekp(30);
        char patch = 6;
        f.write(&patch, 1);

        f.seekp(30+46);
        patch = 0x3e;
        f.write(&patch, 1);
        f.close();

        // check that we did the patch correctly
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0100 0000 "                     // blk_total_cnt
                "06"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0108 0000 0000 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "3e58 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Open, this should fail
        EXPECT_THAT(
            [&]() { Repository repo(dmap, SCRATCH_HOME "OpenTooSmallBlockSize.xoz"); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("block size order 6 is out of range [7 to 16] (block sizes of 128 to 64K)")
                    )
                )
        );

    }

    TEST(RepositoryTest, TrampolineNotRequired) {
        DescriptorMapping dmap({});

        DELETE("TrampolineNotRequired.xoz");

        const char* fpath = SCRATCH_HOME "TrampolineNotRequired.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        for (char c = 'A'; c <= 'D'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            repo.root()->add(std::move(dscptr));
            repo.root()->full_sync(false);
        }

        // We expect the file has grown 1 block:
        // The reasoning is that the 4 descriptors will fit in a single
        // block thanks to the suballocation
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)4);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and reopen and check again
        // Note how large is the root set due the size of its segment
        // that it was fragmented in several extents due the repeated
        // calls to full_sync
        // However, the set still fits in the header of the xoz file
        // so there is no need of a trampoline
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(dmap, SCRATCH_HOME "TrampolineNotRequired.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats2 = repo2.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(128));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set2 = repo2.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)4);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

        Repository repo3(dmap, SCRATCH_HOME "TrampolineNotRequired.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats3 = repo3.stats();

        EXPECT_EQ(stats3.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.header_sz, uint64_t(128));
        EXPECT_EQ(stats3.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set3 = repo3.root();
        EXPECT_EQ(root_set3->count(), (uint32_t)4);
        EXPECT_EQ(root_set3->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );
    }

    // TODO: this test is disabled because the optimization of
    // "fragmentation avoidance" is not implemented in DescriptorSet
    // so doing a single full_sync ends up doing a lot of tiny
    // allocations anyways.
#if 0
    TEST(RepositoryTest, TrampolineNotRequiredDueFewWrites) {
        DescriptorMapping dmap({});

        DELETE("TrampolineNotRequiredDueFewWrites.xoz");

        const char* fpath = SCRATCH_HOME "TrampolineNotRequiredDueFewWrites.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            repo.root()->add(std::move(dscptr));
        }

        // Perform a single full_sync
        // This should make the set to allocate all the needed space once
        // so its segment will be less fragmented and much smaller than
        // if we do a single alloc per descriptor
        repo.root()->full_sync(false);

        // We expect the file has grown 1 block:
        // The reasoning is that the 26 descriptors will fit in a single
        // block thanks to the suballocation
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)26);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and reopen and check again
        // Note how large is the root set due the size of its segment
        // that it was fragmented in several extents due the repeated
        // calls to full_sync
        // However, the set still fits in the header of the xoz file
        // so there is no need of a trampoline
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "  // <--- TODO

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "fa04 4545 " // desc
                "fa04 4646 " // desc
                "fa04 4747 " // desc
                "fa04 4848 " // desc
                "fa04 4949 " // desc
                "fa04 4a4a " // desc
                "fa04 4b4b " // desc
                "fa04 4c4c " // desc
                "fa04 4d4d " // desc
                "fa04 4e4e " // desc
                "fa04 4f4f " // desc
                "fa04 5050 " // desc
                "fa04 5151 " // desc
                "fa04 5252 " // desc
                "fa04 5353 " // desc
                "fa04 5454 " // desc
                "fa04 5555 " // desc
                "fa04 5656 " // desc
                "fa04 5757 " // desc
                "fa04 5858 " // desc
                "fa04 5959 " // desc
                "fa04 5a5a " // desc 26 ZZ
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(dmap, SCRATCH_HOME "TrampolineNotRequiredDueFewWrites.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats2 = repo2.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(128));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set2 = repo2.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)26);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "fa04 4545 " // desc
                "fa04 4646 " // desc
                "fa04 4747 " // desc
                "fa04 4848 " // desc
                "fa04 4949 " // desc
                "fa04 4a4a " // desc
                "fa04 4b4b " // desc
                "fa04 4c4c " // desc
                "fa04 4d4d " // desc
                "fa04 4e4e " // desc
                "fa04 4f4f " // desc
                "fa04 5050 " // desc
                "fa04 5151 " // desc
                "fa04 5252 " // desc
                "fa04 5353 " // desc
                "fa04 5454 " // desc
                "fa04 5555 " // desc
                "fa04 5656 " // desc
                "fa04 5757 " // desc
                "fa04 5858 " // desc
                "fa04 5959 " // desc
                "fa04 5a5a " // desc 26 ZZ
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );


        Repository repo3(dmap, SCRATCH_HOME "TrampolineNotRequiredDueFewWrites.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats3 = repo3.stats();

        EXPECT_EQ(stats3.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.header_sz, uint64_t(128));
        EXPECT_EQ(stats3.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set3 = repo3.root();
        EXPECT_EQ(root_set3->count(), (uint32_t)26);
        EXPECT_EQ(root_set3->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "fa04 4545 " // desc
                "fa04 4646 " // desc
                "fa04 4747 " // desc
                "fa04 4848 " // desc
                "fa04 4949 " // desc
                "fa04 4a4a " // desc
                "fa04 4b4b " // desc
                "fa04 4c4c " // desc
                "fa04 4d4d " // desc
                "fa04 4e4e " // desc
                "fa04 4f4f " // desc
                "fa04 5050 " // desc
                "fa04 5151 " // desc
                "fa04 5252 " // desc
                "fa04 5353 " // desc
                "fa04 5454 " // desc
                "fa04 5555 " // desc
                "fa04 5656 " // desc
                "fa04 5757 " // desc
                "fa04 5858 " // desc
                "fa04 5959 " // desc
                "fa04 5a5a " // desc 26 ZZ
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

    }
#endif

    TEST(RepositoryTest, TrampolineRequired) {
        DescriptorMapping dmap({});

        DELETE("TrampolineRequired.xoz");

        const char* fpath = SCRATCH_HOME "TrampolineRequired.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            repo.root()->add(std::move(dscptr));
            repo.root()->full_sync(false);
        }

        // We expect the file has grown 1 block:
        // The reasoning is that the 26 descriptors will fit in a single
        // block thanks to the suballocation
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)26);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  ---------------
                "8448 0284 e0ff 00c2 "

                // trampoline padding
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "a95f "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 4b68 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "fa04 4545 " // desc
                "fa04 4646 " // desc
                "fa04 4747 " // desc
                "fa04 4848 " // desc
                "fa04 4949 " // desc
                "fa04 4a4a " // desc
                "fa04 4b4b " // desc
                "fa04 4c4c " // desc
                "fa04 4d4d " // desc
                "fa04 4e4e " // desc
                "fa04 4f4f " // desc
                "fa04 5050 " // desc
                "fa04 5151 " // desc
                "fa04 5252 " // desc
                "fa04 5353 " // desc
                "fa04 5454 " // desc
                "fa04 5555 " // desc
                "fa04 5656 " // desc
                "fa04 5757 " // desc
                "fa04 5858 " // desc
                "fa04 5959 " // desc
                "fa04 5a5a " // desc 26 ZZ
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, 128,
                // second data block

                // root set descriptor -----------
                "0184 7000 "
                "0184 0080 0080 0100 0040 0080 0100 0020 "
                "0080 0100 0010 0080 0100 0008 0080 0100 0004 "
                "0080 0100 0002 0080 0100 0001 0080 0100 "
                "8000 0080 0100 4000 0080 0100 2000 0080 "
                "0100 1000 0080 0100 0800 0080 0100 0400 00c0 "
                // end of root set descriptor --------

                // padding
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(dmap, SCRATCH_HOME "TrampolineRequired.xoz");

        // The repo.close() forced to allocate a trampoline so the blk array
        // should have one additional block
        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)2);
        EXPECT_EQ(repo2.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats2 = repo2.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(128));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set2 = repo2.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)26);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  ---------------
                "8448 0284 e0ff 00c2 "

                // trampoline padding
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "a95f "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 4b68 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "fa04 4545 " // desc
                "fa04 4646 " // desc
                "fa04 4747 " // desc
                "fa04 4848 " // desc
                "fa04 4949 " // desc
                "fa04 4a4a " // desc
                "fa04 4b4b " // desc
                "fa04 4c4c " // desc
                "fa04 4d4d " // desc
                "fa04 4e4e " // desc
                "fa04 4f4f " // desc
                "fa04 5050 " // desc
                "fa04 5151 " // desc
                "fa04 5252 " // desc
                "fa04 5353 " // desc
                "fa04 5454 " // desc
                "fa04 5555 " // desc
                "fa04 5656 " // desc
                "fa04 5757 " // desc
                "fa04 5858 " // desc
                "fa04 5959 " // desc
                "fa04 5a5a " // desc 26 ZZ
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, 128,
                // second data block

                // root set descriptor -----------
                "0184 7000 "
                "0184 0080 0080 0100 0040 0080 0100 0020 "
                "0080 0100 0010 0080 0100 0008 0080 0100 0004 "
                "0080 0100 0002 0080 0100 0001 0080 0100 "
                "8000 0080 0100 4000 0080 0100 2000 0080 "
                "0100 1000 0080 0100 0800 0080 0100 0400 00c0 "
                // end of root set descriptor --------

                // padding
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );

        Repository repo3(dmap, SCRATCH_HOME "TrampolineRequired.xoz");

        // The repo.close() forced to allocate a trampoline so the blk array
        // should have one additional block
        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)2);
        EXPECT_EQ(repo3.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats3 = repo3.stats();

        EXPECT_EQ(stats3.capacity_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats3.in_use_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats3.header_sz, uint64_t(128));
        EXPECT_EQ(stats3.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set3 = repo3.root();
        EXPECT_EQ(root_set3->count(), (uint32_t)26);
        EXPECT_EQ(root_set3->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  ---------------
                "8448 0284 e0ff 00c2 "

                // trampoline padding
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "a95f "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 4b68 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "fa04 4545 " // desc
                "fa04 4646 " // desc
                "fa04 4747 " // desc
                "fa04 4848 " // desc
                "fa04 4949 " // desc
                "fa04 4a4a " // desc
                "fa04 4b4b " // desc
                "fa04 4c4c " // desc
                "fa04 4d4d " // desc
                "fa04 4e4e " // desc
                "fa04 4f4f " // desc
                "fa04 5050 " // desc
                "fa04 5151 " // desc
                "fa04 5252 " // desc
                "fa04 5353 " // desc
                "fa04 5454 " // desc
                "fa04 5555 " // desc
                "fa04 5656 " // desc
                "fa04 5757 " // desc
                "fa04 5858 " // desc
                "fa04 5959 " // desc
                "fa04 5a5a " // desc 26 ZZ
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, 128,
                // second data block

                // root set descriptor -----------
                "0184 7000 "
                "0184 0080 0080 0100 0040 0080 0100 0020 "
                "0080 0100 0010 0080 0100 0008 0080 0100 0004 "
                "0080 0100 0002 0080 0100 0001 0080 0100 "
                "8000 0080 0100 4000 0080 0100 2000 0080 "
                "0100 1000 0080 0100 0800 0080 0100 0400 00c0 "
                // end of root set descriptor --------

                // padding
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, TrampolineRequiredButBeforeCloseItWasNotLongerRequired) {
        DescriptorMapping dmap({});

        DELETE("TrampolineRequiredButBeforeCloseItWasNotLongerRequired.xoz");

        const char* fpath = SCRATCH_HOME "TrampolineRequiredButBeforeCloseItWasNotLongerRequired.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        std::vector<uint32_t> ids;
        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            auto id = repo.root()->add(std::move(dscptr));
            ids.push_back(id);

            repo.root()->full_sync(false);
        }

        // This will flush any pending write and also it will write the header
        // In this step, it is found that the root set does not fit in the header
        // so the header requires a trampoline
        repo.full_sync(true);

        // 3 blocks needed: 1 header, 1 for the descriptors and 1 for the root set
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)26);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Remove from the set all except the first 4 descriptors added
        for (size_t i = 4; i < ids.size(); ++i) {
            repo.root()->erase(ids[i]);
        }

        // Close and reopen and check again. We should expect to see
        // 2 blocks, not 3 and no trampoline
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 f31e " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, TrampolineRequiredThenCloseThenNotLongerRequired) {
        DescriptorMapping dmap({});

        DELETE("TrampolineRequiredThenCloseThenNotLongerRequired.xoz");

        const char* fpath = SCRATCH_HOME "TrampolineRequiredThenCloseThenNotLongerRequired.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        std::vector<uint32_t> ids;
        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            auto id = repo.root()->add(std::move(dscptr), true);
            ids.push_back(id);
            repo.root()->full_sync(false);
        }

        repo.full_sync(true);

        // We expect the file has grown 3 blocks.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 4)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 4)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)26);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0002 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0400 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  --------------
                "eccc " // trampoline checksum
                                   //|-------------| trampoline segment inline data (6 bytes)
                "030c 0086 1e00 00c6 2000 00c0 0000 " // trampoline segment --v
                // 00003 00004 [   1] 00002 [0000000000011110] (+6) (struct: 14 B, data: 160 B)

                // trampoline padding
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "f1b6 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*3,
                // root descriptor set -----------
                // first data block
                "0000 aa9d " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "fa06 0300 0000 4343 " // desc 3 CC
                "fa06 0400 0000 4444 " // desc 4 DD
                "fa06 0500 0000 4545 "
                "fa06 0600 0000 4646 "
                "fa06 0700 0000 4747 "
                "fa06 0800 0000 4848 "
                "fa06 0900 0000 4949 "
                "fa06 0a00 0000 4a4a "
                "fa06 0b00 0000 4b4b "
                "fa06 0c00 0000 4c4c "
                "fa06 0d00 0000 4d4d "
                "fa06 0e00 0000 4e4e "
                "fa06 0f00 0000 4f4f "
                "fa06 1000 " // desc 16 PP
                // second data block
                "0000 5050 " // desc 16 PP (cont)
                "fa06 1100 0000 5151 "
                "fa06 1200 0000 5252 "
                "fa06 1300 0000 5353 "
                "fa06 1400 0000 5454 "
                "fa06 1500 0000 5555 "
                "fa06 1600 0000 5656 "
                "fa06 1700 0000 5757 "
                "fa06 1800 0000 5858 "
                "fa06 1900 0000 5959 "
                "fa06 1a00 0000 5a5a "
                // end of the root descriptor set -----------

                "0000 0000 " // padding

                // trampoline (second part) ---------------------
                "0200 0004 0080 0200 "
                "0002 0080 0200 0001 "
                "0080 0200 8000 0080 "
                "0200 4000 0080 0200 "
                "0000 0000 0000 0000 " // padding (not allocated)
                // end of trampoline (second part) ---------------------

                // trampoline (first part) ---------------------
                // third data block
                "0184 d800 0184 0080 0080 0100 0040 0080 0100 0020 0080 "
                "0100 0010 0080 0100 0008 0080 0100 0004 0080 0100 0002 "
                "0080 0100 0001 0080 0100 8000 0080 0100 4000 0080 0100 "
                "2000 0080 0100 1000 0080 0100 0800 0080 0100 0400 0080 "
                "0100 0200 0080 0100 0100 0084 0080 0080 0200 0040 0080 "
                "0200 0020 0080 0200 0010 0080 0200 0008 0080"
                // end of trampoline (first part) ---------------------
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*4, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(dmap, fpath);

        // The repo.close() forced to allocate a trampoline so the blk array
        // should have one additional block
        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)3);
        EXPECT_EQ(repo2.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats2 = repo2.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t((128 * 4)+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t((128 * 4)+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(128));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set2 = repo2.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)26);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        // Remove from the set all except the first 2 descriptors added
        for (size_t i = 2; i < ids.size(); ++i) {
            repo2.root()->erase(ids[i]);
        }

        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // trampoline padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*1,
                // root descriptor set -----------
                // first data block
                "0000 7a91 " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

        Repository repo3(dmap, fpath);

        // The repo.close() forced to allocate a trampoline so the blk array
        // should have one additional block
        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats3 = repo3.stats();

        EXPECT_EQ(stats3.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.header_sz, uint64_t(128));
        EXPECT_EQ(stats3.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set3 = repo3.root();
        EXPECT_EQ(root_set3->count(), (uint32_t)2);
        EXPECT_EQ(root_set3->does_require_write(), (bool)false);

        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  ---------------
                "0184 1800 0184 0080 0080 0100 0040 0080 0100 0020 00c0 "

                // trampoline padding
                "0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "def8 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*1,
                // root descriptor set -----------
                // first data block
                "0000 7a91 " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, TrampolineRequiredOfDifferentSizes) {
        DescriptorMapping dmap({});

        DELETE("TrampolineRequiredOfDifferentSizes.xoz");

        const char* fpath = SCRATCH_HOME "TrampolineRequiredOfDifferentSizes.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        std::vector<uint32_t> ids;
        for (char c = 'A'; c <= 'Z'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            auto id = repo.root()->add(std::move(dscptr), true);
            ids.push_back(id);
            repo.root()->full_sync(false);
        }

        repo.full_sync(true);

        // We expect the file has grown 3 blocks.
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)3);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 4)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 4)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)26);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Now let's shrink the trampoline removing some descriptors
        // (and reducing the set)
        for (size_t i = 10; i < ids.size(); ++i) {
            repo.root()->erase(ids[i]);
            repo.root()->full_sync(false);
        }

        repo.close();

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  --------------
                "4bc8 " // trampoline checksum
                                   //|-------------| trampoline segment inline data (6 bytes)
                "0284 80ff 00c0 " // trampoline segment --v
                // 00002 [1111111110000000] (+0) (struct: 6 B, data: 72 B)

                // trampoline padding
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "10dd "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*2,
                // root descriptor set -----------
                // first data block
                "0000 b4ff " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "fa06 0300 0000 4343 " // desc 3 CC
                "fa06 0400 0000 4444 " // desc 4 DD
                "fa06 0500 0000 4545 "
                "fa06 0600 0000 4646 "
                "fa06 0700 0000 4747 "
                "fa06 0800 0000 4848 "
                "fa06 0900 0000 4949 "
                "fa06 0a00 0000 4a4a "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 "
                // second data block
                "0184 5800 0184 0080 "
                "0080 0100 0040 0080 "
                "0100 0020 0080 0100 "
                "0010 0080 0100 0008 "
                "0080 0100 0004 0080 "
                "0100 0002 0080 0100 "
                "0001 0080 0100 8000 "
                "0080 0100 4000 0080 "
                "0100 2000 00c0 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                // garbage ? ---------
                "0200 0004 0080 0200 "
                "0002 0080 0200 0001 "
                "0080 0200 8000 0080 "
                "0200 4000 0080 0200 "
                // end of garbage ----
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(dmap, fpath);

        // Check that the set was loaded correctly
        for (int i = 0; i < 10; ++i) {
            char c = char('A' + i);
            auto dscptr = repo2.root()->get<DefaultDescriptor>(ids[i]);
            auto data = dscptr->get_data();
            EXPECT_EQ(data.size(), (size_t)2);
            EXPECT_EQ(data[0], (char)c);
            EXPECT_EQ(data[1], (char)c);
        }

        // Let's shrink the trampoline even further
        for (size_t i = 4; i < 10; ++i) {
            repo2.root()->erase(ids[i]);
            repo2.root()->full_sync(false);
        }

        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  --------------
                "32c0 " // trampoline checksum
                "0184 8007 00c4 00c0 " // trampoline segment --v
                // 00001 [0000011110000000] (+4) (struct: 10 B, data: 32 B)

                // trampoline padding
                "0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "75a0 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*1,
                // root descriptor set -----------
                // first data block
                "0000 fd26 " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "fa06 0300 0000 4343 " // desc 3 CC
                "fa06 0400 0000 4444 " // desc 4 DD
                "0000 0000 "
                // trampoline  ---------------------
                "0184 2800 "
                "0184 0080 0080 0100 "
                "0040 0080 0100 0020 "
                "0080 0100 0010 0080 "
                "0100 0008 "
                //  root set descriptor segment (trampoline content):
                //      00001 [1000000000000000] 00001 [0100000000000000]
                //      00001 [0010000000000000] 00001 [0001000000000000]
                //      00001 [0000100000000000] (struct: 28 B, data: 40 B)
                // end of trampoline  ---------------------
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );


        Repository repo3(dmap, fpath);

        // Check that the set was loaded correctly
        for (int i = 0; i < 4; ++i) {
            char c = char('A' + i);
            auto dscptr = repo3.root()->get<DefaultDescriptor>(ids[i]);
            auto data = dscptr->get_data();
            EXPECT_EQ(data.size(), (size_t)2);
            EXPECT_EQ(data[0], (char)c);
            EXPECT_EQ(data[1], (char)c);
        }

        // Now expand the trampoline adding the erased descriptors back again
        for (size_t i = 4; i < 10; ++i) {
            char c = char('A' + i);
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo3.expose_block_array());
            dscptr->set_data({c, c});

            auto id = repo3.root()->add(std::move(dscptr), true);
            ids[i] = id;
            repo3.root()->full_sync(false);
        }

        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  --------------
                "e9c0 " // trampoline checksum
                "0284 80ff 00c0 " // trampoline segment --v
                // 00002 [1111111110000000] (+0) (struct: 6 B, data: 72 B)

                // trampoline padding
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "aed5 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*2,
                // root descriptor set -----------
                // first data block
                "0000 b4ff " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "fa06 0300 0000 4343 " // desc 3 CC
                "fa06 0400 0000 4444 " // desc 4 DD
                // garbage ------------
                "fa06 0500 0184 2800 "
                "0184 0080 0080 0100 "
                "0040 0080 0100 0020 "
                "0080 0100 0010 0080 "
                // end of garbage ------
                // continuation of the set --
                "0100 0008 0000 4545 "
                "fa06 0600 0000 4646 "
                "fa06 0700 0000 4747 "
                "fa06 0800 0000 4848 "
                "fa06 0900 0000 4949 "
                "fa06 0a00 0000 4a4a "
                // end of the set -----------
                "0000 0000 0000 0000 "
                "0000 0000 "
                // trampoline ----------------
                // second data block
                "0184 5800 0184 0080 "
                "0080 0100 0040 0080 "
                "0100 0020 0080 0100 "
                "0010 0080 0100 0008 "
                "0080 0100 4000 0080 "
                "0100 2000 0080 0100 "
                "1000 0080 0100 0800 "
                "0080 0100 0400 0080 "
                "0100 0200 00c0 0000 "
                //  root set descriptor segment (trampoline content):
                //      00001 [1000000000000000] 00001 [0100000000000000]
                //      00001 [0010000000000000] 00001 [0001000000000000]
                //      00001 [0000100000000000]
                //      00001 [0000000001000000]
                //      00001 [0000000000100000] 00001 [0000000000010000]
                //      00001 [0000000000001000] 00001 [0000000000000100]
                //      00001 [0000000000000010] (struct: 64 B, data: 88 B)
                // end of trampoline  ---------------------
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );

        Repository repo4(dmap, fpath);

        // Check that the set was loaded correctly
        for (int i = 0; i < 10; ++i) {
            char c = char('A' + i);
            auto dscptr = repo4.root()->get<DefaultDescriptor>(ids[i]);
            auto data = dscptr->get_data();
            EXPECT_EQ(data.size(), (size_t)2);
            EXPECT_EQ(data[0], (char)c);
            EXPECT_EQ(data[1], (char)c);
        }

        // Now expand even further
        for (size_t i = 10; i < ids.size(); ++i) {
            char c = char('A' + i);
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo4.expose_block_array());
            dscptr->set_data({c, c});

            auto id = repo4.root()->add(std::move(dscptr), true);
            ids[i] = id;
            repo4.full_sync(true);
        }


        // Close and reopen and check again
        // Note how the descriptor set and the trampoline are getting mixed because the
        // frequently allocations/deallocation and how there is a lot of unallocated space
        // that it is in the middle of the file and it cannot be recovered/reclaimed.
        repo4.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0003 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0600 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline  --------------
                "9475 " // trampoline checksum
                "050c 0286 4025 00c0 " // trampoline segment --v
                // 00005 00006 [   1] 00002 [0010010101000000] (+0) (struct: 8 B, data: 160 B)

                // trampoline padding
                "0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "a0bf "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128*5,
                // root descriptor set (part 1) -----------
                // first data block
                "0000 aa9d " // set's header
                "fa06 0100 0000 4141 " // desc 1 AA
                "fa06 0200 0000 4242 " // desc 2 BB
                "fa06 0300 0000 4343 " // desc 3 CC
                "fa06 0400 0000 4444 " // desc 4 DD
                // set (part 3)  ------------
                "fa06 0500 0000 4b4b "
                "fa06 0c00 0000 4c4c "
                "fa06 0d00 0000 4d4d "
                "fa06 0e00 0000 4e4e "
                // set (part 2) -------------
                "fa06 0f00 0000 4545 "
                "fa06 0600 0000 4646 "
                "fa06 0700 0000 4747 "
                "fa06 0800 0000 4848 "
                "fa06 0900 0000 4949 "
                "fa06 0a00 0000 4a4a "
                // set (part 5) -----------
                "fa06 0b00 0000 4f4f "
                "fa06 1000 "  // desc 20
                // second data block
                "0000 5555 " // desc 20 PP (cont)
                "fa06 1600 0000 5656 "
                "fa06 1700 " // desc 22 (begin)
                "0200 0040 0080 0200 " // not a desc, part of the trampoline
                "0000 5757 " // desc 22 (cont)
                "fa06 1800 0000 5858 "
                "fa06 1900 " // desc 24 (begin)
                "0010 0080 0200 0008 " // not a desc, part of the trampoline
                "0000 5959 " // desc 24 (cont)
                "fa06 1a00 " // desc 25 (begin)
                "0080 0200 0002 0080 " // not a desc, part of the trampoline
                "0000 5a5a " // desc 25 (cont)
                "0000 0000 0200 8000 " // not a desc, part of the trampoline
                "00c0 0000 "
                "0080 0100 0001 0080 "
                "0100 8000 0080 0100 "
                "0000 5050 fa06 1100 " // desc
                "0100 0084 0800 0080 "
                "0000 5151 fa06 1200 " // desc
                "0200 0200 0084 0080 "

                // third data block
                "0000 5252 fa06 1300 " // part of 2 descs, 17 and 18
                "0000 5353 fa06 1400 " // part of 2 descs, 18 and ....
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "

                // fourth data block
                "0184 a800 0184 0080 "
                "0080 0100 0040 0080 "
                "0100 0020 0080 0100 "
                "0010 0080 0100 0008 "
                "0080 0100 4000 0080 "
                "0100 2000 0080 0100 "
                "1000 0080 0100 0800 "
                "0080 0100 0400 0080 "
                "0100 0200 0080 0100 "
                "0004 0080 0100 0002 "
                "0080 0100 0001 0080 "
                "0100 8000 0080 0100 "
                "0100 0084 0800 0080 "
                "0200 0200 0084 0080 "
                "0080 0300 0040 0084 "
                "0000 5454 fa06 1500 "

                // fifth data block
                // trampoline (first part)
                "0184 d800 0184 0080 "
                "0080 0100 0040 0080 "
                "0100 0020 0080 0100 "
                "0010 0080 0100 0008 "
                "0080 0100 4000 0080 "
                "0100 2000 0080 0100 "
                "1000 0080 0100 0800 "
                "0080 0100 0400 0080 "
                "0100 0200 0080 0100 "
                "0004 0080 0100 0002 "
                "0080 0100 0001 0080 "
                "0100 8000 0080 0100 "
                "0100 0084 0800 0080 "
                "0200 0200 0084 0080 "
                "0080 0300 0040 0084 "
                "0100 0186 0080 0080"
                // end of trampoline (first part) ---------------------
                //
                // Trampoline content
                //      00001 [1000000000000000] 00001 [0100000000000000]
                //      00001 [0010000000000000] 00001 [0001000000000000]
                //      00001 [0000100000000000] 00001 [0000000001000000]
                //      00001 [0000000000100000] 00001 [0000000000010000]
                //      00001 [0000000000001000] 00001 [0000000000000100]
                //      00001 [0000000000000010] 00001 [0000010000000000]
                //      00001 [0000001000000000] 00001 [0000000100000000]
                //      00001 [0000000010000000] 00001 [0000000000000001]
                //      00002 [0000000000001000] 00002 [0000000000000010]
                //      00003 [1000000000000000] 00003 [0100000000000000]
                //      00004 [0000000000000001]
                //      00002 [1000000000000000] 00002 [0100000000000000]
                //      00002 [0001000000000000] 00002 [0000100000000000]
                //      00002 [0000001000000000] 00002 [0000000010000000] (struct: 152 B, data: 216 B)
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*6, -1,
                // trailer
                "454f 4600"
                );

        Repository repo5(dmap, fpath);

        // Check that the set was loaded correctly
        for (int i = 0; i < (int)ids.size(); ++i) {
            char c = char('A' + i);
            auto dscptr = repo5.root()->get<DefaultDescriptor>(ids[i]);
            auto data = dscptr->get_data();
            EXPECT_EQ(data.size(), (size_t)2);
            EXPECT_EQ(data[0], (char)c);
            EXPECT_EQ(data[1], (char)c);
        }
    }


    TEST(RepositoryTest, TwoLevelDescriptorSets) {
        DescriptorMapping dmap({});

        DELETE("TwoLevelDescriptorSets.xoz");

        const char* fpath = SCRATCH_HOME "TwoLevelDescriptorSets.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        for (char c = 'A'; c <= 'D'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            repo.root()->add(std::move(dscptr));
            repo.root()->full_sync(false);
        }

        uint32_t dset_id = 0;
        {
            auto dset = DescriptorSet::create(repo.expose_block_array(), repo.expose_runtime_context());
            dset_id = repo.root()->add(std::move(dset));
            repo.root()->full_sync(false);
        }

        auto dset = repo.root()->get<DescriptorSet>(dset_id);
        for (char c = 'E'; c <= 'H'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            dset->add(std::move(dscptr));
            dset->full_sync(false);
        }

        // dset's descriptor changed so root set must be rewritten
        EXPECT_EQ(repo.root()->does_require_write(), (bool)true);
        repo.root()->full_sync(false);

        // We expect the file has grown 1 block:
        // The reasoning is that the 4 descriptors will fit in a single
        // block thanks to the suballocation
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1); // TODO are all of these ok?
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 2)+4)); // TODO are all of these ok?
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)5);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Check allocator stats
        auto stats1 = repo.expose_block_array().allocator().stats();

        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(9));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(8));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        // Close and reopen and check again
        // Note how large is the root set due the size of its segment
        // that it was fragmented in several extents due the repeated
        // calls to full_sync
        // However, the set still fits in the header of the xoz file
        // so there is no need of a trampoline
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline ---------------
                "bab9 0184 7800 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "f592 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 11f5 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "0184 1800 0184 0008 0080 0100 "
                // sub set -----------
                "0000 032f "
                "fa04 4545 "
                "fa04 4646 "
                "fa04 4747 "
                "fa04 4848 "
                "0000 0000 "
                "0004 0080 0100 0002 00c0 "
                "0000 0000 0000 "
                "0184 3000 0184 0080 0080 0100 0040 0080 0100 0020 0080 0100 0010 0080 0100 8001 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

        Repository repo2(dmap, SCRATCH_HOME "TwoLevelDescriptorSets.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats2 = repo2.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(128));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set2 = repo2.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)5);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        // Check the descriptors in the root set. Save the newly assigned id
        // of the subset (because we added this subset without explicitly requiring
        // a persistent id, the value of dset_id variable is useless, hence,
        // we need to find the new one)
        dset_id = 0;
        for (auto it = root_set2->cbegin(); it != root_set2->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(true);
            if (dsc) {
                EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
                EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
            } else {
                auto dsc2 = (*it)->cast<DescriptorSet>(false); // throw if it is not a set as expected
                dset_id = dsc2->id();
            }
        }
        EXPECT_NE(dset_id, (uint32_t)0);

        auto dset2 = repo2.root()->get<DescriptorSet>(dset_id);
        EXPECT_EQ(dset2->count(), (uint32_t)4);
        EXPECT_EQ(dset2->does_require_write(), (bool)false);

        for (auto it = dset2->cbegin(); it != dset2->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(false); // throw if it is not the expected class
            EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
            EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
        }

        // Check allocator stats: see that we correctly recovered the state of used/free blocks
        // plus the extra space required for the trampoline (see the +n values below)
        auto al_stats2 = repo2.expose_block_array().allocator().stats();

        EXPECT_EQ(al_stats2.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(al_stats2.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(al_stats2.current.in_use_subblk_cnt, uint64_t(9 + 4));

        EXPECT_EQ(al_stats2.current.in_use_ext_cnt, uint64_t(8 + 1));
        EXPECT_EQ(al_stats2.current.in_use_inlined_sz, uint64_t(0 + 4));

        // Close and reopen and check again
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline ---------------
                "bab9 0184 7800 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "f592 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 11f5 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "0184 1800 0184 0008 0080 0100 "
                // sub set -----------
                "0000 032f "
                "fa04 4545 "
                "fa04 4646 "
                "fa04 4747 "
                "fa04 4848 "
                "0000 0000 "
                "0004 0080 0100 0002 00c0 "
                "0000 0000 0000 "
                "0184 3000 0184 0080 0080 0100 0040 0080 0100 0020 0080 0100 0010 0080 0100 8001 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );

        Repository repo3(dmap, SCRATCH_HOME "TwoLevelDescriptorSets.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats3 = repo3.stats();

        EXPECT_EQ(stats3.capacity_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats3.header_sz, uint64_t(128));
        EXPECT_EQ(stats3.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set3 = repo3.root();
        EXPECT_EQ(root_set3->count(), (uint32_t)5);
        EXPECT_EQ(root_set3->does_require_write(), (bool)false);

        // Same checks than made for repo2 but this time for repo3
        dset_id = 0;
        for (auto it = root_set3->cbegin(); it != root_set3->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(true);
            if (dsc) {
                EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
                EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
            } else {
                auto dsc2 = (*it)->cast<DescriptorSet>(false); // throw if it is not a set as expected
                dset_id = dsc2->id();
            }
        }
        EXPECT_NE(dset_id, (uint32_t)0);

        auto dset3 = repo3.root()->get<DescriptorSet>(dset_id);
        EXPECT_EQ(dset3->count(), (uint32_t)4);
        EXPECT_EQ(dset3->does_require_write(), (bool)false);

        for (auto it = dset3->cbegin(); it != dset3->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(false); // throw if it is not the expected class
            EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
            EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
        }

        // Check allocator stats: no change should had happen
        auto al_stats3 = repo3.expose_block_array().allocator().stats();

        EXPECT_EQ(al_stats3.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(al_stats3.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(al_stats3.current.in_use_subblk_cnt, uint64_t(9 + 4));

        EXPECT_EQ(al_stats3.current.in_use_ext_cnt, uint64_t(8 + 1));
        EXPECT_EQ(al_stats3.current.in_use_inlined_sz, uint64_t(0 + 4));

        // Close and reopen and check again
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // trampoline ---------------
                "bab9 0184 7800 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "f592 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128,
                // first data block

                // root descriptor set -----------
                "0000 11f5 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                "0184 1800 0184 0008 0080 0100 "
                // sub set -----------
                "0000 032f "
                "fa04 4545 "
                "fa04 4646 "
                "fa04 4747 "
                "fa04 4848 "
                "0000 0000 "
                "0004 0080 0100 0002 00c0 "
                "0000 0000 0000 "
                "0184 3000 0184 0080 0080 0100 0040 0080 0100 0020 0080 0100 0010 0080 0100 8001 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, ThreeLevelDescriptorSets) {
        DescriptorMapping dmap({});

        DELETE("ThreeLevelDescriptorSets.xoz");

        const char* fpath = SCRATCH_HOME "ThreeLevelDescriptorSets.xoz";
        Repository repo = Repository::create(dmap, fpath, true);
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x0, // let DescriptorSet::add assign an id for us

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        for (char c = 'A'; c <= 'D'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            repo.root()->add(std::move(dscptr));
            repo.root()->full_sync(false);
        }

        uint32_t dset_id = 0;
        uint32_t l2dset_id = 0;
        {
            auto dset = DescriptorSet::create(repo.expose_block_array(), repo.expose_runtime_context());
            dset_id = repo.root()->add(std::move(dset));

            auto l2dset = DescriptorSet::create(repo.expose_block_array(), repo.expose_runtime_context());
            l2dset_id = repo.root()->get<DescriptorSet>(dset_id)->add(std::move(l2dset));

            // sync
            repo.root()->full_sync(false);
        }

        auto dset = repo.root()->get<DescriptorSet>(dset_id);
        for (char c = 'E'; c <= 'H'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            dset->add(std::move(dscptr));
            dset->full_sync(false);
        }

        auto l2dset = dset->get<DescriptorSet>(l2dset_id);
        for (char c = 'I'; c <= 'K'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            l2dset->add(std::move(dscptr));
            l2dset->full_sync(false);
        }

        // dset's descriptor changed so root set must be rewritten
        EXPECT_EQ(repo.root()->does_require_write(), (bool)true);
        repo.root()->full_sync(false);

        EXPECT_EQ(repo.root()->does_require_write(), (bool)false);
        EXPECT_EQ(repo.root()->get<DescriptorSet>(dset_id)->does_require_write(), (bool)false);
        EXPECT_EQ(repo.root()->get<DescriptorSet>(dset_id)->get<DescriptorSet>(l2dset_id)->does_require_write(), (bool)false);

        // We expect the file has grown 1 block:
        // The reasoning is that the 4 descriptors will fit in a single
        // block thanks to the suballocation
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1); // TODO are all of these ok?
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 2)+4)); // TODO are all of these ok?
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = repo.root();
        EXPECT_EQ(root_set->count(), (uint32_t)5);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        auto al_stats = repo.expose_block_array().allocator().stats();

        EXPECT_EQ(al_stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(al_stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(al_stats.current.in_use_subblk_cnt, uint64_t(15));

        EXPECT_EQ(al_stats.current.in_use_ext_cnt, uint64_t(12));
        EXPECT_EQ(al_stats.current.in_use_inlined_sz, uint64_t(0));

        // Close and reopen and check again
        // Note how large is the root set due the size of its segment
        // that it was fragmented in several extents due the repeated
        // calls to full_sync
        // However, the set still fits in the header of the xoz file
        // so there is no need of a trampoline
        repo.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "48ae 0284 00f0 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 "
                "0000 "
                // end of the root set descriptor ----

                // checksum
                "8e77 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128 * 2,
                // first data block
                // root descriptor set -----------
                "0000 c500 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0184 3000 0000 7877 0000 0000 0000 "
                "fa04 4545 "
                "fa04 "
                "0184 0010 0080 0100 0008 0080 0100 0001 "
                      "4646 "
                "fa04 4747 "
                "fa04 4848 "
                "0184 1000 0184 0000 cced "
                "fa04 4949 "
                "fa04 4a4a "
                "fa04 4b4b "
                "4000 0080 0100 2000 00c0 "
                "0000 0000 0000 "
                "0080 0100 8000 0080 0100 1800 00c0 "
                "0000 0000 0000 0000 0000 "
                "0184 3800 0184 0080 0080 0100 0040 0080 0100 0020 0080 0100 0006 0080 0100 0600 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );
        Repository repo2(dmap, SCRATCH_HOME "ThreeLevelDescriptorSets.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo2.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo2.expose_block_array().past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(repo2.expose_block_array().blk_cnt(), (uint32_t)2);
        EXPECT_EQ(repo2.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats2 = repo2.stats();

        EXPECT_EQ(stats2.capacity_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats2.in_use_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats2.header_sz, uint64_t(128));
        EXPECT_EQ(stats2.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set2 = repo2.root();
        EXPECT_EQ(root_set2->count(), (uint32_t)5);
        EXPECT_EQ(root_set2->does_require_write(), (bool)false);

        // Check the descriptors in the root set. Save the newly assigned id
        // of the subset (because we added this subset without explicitly requiring
        // a persistent id, the value of dset_id variable is useless, hence,
        // we need to find the new one)
        dset_id = 0;
        for (auto it = root_set2->cbegin(); it != root_set2->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(true);
            if (dsc) {
                EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
                EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
            } else {
                auto dsc2 = (*it)->cast<DescriptorSet>(false); // throw if it is not a set as expected
                dset_id = dsc2->id();
            }
        }
        EXPECT_NE(dset_id, (uint32_t)0);

        auto dset2 = repo2.root()->get<DescriptorSet>(dset_id);
        EXPECT_EQ(dset2->count(), (uint32_t)5);
        EXPECT_EQ(dset2->does_require_write(), (bool)false);

        l2dset_id = 0;
        for (auto it = dset2->cbegin(); it != dset2->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(true);
            if (dsc) {
                EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
                EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
            } else {
                auto dsc2 = (*it)->cast<DescriptorSet>(false); // throw if it is not a set as expected
                l2dset_id = dsc2->id();
            }
        }

        EXPECT_NE(l2dset_id, (uint32_t)0);

        auto l2dset2 = dset2->get<DescriptorSet>(l2dset_id);
        EXPECT_EQ(l2dset2->count(), (uint32_t)3);
        EXPECT_EQ(l2dset2->does_require_write(), (bool)false);

        for (auto it = l2dset2->cbegin(); it != l2dset2->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(false); // throw if it is not the expected class
            EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
            EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
        }

        // Check allocator stats: expected to see the same values seen for <repo>
        // plus the ones due the trampoline
        auto al_stats2 = repo2.expose_block_array().allocator().stats();

        EXPECT_EQ(al_stats2.current.in_use_blk_cnt, uint64_t(1 + 1));
        EXPECT_EQ(al_stats2.current.in_use_blk_for_suballoc_cnt, uint64_t(1 + 1));
        EXPECT_EQ(al_stats2.current.in_use_subblk_cnt, uint64_t(15 + 4));

        EXPECT_EQ(al_stats2.current.in_use_ext_cnt, uint64_t(12 + 1));
        EXPECT_EQ(al_stats2.current.in_use_inlined_sz, uint64_t(0 + 4));

        // Close and reopen and check again
        repo2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "48ae 0284 00f0 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 "
                "0000 "
                // end of the root set descriptor ----

                // checksum
                "8e77 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "48ae 0284 00f0 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 "
                "0000 "
                // end of the root set descriptor ----

                // checksum
                "8e77 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128 * 2,
                // first data block
                // root descriptor set -----------
                "0000 c500 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0184 3000 0000 7877 0000 0000 0000 "
                "fa04 4545 "
                "fa04 "
                "0184 0010 0080 0100 0008 0080 0100 0001 "
                      "4646 "
                "fa04 4747 "
                "fa04 4848 "
                "0184 1000 0184 0000 cced "
                "fa04 4949 "
                "fa04 4a4a "
                "fa04 4b4b "
                "4000 0080 0100 2000 00c0 "
                "0000 0000 0000 "
                "0080 0100 8000 0080 0100 1800 00c0 "
                "0000 0000 0000 0000 0000 "
                "0184 3800 0184 0080 0080 0100 0040 0080 0100 0020 0080 0100 0006 0080 0100 0600 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );

        Repository repo3(dmap, SCRATCH_HOME "ThreeLevelDescriptorSets.xoz");

        // We expect the file has grown
        EXPECT_EQ(repo3.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo3.expose_block_array().past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(repo3.expose_block_array().blk_cnt(), (uint32_t)2);
        EXPECT_EQ(repo3.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats3 = repo3.stats();

        EXPECT_EQ(stats3.capacity_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats3.in_use_repo_sz, uint64_t((128 * 3)+4));
        EXPECT_EQ(stats3.header_sz, uint64_t(128));
        EXPECT_EQ(stats3.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set3 = repo3.root();
        EXPECT_EQ(root_set3->count(), (uint32_t)5);
        EXPECT_EQ(root_set3->does_require_write(), (bool)false);

        // Same checks than made for repo2 but this time for repo3
        dset_id = 0;
        for (auto it = root_set3->cbegin(); it != root_set3->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(true);
            if (dsc) {
                EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
                EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
            } else {
                auto dsc2 = (*it)->cast<DescriptorSet>(false); // throw if it is not a set as expected
                dset_id = dsc2->id();
            }
        }
        EXPECT_NE(dset_id, (uint32_t)0);

        auto dset3 = repo3.root()->get<DescriptorSet>(dset_id);
        EXPECT_EQ(dset3->count(), (uint32_t)5);
        EXPECT_EQ(dset3->does_require_write(), (bool)false);

        l2dset_id = 0;
        for (auto it = dset3->cbegin(); it != dset3->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(true);
            if (dsc) {
                EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
                EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
            } else {
                auto dsc2 = (*it)->cast<DescriptorSet>(false); // throw if it is not a set as expected
                l2dset_id = dsc2->id();
            }
        }

        EXPECT_NE(l2dset_id, (uint32_t)0);

        auto l2dset3 = dset3->get<DescriptorSet>(l2dset_id);
        EXPECT_EQ(l2dset3->count(), (uint32_t)3);
        EXPECT_EQ(l2dset3->does_require_write(), (bool)false);

        for (auto it = l2dset3->cbegin(); it != l2dset3->cend(); ++it) {
            auto dsc = (*it)->cast<DefaultDescriptor>(false); // throw if it is not the expected class
            EXPECT_EQ(dsc->get_data().size(), (uint32_t)2);
            EXPECT_EQ(dsc->get_data()[0], dsc->get_data()[1]);
        }

        // Check allocator stats: expected no change
        auto al_stats3 = repo3.expose_block_array().allocator().stats();

        EXPECT_EQ(al_stats3.current.in_use_blk_cnt, uint64_t(1 + 1));
        EXPECT_EQ(al_stats3.current.in_use_blk_for_suballoc_cnt, uint64_t(1 + 1));
        EXPECT_EQ(al_stats3.current.in_use_subblk_cnt, uint64_t(15 + 4));

        EXPECT_EQ(al_stats3.current.in_use_ext_cnt, uint64_t(12 + 1));
        EXPECT_EQ(al_stats3.current.in_use_inlined_sz, uint64_t(0 + 4));

        // Close and reopen and check again
        repo3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "48ae 0284 00f0 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 "
                "0000 "
                // end of the root set descriptor ----

                // checksum
                "8e77 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8001 0000 0000 0000 "           // repo_sz
                "0400 "                          // trailer_sz
                "0300 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "80 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "48ae 0284 00f0 00c4 00c0 "

                // padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 "
                "0000 "
                // end of the root set descriptor ----

                // checksum
                "8e77 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128, 128 * 2,
                // first data block
                // root descriptor set -----------
                "0000 c500 " // set's header
                "fa04 4141 " // desc 1 AA
                "fa04 4242 " // desc 2 BB
                "fa04 4343 " // desc 3 CC
                "fa04 4444 " // desc 4 DD
                // end of root descriptor set -----------
                "0184 3000 0000 7877 0000 0000 0000 "
                "fa04 4545 "
                "fa04 "
                "0184 0010 0080 0100 0008 0080 0100 0001 "
                      "4646 "
                "fa04 4747 "
                "fa04 4848 "
                "0184 1000 0184 0000 cced "
                "fa04 4949 "
                "fa04 4a4a "
                "fa04 4b4b "
                "4000 0080 0100 2000 00c0 "
                "0000 0000 0000 "
                "0080 0100 8000 0080 0100 1800 00c0 "
                "0000 0000 0000 0000 0000 "
                "0184 3800 0184 0080 0080 0100 0040 0080 0100 0020 0080 0100 0006 0080 0100 0600 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 128*3, -1,
                // trailer
                "454f 4600"
                );
    }
}

