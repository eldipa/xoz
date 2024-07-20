#include "xoz/file/file.h"
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

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

#define XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, at, len, data) do {           \
    EXPECT_EQ(hexdump((xfile).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    // Create a new xoz file with default settings.
    // Close it and check the dump of the file.
    //
    // The check of the dump is simplistic: it is only to validate
    // that the .xoz file was created and it is non-empty.
    TEST(FileTest, MemCreateNewUsingDefaults) {
        DescriptorMapping dmap({});

        File xfile = File::create_mem_based(dmap);

        // Check xoz file's parameters
        // Because we didn't specified anything on File::create, it
        // should be using the defaults.
        EXPECT_EQ(xfile.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(xfile.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = xfile.stats();

        EXPECT_EQ(stats.capacity_file_sz, uint64_t(128+4));
        EXPECT_EQ(stats.in_use_file_sz, uint64_t(128+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = xfile.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and check what we have on disk.
        xfile.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // file_sz
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(FileTest, MemCreateNotUsingDefaults) {
        DescriptorMapping dmap({});

        // Custom non-default parameters
        struct File::default_parameters_t gp = {
            .blk_sz = 256
        };
        File xfile = File::create_mem_based(dmap, gp);

        EXPECT_EQ(xfile.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(xfile.expose_block_array().blk_sz(), (uint32_t)256);

        auto stats = xfile.stats();

        EXPECT_EQ(stats.capacity_file_sz, uint64_t(256+4));
        EXPECT_EQ(stats.in_use_file_sz, uint64_t(256+4));
        EXPECT_EQ(stats.header_sz, uint64_t(256));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = xfile.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and check what we have on disk.
        xfile.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 0, 256,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // file_sz
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 256, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(FileTest, MemCreateAddDescThenExpandExplicitWrite) {
        DescriptorMapping dmap({});

        File xfile = File::create_mem_based(dmap);
        const auto blk_sz_order = xfile.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, xfile.expose_block_array());
        dscptr->set_data({'A', 'B'});

        xfile.root()->add(std::move(dscptr));

        // Explicit write
        xfile.root()->full_sync(false);

        // We expect the file has grown
        EXPECT_EQ(xfile.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(xfile.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = xfile.stats();

        EXPECT_EQ(stats.capacity_file_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.in_use_file_sz, uint64_t((128 * 2)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was explicitly written above, we don't expect
        // the set to require another write.
        auto root_set = xfile.root();
        EXPECT_EQ(root_set->count(), (uint32_t)1);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and reopen and check again
        xfile.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // file_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 0800 0184 0080 00c0 "

                // padding
                "0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "cb98 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 128 * 2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(FileTest, MemCreateAddDescThenExpandImplicitWrite) {
        DescriptorMapping dmap({});

        File xfile = File::create_mem_based(dmap);
        const auto blk_sz_order = xfile.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, xfile.expose_block_array());
        dscptr->set_data({'A', 'B'});

        // Add a descriptor to the set but do not write the set. Let xfile.close() to do it.
        xfile.root()->add(std::move(dscptr));

        // We expect the file has *not* grown
        EXPECT_EQ(xfile.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().blk_cnt(), (uint32_t)0);
        EXPECT_EQ(xfile.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = xfile.stats();

        EXPECT_EQ(stats.capacity_file_sz, uint64_t((128 * 1)+4));
        EXPECT_EQ(stats.in_use_file_sz, uint64_t((128 * 1)+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        // The set was modified but not written: we expect
        // the set to require another write.
        auto root_set = xfile.root();
        EXPECT_EQ(root_set->count(), (uint32_t)1);
        EXPECT_EQ(root_set->does_require_write(), (bool)true);

        // Close the xfile. This should imply a write of the set.
        xfile.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "0001 0000 0000 0000 "           // file_sz
                "0400 "                          // trailer_sz
                "0200 0000 "                     // blk_total_cnt
                "07"                             // blk_sz_order
                "00 "                            // flags
                "0000 0000 "                     // feature_flags_compat
                "0000 0000 "                     // feature_flags_incompat
                "0000 0000 "                     // feature_flags_ro_compat

                // root set descriptor ---------------
                "0184 0800 0184 0080 00c0 "

                // padding
                "0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                // end of the root set descriptor ----

                // checksum
                "cb98 "

                // header padding
                "0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 128 * 2, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(FileTest, MemCreateThenExpandThenRevertExpectShrinkOnClose) {
        DescriptorMapping dmap({});

        File xfile = File::create_mem_based(dmap);
        const auto blk_sz_order = xfile.expose_block_array().blk_sz_order();

        // Add one descriptor
        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xfa,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(blk_sz_order)
        };

        auto dscptr = std::make_unique<DefaultDescriptor>(hdr, xfile.expose_block_array());
        dscptr->set_data({'A', 'B'});

        // Add a descriptor to the set and write it.
        auto id1 = xfile.root()->add(std::move(dscptr));
        xfile.root()->full_sync(false);

        // Now, remove it.
        xfile.root()->erase(id1);
        xfile.root()->full_sync(false);

        // Check xoz file's parameters: the blk array *should* be larger
        // than the initial size
        EXPECT_EQ(xfile.expose_block_array().begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(xfile.expose_block_array().blk_cnt(), (uint32_t)1);
        EXPECT_EQ(xfile.expose_block_array().blk_sz(), (uint32_t)128);

        auto stats = xfile.stats();

        EXPECT_EQ(stats.capacity_file_sz, uint64_t(128*2+4));
        EXPECT_EQ(stats.in_use_file_sz, uint64_t(128*2+4));
        EXPECT_EQ(stats.header_sz, uint64_t(128));
        EXPECT_EQ(stats.trailer_sz, uint64_t(4));

        auto root_set = xfile.root();
        EXPECT_EQ(root_set->count(), (uint32_t)0);
        EXPECT_EQ(root_set->does_require_write(), (bool)false);

        // Close and check what we have on disk. Because the descriptor set
        // has some erased data, we can shrink the file during the close.
        xfile.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 0, 128,
                // header
                "584f 5a00 "                     // magic XOZ\0
                "0000 0000 0000 0000 0000 0000 " // app_name
                "8000 0000 0000 0000 "           // file_sz
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

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(xfile, 128, -1,
                // trailer
                "454f 4600"
                );
    }

    TEST(FileTest, MemCreateTooSmallBlockSize) {
        DescriptorMapping dmap({});

        // Too small
        struct File::default_parameters_t gp = {
            .blk_sz = 64
        };

        EXPECT_THAT(
            [&]() { File::create_mem_based(dmap, gp); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("The minimum block size is 128 but given 64.")
                    )
                )
        );
    }
}
