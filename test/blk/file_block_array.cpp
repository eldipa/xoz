#include "xoz/blk/file_block_array.h"
#include "xoz/err/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <cstdlib>
#include <vector>

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

#define XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, at, len, data) do {  \
    std::vector<char> header((blkarr).header_sz());                       \
    (blkarr).read_header(&header[0], uint32_t(header.size()));                   \
    EXPECT_EQ(hexdump(header, (at), (len)), (data));                     \
} while (0)

#define XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, at, len, data) do {  \
    std::vector<char> trailer((blkarr).trailer_sz());                       \
    (blkarr).read_trailer(&trailer[0], uint32_t(trailer.size()));                   \
    EXPECT_EQ(hexdump(trailer, (at), (len)), (data));                     \
} while (0)

#define XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(FileBlockArrayTest, CreateNew) {
        // Create a file, empty, close it and check the content in disk
        {
        DELETE("CreateNew.xoz");

        const char* fpath = SCRATCH_HOME "CreateNew.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 512, 0, true);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(512));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        // Close and check what we have on disk.
        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "" // nothing as expected
                );
        }

        // Same file creation but in memory
        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(512, 0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(512));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        // Close and check what we have on disk.
        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "" // nothing as expected
                );
        }

    }

    TEST(FileBlockArrayTest, CreateNewWithHeader) {
        // Create a file with 1 block of header. Because the file is new
        // FileBlockArray::create will fill that block with zeros
        {
        DELETE("CreateNewWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewWithHeader.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 1, true);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64)); // header is always created
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

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
        // Create a file, close is, and open it again (in a new FileBlockArray object)
        // Opening a file reusing the FileBlockArray instance is not supported
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


        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

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


        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                ""
                );
        }
    }


    TEST(FileBlockArrayTest, CreateNewThenOpenWithHeader) {
        // Create a file with 2 blocks of header, close it and reopen it.
        {
        DELETE("CreateNewThenOpenWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateNewThenOpenWithHeader.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 2, true);
        new_blkarr.close();

        FileBlockArray blkarr(SCRATCH_HOME "CreateNewThenOpenWithHeader.xoz", 64, 2);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(128));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(2));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(2));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

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
        // Create, close, open, close , open.
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                ""
                );
        }
    }

    TEST(FileBlockArrayTest, CreateThenOpenCloseOpenWithHeader) {
        // Create, close, open, close , open, with an initial 1 block of header
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }

    TEST(FileBlockArrayTest, CreateThenCreateButOpen) {
        // Create, close, then create again but because the file exists and
        // fail_if_exists is false, do not override but open instead.
        DELETE("CreateThenCreateButOpen.xoz");


        const char* fpath = SCRATCH_HOME "CreateThenCreateButOpen.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 0, true);
        new_blkarr.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButOpen.xoz", 64, 0, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                ""
                );
    }

    TEST(FileBlockArrayTest, CreateThenCreateButFail) {
        DELETE("CreateThenCreateButFail.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenCreateButFail.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 0, true);
        new_blkarr.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists
        EXPECT_THAT(
            [&]() { FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButFail.xoz", 64, 0, true); },
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
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButFail.xoz", 64, 0, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                ""
                );
    }

    TEST(FileBlockArrayTest, CreateThenCreateButOpenWithHeader) {
        // Create, close, then create again but because the file exists and
        // fail_if_exists is false, do not override but open instead.
        DELETE("CreateThenCreateButOpenWithHeader.xoz");


        const char* fpath = SCRATCH_HOME "CreateThenCreateButOpenWithHeader.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 1, true);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButOpenWithHeader.xoz", 64, 1, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(FileBlockArrayTest, CreateThenCreateButFailWithHeader) {
        DELETE("CreateThenCreateButFailWithHeader.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenCreateButFailWithHeader.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 1, true);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.close();

        // Create again with fail_if_exists == True so it **will** fail
        // because the file already exists
        EXPECT_THAT(
            [&]() { FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButFailWithHeader.xoz", 64, 1, true); },
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
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButFailWithHeader.xoz", 64, 1, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(FileBlockArrayTest, CreateThenCreateButOpenWithHeaderAndTrailer) {
        // Create, close, then create again but because the file exists and
        // fail_if_exists is false, do not override but open instead.
        DELETE("CreateThenCreateButOpenWithHeaderAndTrailer.xoz");


        const char* fpath = SCRATCH_HOME "CreateThenCreateButOpenWithHeaderAndTrailer.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 1, true);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.write_trailer("EFG", 3);
        new_blkarr.close();

        // Create again with fail_if_exists == False so it will not fail
        // because the file already exists but instead it will open it
        FileBlockArray blkarr = FileBlockArray::create(SCRATCH_HOME "CreateThenCreateButOpenWithHeaderAndTrailer.xoz", 64, 1, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64 + 3));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                "4546 47"
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "4546 47"
                );
    }

    TEST(FileBlockArrayTest, CreateThenExpand) {
        // Create a new file, expand it and close it. Open, check, expand again, close, and check
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        // Close and reopen and check again
        blkarr.close();
        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "CreateThenExpand.xoz"))), (bool)true);

        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpand.xoz", 64);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(9 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(9));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(9));

        // Add 3 more blocks
        old_top_nr = blkarr2.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)9);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(12 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(12));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(12));

        blkarr2.close();
        EXPECT_EQ(are_all_zeros(file2mem((SCRATCH_HOME "CreateThenExpand.xoz"))), (bool)true);

        FileBlockArray blkarr3(SCRATCH_HOME "CreateThenExpand.xoz", 64);

        EXPECT_EQ(blkarr3.phy_file_sz(), uint32_t(12 * 64));
        EXPECT_EQ(blkarr3.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr3.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.past_end_blk_nr(), uint32_t(12));
        EXPECT_EQ(blkarr3.blk_cnt(), uint32_t(12));

        blkarr3.close();
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

        // Add 3 more blocks
        old_top_nr = blkarr2.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)9);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(12 * 64));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(12));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(12));


        blkarr2.close();
        EXPECT_EQ(are_all_zeros(blkarr2.expose_mem_fp()), (bool)true);

        // Close and reopen and check again
        auto ss2 = blkarr2.expose_mem_fp().str(); // copy
        FileBlockArray blkarr3(std::stringstream(ss2), 64);

        EXPECT_EQ(blkarr3.phy_file_sz(), uint32_t(12 * 64));
        EXPECT_EQ(blkarr3.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr3.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.past_end_blk_nr(), uint32_t(12));
        EXPECT_EQ(blkarr3.blk_cnt(), uint32_t(12));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );

        blkarr3.close();
        EXPECT_EQ(are_all_zeros(blkarr3.expose_mem_fp()), (bool)true);
        }
    }

    TEST(FileBlockArrayTest, CreateThenExpandWithHeaderAndTrailer) {
        DELETE("CreateThenExpandWithHeaderAndTrailer.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandWithHeaderAndTrailer.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 1, true);
        blkarr.write_header("ABCD", 4);
        blkarr.write_trailer("EFG", 3);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)1);

        // Note: trailer is not included in phy_file_sz (3 bytes are missing from the count)
        // This is because the trailer was not written down to disk
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                "4546 47"
                );

        // Close and check again
        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "4546 47"
                );

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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);
        }
    }

    TEST(FileBlockArrayTest, CreateThenExpandThenRevertWithHeaderAndTrailer) {
        {
        DELETE("CreateThenExpandThenRevertWithHeaderAndTrailer.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandThenRevertWithHeaderAndTrailer.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 1, true);
        blkarr.write_header("ABCD", 4);
        blkarr.write_trailer("EFG", 3);

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
        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpandThenRevertWithHeaderAndTrailer.xoz", 64, 1);

        // After the close above, the trailer is now present in disk
        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(1 * 64 + 3));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr2, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr2, 0, -1,
                "4546 47"
                );

        blkarr2.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "4546 47"
                );
        }

        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(64, 1);
        blkarr.write_header("ABCD", 4);
        blkarr.write_trailer("EFG", 3);

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

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(1 * 64 + 3));
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        EXPECT_EQ(blkarr2.capacity(), (uint32_t)0);

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr2, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr2, 0, -1,
                "4546 47"
                );

        blkarr2.close();
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr2, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "4546 47"
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );
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

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );
    }

    TEST(FileBlockArrayTest, BadReadHeaderAndTrailer) {
        {
        FileBlockArray blkarr = FileBlockArray::create_mem_based(64, 0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        char buf[1] = {'A'};
        EXPECT_THAT(
            [&]() { blkarr.read_header(buf, 1); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 1 bytes but only 0 bytes are available. Bad read header")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { blkarr.read_trailer(buf, 1); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 1 bytes but only 0 bytes are available. Bad read trailer")
                    )
                )
        );

        // You cannot write a header if there is not room. There is no way to grow the space
        // for the header.
        EXPECT_THAT(
            [&]() { blkarr.write_header(buf, 1); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 1 bytes but only 0 bytes are available. Bad write header")
                    )
                )
        );

        // In contrast, a trailer can always grow
        blkarr.write_trailer(buf, 1);

        // However, the trailer cannot grow beyond the size of a single block
        EXPECT_THAT(
            [&]() { blkarr.write_trailer(buf, 64); },
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 64 bytes but only 63 bytes are available. Bad write trailer, trailer must be smaller than the block size")
                    )
                )
        );

        blkarr.close();

        // Note how only the first trailer that we wrote really ended in the file
        XOZ_EXPECT_FILE_MEM_SERIALIZATION(blkarr, 0, -1,
                "41"
                );
        }

    }

    TEST(FileBlockArrayTest, CreateThenExpandCloseThenShrinkWithTrailer) {
        DELETE("CreateThenExpandCloseThenShrinkWithTrailer.xoz");

        const char* fpath = SCRATCH_HOME "CreateThenExpandCloseThenShrinkWithTrailer.xoz";
        FileBlockArray blkarr = FileBlockArray::create(fpath, 64, 0, true);
        blkarr.write_trailer("ABCD", 4);

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(3 * 64)); // trailer is not there yet
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(3));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(3));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344"
                );

        // Close and check: the file should be grown
        blkarr.close();

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "4142 4344"
                );

        // Now "shrink" freeing those 3 blocks
        FileBlockArray blkarr2(SCRATCH_HOME "CreateThenExpandCloseThenShrinkWithTrailer.xoz", 64);
        blkarr2.shrink_by_blocks(3);

        EXPECT_EQ(blkarr2.phy_file_sz(), uint32_t(3 * 64 + 4)); // the shrink is not reflected in the file yet but the trailer is
        EXPECT_EQ(blkarr2.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr2.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr2.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr2, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr2, 0, -1,
                "4142 4344"
                );

        // Close and check again: the file should shrank
        blkarr2.close();

        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344"
                );

        FileBlockArray blkarr3(SCRATCH_HOME "CreateThenExpandCloseThenShrinkWithTrailer.xoz", 64);

        EXPECT_EQ(blkarr3.phy_file_sz(), uint32_t(0 * 64 + 4)); // only the trailer is there
        EXPECT_EQ(blkarr3.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr3.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.past_end_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr3.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr3, 0, -1,
                ""
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr3, 0, -1,
                "4142 4344"
                );

        blkarr3.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344"
                );
    }

    TEST(FileBlockArrayTest, UsePreloadFunc) {
        DELETE("UsePreloadFunc.xoz");

        // 64 bytes block with 1 block of header
        const char* fpath = SCRATCH_HOME "UsePreloadFunc.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create(fpath, 64, 1, true);
        new_blkarr.write_header("ABCD", 4);
        new_blkarr.close();

        {
        // Same parameters: 64 bytes block and 1 block of header
        FileBlockArray blkarr(SCRATCH_HOME "UsePreloadFunc.xoz",
                []([[maybe_unused]] std::istream& is, struct FileBlockArray::blkarr_cfg_t& cfg, [[maybe_unused]] bool on_create) {
                    cfg.blk_sz = 64;
                    cfg.begin_blk_nr = 1;

                });

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }

        {
        // We set the begin_blk_nr to 0. We expect to see the header as another
        // block.
        FileBlockArray blkarr(SCRATCH_HOME "UsePreloadFunc.xoz",
                []([[maybe_unused]] std::istream& is, struct FileBlockArray::blkarr_cfg_t& cfg, [[maybe_unused]] bool on_create) {
                    cfg.blk_sz = 64;
                    cfg.begin_blk_nr = 0; // this is "wrong"

                });

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(0));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(1)); // blkarr sees the header as any other block

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "" // blkarr then does not see any header
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }

    TEST(FileBlockArrayTest, UsePreloadFuncOnCreate) {
        DELETE("UsePreloadFuncOnCreate.xoz");

        // By default, block of 64 bytes with 1 block of header
        auto fn = []([[maybe_unused]] std::istream& is, struct FileBlockArray::blkarr_cfg_t& cfg, bool on_create) {
                if (on_create) {
                    cfg.blk_sz = 64;
                    cfg.begin_blk_nr = 1;
                } else {
                    char data[2];
                    is.read(data, 2);
                    cfg.blk_sz = uint32_t(data[0]);
                    cfg.begin_blk_nr = uint32_t(data[1]);
                }
            };

        // 64 bytes block with 1 block of header
        const char* fpath = SCRATCH_HOME "UsePreloadFuncOnCreate.xoz";
        FileBlockArray new_blkarr = FileBlockArray::create( fpath, fn, true);

        // store in the header the blk sz (64) and begin_blk_nr (1)
        new_blkarr.write_header("\x40\x01", 2);
        new_blkarr.close();

        {
        // Check
        FileBlockArray blkarr(SCRATCH_HOME "UsePreloadFuncOnCreate.xoz", 64, 1);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4001 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4001 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }

        {
        // Create an existing file: open instead of failing
        FileBlockArray blkarr = FileBlockArray::create(fpath, fn, false);

        EXPECT_EQ(blkarr.phy_file_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.blk_sz(), uint32_t(64));
        EXPECT_EQ(blkarr.begin_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.past_end_blk_nr(), uint32_t(1));
        EXPECT_EQ(blkarr.blk_cnt(), uint32_t(0));

        XOZ_EXPECT_FILE_HEADER_SERIALIZATION(blkarr, 0, -1,
                "4001 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        XOZ_EXPECT_FILE_TRAILER_SERIALIZATION(blkarr, 0, -1,
                ""
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                "4001 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
        }
    }
}

