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

namespace {
    // Create a new repository with default settings.
    // Close it and check the dump of the file.
    //
    // The check of the dump is simplistic: it is only to validate
    // that the .xoz file was created and it is non-empty.
    TEST(RepositoryTest, CreateNewUsingDefaults) {
        DELETE("CreateNewUsingDefaults.xoz");

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);


        const char* fpath = SCRATCH_HOME "CreateNewUsingDefaults.xoz";
        Repository repo = Repository::create(fpath, true);

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

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateNewNotUsingDefaults.xoz");

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateNewNotUsingDefaults.xoz";
        Repository repo = Repository::create(fpath, true, gp);

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

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateNewUsingDefaultsThenOpen.xoz");

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        const char* fpath = SCRATCH_HOME "CreateNewUsingDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(fpath, true);
        new_repo.close();

        Repository repo(SCRATCH_HOME "CreateNewUsingDefaultsThenOpen.xoz");

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

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateNotUsingDefaultsThenOpen.xoz");

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateNotUsingDefaultsThenOpen.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);

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

        auto root_holder = new_repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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

        Repository repo(SCRATCH_HOME "CreateNotUsingDefaultsThenOpen.xoz");

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

        auto root_holder2 = repo.root();
        EXPECT_EQ(root_holder2->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder2->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateNotUsingDefaultsThenOpenCloseOpen.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateNotUsingDefaultsThenOpenCloseOpen.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);
        new_repo.close();

        {
            Repository repo(SCRATCH_HOME "CreateNotUsingDefaultsThenOpenCloseOpen.xoz");

            // Close and reopen again
            repo.close();
        }

        Repository repo(SCRATCH_HOME "CreateNotUsingDefaultsThenOpenCloseOpen.xoz");

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

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenRecreateAndOverride.xoz");

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };

        const char* fpath = SCRATCH_HOME "CreateThenRecreateAndOverride.xoz";
        Repository new_repo = Repository::create(fpath, true, gp);
        new_repo.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        Repository repo = Repository::create(SCRATCH_HOME "CreateThenRecreateAndOverride.xoz", false);

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

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenRecreateButFail.xoz");

        // Custom non-default parameters
       struct Repository::default_parameters_t  gp = {
            .blk_sz = 256
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

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenExpandByAlloc.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandByAlloc.xoz";
        Repository repo = Repository::create(fpath, true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo2(SCRATCH_HOME "CreateThenExpandByAlloc.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo3(SCRATCH_HOME "CreateThenExpandByAlloc.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenExpandByBlkArrGrow.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandByBlkArrGrow.xoz";
        Repository repo = Repository::create(fpath, true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo2(SCRATCH_HOME "CreateThenExpandByBlkArrGrow.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo3(SCRATCH_HOME "CreateThenExpandByBlkArrGrow.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenExpandThenRevertByAlloc.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevertByAlloc.xoz";
        Repository repo = Repository::create(fpath, true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo2(SCRATCH_HOME "CreateThenExpandThenRevertByAlloc.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenExpandThenRevertByBlkArrGrow.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevertByBlkArrGrow.xoz";
        Repository repo = Repository::create(fpath, true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo2(SCRATCH_HOME "CreateThenExpandThenRevertByBlkArrGrow.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        DELETE("CreateThenExpandCloseThenShrink.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz";
        Repository repo = Repository::create(fpath, true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
        Repository repo2(SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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

        Repository repo3(SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz");

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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

        const char* fpath = SCRATCH_HOME "OpenTooSmallBlockSize.xoz";
        Repository new_repo = Repository::create(fpath, true);

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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

                // root holder ---------------
                "0108 0000 0000 "

                // holder padding
                "0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

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
            [&]() { Repository repo(SCRATCH_HOME "OpenTooSmallBlockSize.xoz"); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("block size order 6 is out of range [7 to 16] (block sizes of 128 to 64K)")
                    )
                )
        );

    }

    TEST(RepositoryTest, TrampolineRequired) {
        DELETE("TrampolineRequired.xoz");

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        const char* fpath = SCRATCH_HOME "TrampolineRequired.xoz";
        Repository repo = Repository::create(fpath, true);
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

        for (char c = 'A'; c <= 'C'; ++c) {
            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
            dscptr->set_data({c, c});

            repo.root()->set()->add(std::move(dscptr));
            repo.root()->set()->flush_writes();
        }

        // We expect the file has grown
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
        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)1);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)false);

        std::cout << repo.root()->set()->segment() << '\n';

        // Close and reopen and check again
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

                // root holder ---------------
                "0184 0800 0184 0080 00c0 "

                // holder padding
                "0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

                // checksum
                "cb98 "

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

#if 0
        Repository repo2(SCRATCH_HOME "TrampolineRequired.xoz");

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
        auto root_holder2 = repo2.root();
        EXPECT_EQ(root_holder2->set()->count(), (uint32_t)1);
        EXPECT_EQ(root_holder2->set()->does_require_write(), (bool)false);

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

                // root holder ---------------
                "0184 0800 0184 0080 00c0 "

                // holder padding
                "0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root holder ----

                // checksum
                "cb98 "

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
#endif
    }

    // no trampoline -> with trampoline + descriptors (tramp allocated)
    // no trampoline -> with trampoline + descriptors -> no trampoline + descriptors (tramp dealloc, no leak)
    // no trampoline -> with trampoline + descriptors -> with other, more larger trampoline + descriptors (tramp realloc, no leak)
    // no trampoline -> with trampoline but too large to fit in header, so it is reallocated as a single extent
}
