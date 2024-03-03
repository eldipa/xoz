#include "xoz/blk/file_block_array.h"
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
using ::testing_xoz::helpers::are_all_zeros;

#define XOZ_EXPECT_FILE_SERIALIZATION(path, at, len, data) do {           \
    EXPECT_EQ(hexdump(file2mem(path), (at), (len)), (data));              \
} while (0)

#define XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    // Create a new repository with default settings.
    // Close it and check the dump of the file.
    //
    // The check of the dump is simplistic: it is only to validate
    // that the .xoz file was created and it is non-empty.
    TEST(FileBlockArrayTest, CreateNew) {
        {
        DELETE("CreateNew.xoz");

        const char* fpath = SCRATCH_HOME "CreateNew.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 512, 0, true);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(512));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        // Close and check what we have on disk.
        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "" // nothing as expected
                );
        }

        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(512, 0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(512));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        // Close and check what we have on disk.
        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "" // nothing as expected
                );
        }

    }

    TEST(FileBlockArrayTest, CreateNewWithHeader) {
        {
        DELETE("CreateNewWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewWithHeader.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 1, true);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64)); // header is always created
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        // Close and check what we have on disk.
        // Expected: only the header, zero'd
        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }

        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(64, 1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64)); // header is always created
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        // Close and check what we have on disk.
        // Expected: only the header, zero'd
        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }


    TEST(FileBlockArrayTest, CreateNewThenOpen) {
        {
        DELETE("CreateNewThenOpen.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewThenOpen.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 512, 0, true);
        new_blkarr.close();

        FileBlockArray blkarr(SCRATCH_HOME "CreateNewThenOpen.xoz", 512);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(512));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        // Close and check that the file in disk still exists
        // Note: in CreateNew test we create-close-check, here
        // we do create-close-open-close-check.
        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                ""
                );
        }

        {
        FileBlockArray new_blkarr = FileBlockArray::create_mem_based(512);
        new_blkarr.close();

        auto ss = new_blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr(std::stringstream(ss), 512);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(512));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                ""
                );
        }
    }


    TEST(FileBlockArrayTest, CreateNewThenOpenWithHeader) {
        {
        DELETE("CreateNewThenOpenWithHeader.xoz");

        testing_xoz::zbreak();
        const char* fpath = SCRATCH_HOME "CreateNewThenOpenWithHeader.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 2, true);
        new_blkarr.close();

        FileBlockArray blkarr(SCRATCH_HOME "CreateNewThenOpenWithHeader.xoz", 64, 2);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(128));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(2));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(2));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        // Close and check that the file in disk still exists
        // Note: in CreateNew test we create-close-check, here
        // we do create-close-open-close-check.
        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }

        {
        FileBlockArray new_blkarr = FileBlockArray::create_mem_based(64, 2);
        new_blkarr.close();

        auto ss = new_blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr(std::stringstream(ss), 64, 2);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(128));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(2));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(2));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));


        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }

    TEST(FileBlockArrayTest, CreateThenOpenCloseOpen) {
        {
        DELETE("CreateThenOpenCloseOpen.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenOpenCloseOpen.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 0, true);
        new_blkarr.close();

        {
            FileBlockArray blkarr(SCRATCH_HOME "CreateThenOpenCloseOpen.xoz", 64);

            // Close and reopen again
            blkarr.close();
        }

        FileBlockArray blkarr(SCRATCH_HOME "CreateThenOpenCloseOpen.xoz", 64);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                ""
                );
        }

        {
        FileBlockArray new_blkarr = FileBlockArray::create_mem_based(64);
        new_blkarr.close();

        {
            auto ss = new_blkarr.expose_mem_fp().str(); // copy
            FileBlockArray blkarr(std::stringstream(ss), 64);

            // Close and reopen again
            blkarr.close();
        }

        auto ss = new_blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr(std::stringstream(ss), 64);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                ""
                );
        }
    }

    TEST(FileBlockArrayTest, CreateThenOpenCloseOpenWithHeader) {
        {
        DELETE("CreateThenOpenCloseOpenWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenOpenCloseOpenWithHeader.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 1, true);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.close();

        {
            FileBlockArray blkarr(SCRATCH_HOME "CreateThenOpenCloseOpenWithHeader.xoz", 64, 1);

            // Close and reopen again
            blkarr.close();
        }

        FileBlockArray blkarr(SCRATCH_HOME "CreateThenOpenCloseOpenWithHeader.xoz", 64, 1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }

        {
        FileBlockArray new_blkarr = FileBlockArray::create_mem_based(64, 1);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.close();

        {
            auto ss = new_blkarr.expose_mem_fp().str(); // copy
            FileBlockArray blkarr(std::stringstream(ss), 64, 1);

            // Close and reopen again
            blkarr.close();
        }

        auto ss = new_blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr(std::stringstream(ss), 64, 1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }

    TEST(FileBlockArrayTest, CreateThenRecreateAndOverride) {
        DELETE("CreateThenRecreateAndOverride.xoz");


        const char* fpath = SCRATCH_HOME "CreateThenRecreateAndOverride.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 0, true);
        new_blkarr.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenRecreateAndOverride.xoz", 64, 0, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                ""
                );
    }

    TEST(FileBlockArrayTest, CreateThenRecreateButFail) {
        DELETE("CreateThenRecreateButFail.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenRecreateButFail.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 0, true);
        new_blkarr.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists
        EXPECT_THAT(
            [&]() { FileBlockArray::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", 64, 0, true); },
            ThrowsMessage<OpenXOZError>(
                AllOf(
                    HasSubstr("FileBlockArray::create"),
                    HasSubstr("the file already exist and FileBlockArray::create is configured to not override it")
                    )
                )
        );

        // Try to open it again, this time with fail_if_exists == False.
        // Check that the previous failed creation **did not** corrupted the original
        // file
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenRecreateButFail.xoz", 64, 0, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                ""
                );
    }

    TEST(FileBlockArrayTest, CreateThenRecreateButFailWithHeader) {
        DELETE("CreateThenRecreateButFailWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenRecreateButFailWithHeader.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 1, true);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists
        EXPECT_THAT(
            [&]() { FileBlockArray::create(SCRATCH_HOME "CreateThenRecreateButFailWithHeader.xoz", 64, 1, true); },
            ThrowsMessage<OpenXOZError>(
                AllOf(
                    HasSubstr("FileBlockArray::create"),
                    HasSubstr("the file already exist and FileBlockArray::create is configured to not override it")
                    )
                )
        );

        // Try to open it again, this time with fail_if_exists == False.
        // Check that the previous failed creation **did not** corrupted the original
        // file
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenRecreateButFailWithHeader.xoz", 64, 1, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(FileBlockArrayTest, CreateThenExpand) {
        {
        DELETE("CreateThenExpand.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpand.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 0, true);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Add 6 more blocks
        old_top_nr = blkarr.grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)3);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(9 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(9));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(9));

        // Close and reopen and check again
        blkarr.close();
        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "CreateThenExpand.xoz"))), (bool)true);

        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpand.xoz", 64);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(9 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(9));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(9));

        blkarr2.close();
        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "CreateThenExpand.xoz"))), (bool)true);

        }

        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(64);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Add 6 more blocks
        old_top_nr = blkarr.grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)3);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(9 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(9));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(9));

        // Close and reopen and check again
        blkarr.close();
        auto ss = blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr2(std::stringstream(ss), 64);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(9 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(9));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(9));

        blkarr2.close();
        EXPECT_EQ(are_all_zeros(blkarr2.expose_mem_fp()), (bool)true);
        }
    }

    TEST(FileBlockArrayTest, CreateThenExpandNonZeroBeginBlkNr) {
        DELETE("CreateThenExpandNonZeroBeginBlkNr.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandNonZeroBeginBlkNr.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 1, true);

        // The repository by default has 1 block so adding 3 more
        // will yield 4 blocks in total
        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(4 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(4));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Add 6 more blocks
        old_top_nr = blkarr.grow_by_blocks(6);
        EXPECT_EQ(old_top_nr, (uint32_t)4);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(10 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(10));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(9));

        // Close and check again
        blkarr.close();
        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "CreateThenExpandNonZeroBeginBlkNr.xoz"))), (bool)true);

    }

    TEST(FileBlockArrayTest, CreateThenExpandThenRevert) {
        {
        DELETE("CreateThenExpandThenRevert.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevert.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 0, true);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Now "revert" freeing those 3 blocks
        blkarr.shrink_by_blocks(3);
        // Capacity still remains in 3: no real shrink happen
        EXPECT_EQ(blkarr.capacity(), (uint32_t)3);

        // No resize happen in the file either
        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        // Close and reopen and check again, this should release_blocks and shrink the file automatically
        blkarr.close();
        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpandThenRevert.xoz", 64);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(0 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);
        }

        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(64);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Now "revert" freeing those 3 blocks
        blkarr.shrink_by_blocks(3);
        // Capacity still remains in 3: no real shrink happen
        EXPECT_EQ(blkarr.capacity(), (uint32_t)3);

        // No resize happen in the file either
        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        // Close and reopen and check again, this should release_blocks and shrink the file automatically
        blkarr.close();
        auto ss = blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr2(std::stringstream(ss), 64);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(0 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);
        }
    }

    TEST(FileBlockArrayTest, CreateThenExpandThenRevertWithHeader) {
        {
        DELETE("CreateThenExpandThenRevertWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevertWithHeader.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 1, true);
        blkarr.write_header("ABCD", 4);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(4 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(4));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Now "revert" freeing those 3 blocks
        blkarr.shrink_by_blocks(3);
        // Capacity still remains in 3: no real shrink happen
        EXPECT_EQ(blkarr.capacity(), (uint32_t)3);

        // No resize happen in the file either
        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(4 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        // Close and reopen and check again, this should release_blocks and shrink the file automatically
        blkarr.close();
        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpandThenRevertWithHeader.xoz", 64, 1);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(1 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }

        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(64, 1);
        blkarr.write_header("ABCD", 4);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(4 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(4));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Now "revert" freeing those 3 blocks
        blkarr.shrink_by_blocks(3);
        // Capacity still remains in 3: no real shrink happen
        EXPECT_EQ(blkarr.capacity(), (uint32_t)3);

        // No resize happen in the file either
        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(4 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        // Close and reopen and check again, this should release_blocks and shrink the file automatically
        blkarr.close();
        auto ss = blkarr.expose_mem_fp().str(); // copy
        FileBlockArray blkarr2(std::stringstream(ss), 64, 1);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(1 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);

        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }

    TEST(FileBlockArrayTest, CreateThenExpandCloseThenShrink) {
        DELETE("CreateThenExpandCloseThenShrink.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 0, true);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        // Close and check: the file should be grown
        blkarr.close();

        // Now "shrink" freeing those 3 blocks
        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz", 64);
        blkarr2.shrink_by_blocks(3);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(3 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        // Close and check again: the file should shrank
        blkarr2.close();
        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz"))), (bool)true);

        FileBlockArray blkarr3(SCRATCH_HOME "CreateThenExpandCloseThenShrink.xoz", 64);

        EXPECT_EQ(blkarr3.phy_file_sz(), uint32_t(0 * 64));
        EXPECT_EQ(blkarr3.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr3.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.blk_cnt(), uint32_t(0));
    }

    TEST(FileBlockArrayTest, ReleaseBlocksOnDestroy) {
        DELETE("ReleaseBlocksOnDestroy.xoz");

        const char* fpath = SCRATCH_HOME "ReleaseBlocksOnDestroy.xoz";
        {
            FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 0, true);

            auto old_top_nr = blkarr.grow_by_blocks(3);
            EXPECT_EQ(old_top_nr, (uint32_t)0);

            EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
            EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
            EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
            EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
            EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

            // Now "shrink" freeing those 3 blocks
            blkarr.shrink_by_blocks(3);

            // No change in the file size
            EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64));
            EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
            EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
            EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
            EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

            // no explicit call to blkarr.close()
            // implicit call in ~FileBlockArray
        }

        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "ReleaseBlocksOnDestroy.xoz"))), (bool)true);

        FileBlockArray blkarr3(SCRATCH_HOME "ReleaseBlocksOnDestroy.xoz", 64);

        EXPECT_EQ(blkarr3.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr3.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr3.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.blk_cnt(), uint32_t(0));
    }
}

