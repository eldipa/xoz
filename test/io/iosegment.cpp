#include "xoz/blk/file_block_array.h"
#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iosegment.h"
#include "xoz/err/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::subvec;

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

#define XOZ_EXPECT_FILE_SERIALIZATION(blkarr, at, len, data) do {           \
    EXPECT_EQ(hexdump((blkarr).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

#define XOZ_EXPECT_BUFFER_SERIALIZATION(buffer, at, len, data) do {           \
    EXPECT_EQ(hexdump((buffer), (at), (len)), (data));              \
} while (0)

namespace {
    TEST(IOSegmentTest, OneBlock) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, 4);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(64 - 4));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, OneBlockCompletely) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::vector<char> rdbuf;

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, (uint32_t)64);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Call read_extent again but let read_extent to figure out how many bytes needs to read
        // (the size of the extent in bytes)
        rdbuf.clear();
        iosg2.seek_rd(0);
        EXPECT_EQ(iosg2.remain_rd(), uint32_t(64));

        iosg2.readall(rdbuf);
        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
    }

    TEST(IOSegmentTest, MultiExtentSegment) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(4);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(1, 1, false)); // one block
        sg.add_extent(Extent(0, 1, false)); // one block
        sg.add_extent(Extent(2, 2, false)); // two blocks

        std::vector<char> wrbuf(64 * 4 - 12);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // enough to fill "almost" all the sg

        std::vector<char> rdbuf;

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf); // write all the buffer into the segment ("almost" completely)

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(12));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64*4 - 12));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "c0c1 c2c3 c4c5 c6c7 c8c9 cacb cccd cecf d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf "
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef f0f1 f2f3 0000 0000 0000 0000 0000 0000"
                );

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, 64 * 4 - 12); // read that exact count of bytes

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(12));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64*4 - 12));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "c0c1 c2c3 c4c5 c6c7 c8c9 cacb cccd cecf d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf "
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef f0f1 f2f3 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Reset the reading buffer/io
        rdbuf.clear();
        iosg2.seek_rd(0);

        // Read all the segment (the 4 blocks)
        iosg2.readall(rdbuf);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64*4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "c0c1 c2c3 c4c5 c6c7 c8c9 cacb cccd cecf d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf "
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef f0f1 f2f3 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, subvec(rdbuf, 0, 64 * 4 - 12)); // compare only these

        std::vector<char> zeros = {0,0,0,0,0,0,0,0,0,0,0,0};
        EXPECT_EQ(zeros, subvec(rdbuf, 64 * 4 - 12)); // compare the rest

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "c0c1 c2c3 c4c5 c6c7 c8c9 cacb cccd cecf d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf "
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef f0f1 f2f3 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, MultiExtentSegmentMultiReadWrite) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(4);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(1, 1, false)); // one block
        sg.add_extent(Extent(0, 1, false)); // one block
        sg.add_extent(Extent(2, 2, false)); // two blocks

        std::vector<char> wrbuf(64 * 4);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // enough to fill all the sg

        std::vector<char> rdbuf;

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf, 30); // first 30

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64*4 - 30));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(30));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iosg1.writeall(subvec(wrbuf, 30), 68); // next 68

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64*4 - 30 - 68));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(30 + 68));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iosg1.writeall(subvec(wrbuf, 30+68), 1);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64*4 - 30 - 68 - 1));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(30 + 68 + 1));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6200 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iosg1.writeall(subvec(wrbuf, 30+68+1)); // the rest

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64 * 4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "c0c1 c2c3 c4c5 c6c7 c8c9 cacb cccd cecf d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf "
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef f0f1 f2f3 f4f5 f6f7 f8f9 fafb fcfd feff"
                );

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, 30); // read first 30

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(64*4 - 30));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(30));
        EXPECT_EQ(subvec(wrbuf, 0, 30), rdbuf);
        rdbuf.clear();

        iosg2.readall(rdbuf, 68); // read next 68 bytes

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(64*4 - 30 - 68));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(30 + 68));
        EXPECT_EQ(subvec(wrbuf, 30, 30+68), rdbuf);
        rdbuf.clear();

        iosg2.readall(rdbuf, 1); // read 1 byte

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(64*4 - 30 - 68 - 1));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(30 + 68 + 1));
        EXPECT_EQ(subvec(wrbuf, 30+68, 30+68+1), rdbuf);
        rdbuf.clear();

        iosg2.readall(rdbuf); // read the rest
        EXPECT_EQ(subvec(wrbuf, 30+68+1), rdbuf);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64 * 4));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4041 4243 4445 4647 4849 4a4b 4c4d 4e4f 5051 5253 5455 5657 5859 5a5b 5c5d 5e5f "
                "6061 6263 6465 6667 6869 6a6b 6c6d 6e6f 7071 7273 7475 7677 7879 7a7b 7c7d 7e7f "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "8081 8283 8485 8687 8889 8a8b 8c8d 8e8f 9091 9293 9495 9697 9899 9a9b 9c9d 9e9f "
                "a0a1 a2a3 a4a5 a6a7 a8a9 aaab acad aeaf b0b1 b2b3 b4b5 b6b7 b8b9 babb bcbd bebf "
                "c0c1 c2c3 c4c5 c6c7 c8c9 cacb cccd cecf d0d1 d2d3 d4d5 d6d7 d8d9 dadb dcdd dedf "
                "e0e1 e2e3 e4e5 e6e7 e8e9 eaeb eced eeef f0f1 f2f3 f4f5 f6f7 f8f9 fafb fcfd feff"
                );
    }

    TEST(IOSegmentTest, RWBeyondBoundary) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block

        std::vector<char> wrbuf(65); // block size plus 1
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0);

        std::vector<char> rdbuf;

        IOSegment iosg1(blkarr, sg);
        uint32_t n = iosg1.writesome(wrbuf); // try to write 65 bytes, but write only 64

        EXPECT_EQ(n, (uint32_t)64);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        n = iosg1.writesome(wrbuf); // yes, try to write 65 bytes "more"
        EXPECT_EQ(n, (uint32_t)0);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));

        iosg1.seek_wr(99); // try to go past the end but no effect
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));

        IOSegment iosg2(blkarr, sg);
        n = iosg2.readsome(rdbuf, 65); // try to read 65 but read only 64

        EXPECT_EQ(n, (uint32_t)64);
        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        n = iosg2.readsome(rdbuf, 65); // try to read 65 more
        EXPECT_EQ(n, (uint32_t)0);
        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64));

        iosg2.seek_rd(99); // try to go past the end but no effect
        EXPECT_EQ(iosg2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(64));

        EXPECT_EQ(subvec(wrbuf, 0, 64), subvec(rdbuf, 0, 64));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );
    }

    TEST(IOSegmentTest, Seek) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block

        IOSegment iosg1(blkarr, sg);

        // Initial positions
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(64));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(0));

        // Read/write pointers are independent
        iosg1.seek_wr(5);
        iosg1.seek_rd(9);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64-5));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(5));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(64-9));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(9));

        // Positions are absolute by default (relative to the begin of the segment)
        iosg1.seek_wr(50);
        iosg1.seek_rd(39);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64-50));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(50));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(64-39));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(39));

        // Past the end is clamp to the segment size
        iosg1.seek_wr(9999);
        iosg1.seek_rd(9999);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64));

        // Seek relative the current position in backward direction
        iosg1.seek_wr(2, IOSegment::Seekdir::bwd);
        iosg1.seek_rd(1, IOSegment::Seekdir::bwd);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(2));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64 - 2));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64 - 1));

        // Seek relative the current position in backward direction (validate that it's relative)
        iosg1.seek_wr(6, IOSegment::Seekdir::bwd);
        iosg1.seek_rd(6, IOSegment::Seekdir::bwd);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(8));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64 - 8));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(7));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64 - 7));

        // Seek past the begin is set to 0; seek relative 0 does not change the pointer
        iosg1.seek_wr(999, IOSegment::Seekdir::bwd);
        iosg1.seek_rd(0, IOSegment::Seekdir::bwd);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(7));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64 - 7));

        // Seek relative the current position in forward direction
        iosg1.seek_wr(4, IOSegment::Seekdir::fwd);
        iosg1.seek_rd(4, IOSegment::Seekdir::fwd);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(4));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(7 - 4));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64 - 7 + 4));

        // Seek relative the current position in forward direction, again
        iosg1.seek_wr(2, IOSegment::Seekdir::fwd);
        iosg1.seek_rd(2, IOSegment::Seekdir::fwd);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64 - 4 - 2));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(4+2));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(7 - 4 - 2));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64 - 7 + 4 + 2));

        // Seek relative the current position in forward direction, past the end
        iosg1.seek_wr(59, IOSegment::Seekdir::fwd);
        iosg1.seek_rd(3, IOSegment::Seekdir::fwd);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64));

        // Seek relative the end position
        iosg1.seek_wr(0, IOSegment::Seekdir::end);
        iosg1.seek_rd(0, IOSegment::Seekdir::end);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64));

        // Again
        iosg1.seek_wr(3, IOSegment::Seekdir::end);
        iosg1.seek_rd(3, IOSegment::Seekdir::end);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(3));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64-3));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(3));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64-3));

        // Again
        iosg1.seek_wr(6, IOSegment::Seekdir::end);
        iosg1.seek_rd(1, IOSegment::Seekdir::end);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(6));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64-6));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(64-1));

        // Past the begin goes to zero
        iosg1.seek_wr(64, IOSegment::Seekdir::end);
        iosg1.seek_rd(65, IOSegment::Seekdir::end);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(64));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(0));

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, RWExactFail) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block

        std::vector<char> wrbuf(65); // block size plus 1
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0);

        std::vector<char> rdbuf(128, 0); // initialize to 0 so we can check later that nobody written on it

        IOSegment iosg1(blkarr, sg);
        EXPECT_THAT(
            [&]() { iosg1.writeall(wrbuf); },  // try to write 65 bytes, but 64 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 65 bytes but only 64 bytes are available. "
                              "Write exact-byte-count operation at position 0 failed; "
                              "detected before the write."
                        )
                    )
                )
        );

        // Nothing is written
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Write a few bytes
        iosg1.writeall(subvec(wrbuf, 0, 8));

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSegment iosg2(blkarr, sg);
        EXPECT_THAT(
            [&]() { iosg2.readall(rdbuf, 65); },  // try to read 65 bytes, but 64 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 65 bytes but only 64 bytes are available. "
                              "Read exact-byte-count operation at position 0 failed; "
                              "detected before the read."
                        )
                    )
                )
        );

        // Nothing was read
        std::vector<char> zeros = {0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_EQ(subvec(rdbuf, 0, 8), zeros);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0001 0203 0405 0607 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, Fill) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block

        IOSegment iosg1(blkarr, sg);
        iosg1.fill(0x41, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4141 4141 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iosg1.fill(0x42, 6);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(64 - 4 - 6));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(4 + 6));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4141 4141 4242 4242 4242 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iosg1.seek_wr(2, IOBase::Seekdir::fwd);
        iosg1.fill(0x44, 64 - 4 - 6 - 2);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4141 4141 4242 4242 4242 0000 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 "
                "4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444"
                );

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4141 4141 4242 4242 4242 0000 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 "
                "4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444 4444"
                );
    }

    TEST(IOSegmentTest, EmptySegmentNoInline) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        Segment sg(blkarr.blk_sz_order());

        IOSegment iosg1(blkarr, sg);

        // Initial positions
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(0));
    }


    TEST(IOSegmentTest, EmptySegmentWithEmptyInline) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        Segment sg = Segment::create_empty_zero_inline(blkarr.blk_sz_order());

        IOSegment iosg1(blkarr, sg);

        // Initial positions
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iosg1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_rd(), uint32_t(0));
    }

    TEST(IOSegmentTest, InlineOnly) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.reserve_inline_data(6);

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(6 - 4));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> zeros = {0,0};
        EXPECT_EQ(wrbuf, subvec(sg.inline_data(), 0, 4));
        EXPECT_EQ(zeros, subvec(sg.inline_data(), 4, 6));
        EXPECT_EQ(sg.inline_data().size(), (size_t)6);

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, 4);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(6 - 4));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, FullInlineOnly) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.reserve_inline_data(6);

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf, 6);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(6 - 6));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(6));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, sg.inline_data());
        EXPECT_EQ(sg.inline_data().size(), (size_t)6);

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, 6);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(6 - 6));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(6));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, InlineDoesNotGrow) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.reserve_inline_data(4); // smaller

        IOSegment iosg1(blkarr, sg);
        EXPECT_THAT(
            [&]() { iosg1.writeall(wrbuf); },  // try to write 6 bytes, but 4 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 6 bytes but only 4 bytes are available. "
                              "Write exact-byte-count operation at position 0 failed; "
                              "detected before the write."
                        )
                    )
                )
        );

        // Nothing was written
        std::vector<char> zeros = {0, 0, 0, 0};
        EXPECT_EQ(sg.inline_data(), zeros);
        EXPECT_EQ(sg.inline_data().size(), (size_t)4);

        EXPECT_THAT(
            [&]() { iosg1.readall(rdbuf, 6); },  // try to read 6 bytes, but 4 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 6 bytes but only 4 bytes are available. "
                              "Read exact-byte-count operation at position 0 failed; "
                              "detected before the read."
                        )
                    )
                )
        );

        // Nothing was read
        std::vector<char> zeros2 = {0, 0, 0, 0, 0, 0};
        EXPECT_EQ(rdbuf, zeros2);
        EXPECT_EQ(sg.inline_data().size(), (size_t)4);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSegmentTest, OneBlockAndInline) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(4);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block
        sg.reserve_inline_data(4);

        IOSegment iosg1(blkarr, sg);
        iosg1.writeall(wrbuf);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(8 - 6));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(6));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4142 4344"
                );

        std::vector<char> zeros = {0,0};
        EXPECT_EQ(subvec(wrbuf, 4, 6), subvec(sg.inline_data(), 0, 2));
        EXPECT_EQ(zeros, subvec(sg.inline_data(), 2, 4));
        EXPECT_EQ(sg.inline_data().size(), (size_t)4);

        IOSegment iosg2(blkarr, sg);
        iosg2.readall(rdbuf, 6);

        EXPECT_EQ(iosg2.remain_rd(), uint32_t(8 - 6));
        EXPECT_EQ(iosg2.tell_rd(), uint32_t(6));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4142 4344"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        EXPECT_EQ(subvec(wrbuf, 4, 6), subvec(sg.inline_data(), 0, 2));
        EXPECT_EQ(zeros, subvec(sg.inline_data(), 2, 4));
        EXPECT_EQ(sg.inline_data().size(), (size_t)4);

        blkarr.close();
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4142 4344"
                );
    }

    TEST(IOSegmentTest, FillC) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(1);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 1, false)); // one block
        sg.set_inline_data({0, 0});

        // Fill the entire space (inline data space not included)
        IOSegment::fill_c(blkarr, sg, 0x41, false);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 "
                "4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141 4141"
                );
        EXPECT_EQ(sg.inline_data_sz(), (uint32_t)2);
        EXPECT_EQ(sg.inline_data()[0], (char)0);
        EXPECT_EQ(sg.inline_data()[1], (char)0);

        // Fill the entire space (inline data space included)
        IOSegment::fill_c(blkarr, sg, 0x42, true);
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 "
                "4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242 4242"
                );
        EXPECT_EQ(sg.inline_data_sz(), (uint32_t)2);
        EXPECT_EQ(sg.inline_data()[0], (char)0x42);
        EXPECT_EQ(sg.inline_data()[1], (char)0x42);

    }

    TEST(IOSegmentTest, LimitsOnReadWrite) {
        auto blkarr_ptr = FileBlockArray::create_mem_based(8);
        FileBlockArray& blkarr = *blkarr_ptr.get();

        auto old_top_nr = blkarr.grow_by_blocks(3);
        EXPECT_EQ(old_top_nr, (uint32_t)0);

        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        Segment sg(blkarr.blk_sz_order());
        sg.add_extent(Extent(0, 2, false)); // 2 block of 8 bytes
        sg.add_extent(Extent(2, 1, false)); // 1 block of 8 bytes
        sg.set_inline_data({0, 0}); // plus 2 bytes of inline data

        IOSegment iosg1(blkarr, sg);
        EXPECT_EQ(iosg1.remain_wr(), uint32_t(26));

        // Limit within a single 2-blocks extent
        iosg1.limit_wr(1, 4);
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(5));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0041 4243 4400 0000 "
                "0000 0000 0000 0000 "
                "0000 0000 0000 0000"
                );

        // Limit within a single 1-block extent
        iosg1.limit_wr(16, 4);
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(20));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0041 4243 4400 0000 "
                "0000 0000 0000 0000 "
                "4142 4344 0000 0000"
                );

        // Limit within the inline
        iosg1.limit_wr(24, 1);
        iosg1.writeall(wrbuf, 1);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(25));
        XOZ_EXPECT_BUFFER_SERIALIZATION(sg.inline_data(), 0, -1,
                "4100"
                );

        // Limit within the inline (again)
        iosg1.limit_wr(24, 3); // overflow
        iosg1.seek_wr(24);
        iosg1.writeall(wrbuf, 2);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(26));
        XOZ_EXPECT_BUFFER_SERIALIZATION(sg.inline_data(), 0, -1,
                "4142"
                );

        // Limit covers two extents
        iosg1.limit_wr(10, 10);
        iosg1.seek_wr(10);
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(6));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(14));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0041 4243 4400 0000 "
                "0000 4142 4344 0000 "
                "4142 4344 0000 0000"
                );

        // Keep writing, this will cross the boundary between the extents
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(2));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(18));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0041 4243 4400 0000 "
                "0000 4142 4344 4142 "
                "4344 4344 0000 0000"
                );

        // Limit covers one extent and the inline section
        iosg1.limit_wr(22, 4);
        iosg1.writeall(wrbuf, 4);

        EXPECT_EQ(iosg1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iosg1.tell_wr(), uint32_t(26));
        XOZ_EXPECT_FILE_SERIALIZATION(blkarr, 0, -1,
                "0041 4243 4400 0000 "
                "0000 4142 4344 4142 "
                "4344 4344 0000 4142"
                );
        XOZ_EXPECT_BUFFER_SERIALIZATION(sg.inline_data(), 0, -1,
                "4344"
                );
    }

}
