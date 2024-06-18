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
    TEST(RepositoryTest, MemCreateNewUsingDefaults) {

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        Repository repo = Repository::create_mem_based();

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
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 128, -1,
                // trailer
                "454f 4600"
                );
    }


    TEST(RepositoryTest, MemCreateNotUsingDefaults) {

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        // Custom non-default parameters
        struct Repository::default_parameters_t gp = {
            .blk_sz = 256
        };
        Repository repo = Repository::create_mem_based(gp);

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
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 256,
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, MemCreateAddDescThenExpandExplicitWrite) {

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        Repository repo = Repository::create_mem_based();
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
        dscptr->set_data({'A', 'B'});

        repo.root()->set()->add(std::move(dscptr));

        // Explicit write
        repo.root()->set()->write_set();

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

        // Close and reopen and check again
        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 128 * 2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, MemCreateAddDescThenExpandImplicitWrite) {

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        Repository repo = Repository::create_mem_based();
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
        dscptr->set_data({'A', 'B'});

        // Add a descriptor to the set but do not write the set. Let repo.close() to do it.
        repo.root()->set()->add(std::move(dscptr));

        // We expect the file has *not* grown
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t((128 * 1)+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t((128 * 1)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was modified but not written: we expect
        // the set to require another write.
        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)1);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)true);

        // Close the repo. This should imply a write of the set.
        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 128 * 2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, MemCreateThenExpandThenRevertExpectShrinkOnClose) {

        std::map<uint16_t, descriptor_create_fn> descriptors_map = {
            {0x01, DescriptorSetHolder::create }
        };
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        Repository repo = Repository::create_mem_based();
        const auto blk_sz_order = repo.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo.expose_block_array());
        dscptr->set_data({'A', 'B'});

        // Add a descriptor to the set and write it.
        auto id1 = repo.root()->set()->add(std::move(dscptr));
        repo.root()->set()->write_set();

        // Now, remove it.
        repo.root()->set()->erase(id1);
        repo.root()->set()->write_set();

        // Check repository's parameters: the blk array *should* be larger
        // than the initial size
        EXPECT_EQ(repo.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(repo.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = repo.stats();

        EXPECT_EQ(stats.capacity_repo_sz, uint64_t(128*2+4));
        EXPECT_EQ(stats.in_use_repo_sz, uint64_t(128*2+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_holder = repo.root();
        EXPECT_EQ(root_holder->set()->count(), (uint32_t)0);
        EXPECT_EQ(root_holder->set()->does_require_write(), (bool)false);

        // Close and check what we have on disk. Because the descriptor set
        // has some erased data, we can shrink the file during the close.
        repo.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 0, 128,
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(repo, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(RepositoryTest, MemCreateTooSmallBlockSize) {
        // Too small
        struct Repository::default_parameters_t gp = {
            .blk_sz = 64
        };

        EXPECT_THAT(
            [&]() { Repository::create_mem_based(gp); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("The minimum block size is 128 but given 64.")
                    )
                )
        );
    }
}
