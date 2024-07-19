#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include "xoz/ext/extent.h"
#include "xoz/err/exceptions.h"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing_xoz::helpers::ensure_called_once;

using namespace ::xoz;

namespace {
    TEST(ExtentTest, BlockNumberBits) {
        // Block numbers are 26 bits long
        // This test check that the 25th bit is preserved (being 0th the lowest)
        // and the 26th is dropped (because it would require 27 bits)
        EXPECT_THAT(
            ensure_called_once([&]() { Extent ext1((1 << 25) | (1 << 26), 1, false); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Invalid block number 100663296, it is more than 26 bits. "
                        "Error when creating a new extent of block count 1 "
                        "(is suballoc: 0)"
                        )
                    )
                )
        );

        // Suballoc'd does not change the above
        EXPECT_THAT(
            ensure_called_once([&]() { Extent ext2((1 << 25) | (1 << 26), 1, true); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "Invalid block number 100663296, it is more than 26 bits. "
                        "Error when creating a new extent of block count 1 "
                        "(is suballoc: 1)"
                        )
                    )
                )
        );


        // Check lower bits
        Extent ext4((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext4.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));
        EXPECT_EQ(ext4.blk_nr() & 0xffff, (uint32_t)((1 << 15) | (1 << 3)));
        EXPECT_EQ(ext4.blk_nr() >> 16, (uint32_t)(0));

        // Suballoc'd does not change the above
        Extent ext5((1 << 15) | (1 << 3), 1, true);
        EXPECT_EQ(ext5.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));
        EXPECT_EQ(ext5.blk_nr() & 0xffff, (uint32_t)((1 << 15) | (1 << 3)));
        EXPECT_EQ(ext5.blk_nr() >> 16, (uint32_t)(0));

        // Check higher and lower bits
        Extent ext6((1 << 16) | (1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext6.blk_nr(), (uint32_t)((1 << 16) | (1 << 15) | (1 << 3)));
        EXPECT_EQ(ext6.blk_nr() >> 16, (uint16_t)((1 << 16) >> 16));
        EXPECT_EQ(ext6.blk_nr() & 0xffff, (uint16_t)(((1 << 15) | (1 << 3))));
    }

    TEST(ExtentTest, BlockSuballoced) {
        Extent ext1(1, 0x8142, true);
        EXPECT_EQ(ext1.blk_bitmap(), (uint16_t)(0x8142));
        EXPECT_EQ(ext1.is_suballoc(), true);
    }

    TEST(ExtentTest, BlockDistanceForward) {
        Extent ref(500, 100, false);
        Extent::blk_distance_t d1 = Extent::distance_in_blks(
                ref,
                Extent(600, 10, false) /* target */
                );

        EXPECT_EQ(d1.blk_cnt, (unsigned)0);
        EXPECT_EQ(d1.is_backwards, false);
        EXPECT_EQ(d1.is_near, true);

        Extent::blk_distance_t d2 = Extent::distance_in_blks(
                ref,
                Extent(610, 10, false) /* target */
                );

        EXPECT_EQ(d2.blk_cnt, (unsigned)10);
        EXPECT_EQ(d2.is_backwards, false);
        EXPECT_EQ(d2.is_near, true);

        Extent::blk_distance_t d3 = Extent::distance_in_blks(
                ref,
                Extent(600+511, 10, false) /* target */
                );

        EXPECT_EQ(d3.blk_cnt, (unsigned)511);
        EXPECT_EQ(d3.is_backwards, false);
        EXPECT_EQ(d3.is_near, true);

        Extent::blk_distance_t d4 = Extent::distance_in_blks(
                ref,
                Extent(600+512, 10, false) /* target */
                );

        EXPECT_EQ(d4.blk_cnt, (unsigned)512);
        EXPECT_EQ(d4.is_backwards, false);
        EXPECT_EQ(d4.is_near, false);
    }

    TEST(ExtentTest, BlockDistanceForwardFromZeroRef) {
        Extent ref(0, 0, false); // zero blk extent
        Extent::blk_distance_t d1 = Extent::distance_in_blks(
                ref,
                Extent(0, 10, false) /* target, same blk nr */
                );

        EXPECT_EQ(d1.blk_cnt, (unsigned)0);
        EXPECT_EQ(d1.is_backwards, false);
        EXPECT_EQ(d1.is_near, true);
    }

    TEST(ExtentTest, BlockDistanceBackwards) {
        Extent ref(700, 100, false);
        Extent::blk_distance_t d1 = Extent::distance_in_blks(
                ref,
                Extent(600, 100, false) /* target */
                );

        EXPECT_EQ(d1.blk_cnt, (unsigned)0);
        EXPECT_EQ(d1.is_backwards, true);
        EXPECT_EQ(d1.is_near, true);

        Extent::blk_distance_t d2 = Extent::distance_in_blks(
                ref,
                Extent(590, 100, false) /* target */
                );

        EXPECT_EQ(d2.blk_cnt, (unsigned)10);
        EXPECT_EQ(d2.is_backwards, true);
        EXPECT_EQ(d2.is_near, true);

        Extent::blk_distance_t d3 = Extent::distance_in_blks(
                ref,
                Extent(600-511, 100, false) /* target */
                );

        EXPECT_EQ(d3.blk_cnt, (unsigned)511);
        EXPECT_EQ(d3.is_backwards, true);
        EXPECT_EQ(d3.is_near, true);

        Extent::blk_distance_t d4 = Extent::distance_in_blks(
                ref,
                Extent(600-512, 100, false) /* target */
                );

        EXPECT_EQ(d4.blk_cnt, (unsigned)512);
        EXPECT_EQ(d4.is_backwards, true);
        EXPECT_EQ(d4.is_near, false);
    }

    TEST(ExtentTest, BlockDistanceOverlapFailBothFullBlock) {
        Extent ref(500, 100, false);
        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001f4 00258 [  64] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (at same start)"
                        )
                    )
                )
            );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 20, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001f4 00208 [  14] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 0, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001f4 001f4 [   0] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(550, 10, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 00226 00230 [   a] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (ext start is ahead ref)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(550, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 00226 0028a [  64] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (ext start is ahead ref)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(450, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001c2 00226 [  64] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (ext start is behind ref)"
                        )
                    )
                )
        );
    }

    TEST(ExtentTest, BlockDistanceOverlapFailRefIsSuballoc) {
        Extent ref(500, 0b0000000001100100, true);
        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001f4 00258 [  64] "
                        "overlaps with the suballoc'd block "
                        "001f4 [0000000001100100] (reference extent): (at same start)"
                        )
                    )
                )
            );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 20, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001f4 00208 [  14] "
                        "overlaps with the suballoc'd block "
                        "001f4 [0000000001100100] (reference extent): (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 0, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001f4 001f4 [   0] "
                        "overlaps with the suballoc'd block "
                        "001f4 [0000000001100100] (reference extent): (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(450, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001c2 00226 [  64] "
                        "overlaps with the suballoc'd block "
                        "001f4 [0000000001100100] (reference extent): (ext start is behind ref)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(450, 51, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 001c2 001f5 [  33] "
                        "overlaps with the suballoc'd block "
                        "001f4 [0000000001100100] (reference extent): (ext start is behind ref)"
                        )
                    )
                )
        );
    }

    TEST(ExtentTest, BlockDistanceOverlapFailTargetIsSuballoc) {
        Extent ref(500, 100, false);
        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 100, true)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 001f4 [0000000001100100] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (at same start)"
                        )
                    )
                )
            );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 0, true)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 001f4 [0000000000000000] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(550, 100, true)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 00226 [0000000001100100] "
                        "overlaps with the extent "
                        "001f4 00258 [  64] (reference extent): (ext start is ahead ref)"
                        )
                    )
                )
        );
    }

    TEST(ExtentTest, SplitExtent) {
        Extent left1(1, 12, false);
        Extent right1 = left1.split(6); // half

        EXPECT_EQ(left1.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left1.blk_cnt(), (uint16_t)(6));

        EXPECT_EQ(right1.blk_nr(), (uint32_t)(0x7));
        EXPECT_EQ(right1.blk_cnt(), (uint16_t)(6));

        EXPECT_EQ(left1.blk_cnt() + right1.blk_cnt(), (uint16_t)(12));

        Extent left2(1, 12, false);
        Extent right2 = left2.split(0); // left gets 0 blocks

        EXPECT_EQ(left2.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left2.blk_cnt(), (uint16_t)(0));

        EXPECT_EQ(right2.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(right2.blk_cnt(), (uint16_t)(12));

        EXPECT_EQ(left2.blk_cnt() + right2.blk_cnt(), (uint16_t)(12));

        Extent left3(1, 12, false);
        Extent right3 = left3.split(12); // right gets 0 blocks

        EXPECT_EQ(left3.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left3.blk_cnt(), (uint16_t)(12));

        EXPECT_EQ(right3.blk_nr(), (uint32_t)(0x1 + 12));
        EXPECT_EQ(right3.blk_cnt(), (uint16_t)(0));

        EXPECT_EQ(left3.blk_cnt() + right3.blk_cnt(), (uint16_t)(12));

        Extent left4(1, 1, false);
        Extent right4 = left4.split(0); // left gets 0 blocks

        EXPECT_EQ(left4.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left4.blk_cnt(), (uint16_t)(0));

        EXPECT_EQ(right4.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(right4.blk_cnt(), (uint16_t)(1));

        EXPECT_EQ(left4.blk_cnt() + right4.blk_cnt(), (uint16_t)(1));

        Extent left5(1, 1, false);
        Extent right5 = left5.split(1); // right gets 0 blocks

        EXPECT_EQ(left5.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left5.blk_cnt(), (uint16_t)(1));

        EXPECT_EQ(right5.blk_nr(), (uint32_t)(0x1 + 1));
        EXPECT_EQ(right5.blk_cnt(), (uint16_t)(0));

        EXPECT_EQ(left5.blk_cnt() + right5.blk_cnt(), (uint16_t)(1));
    }

    TEST(ExtentTest, SplitSubAllocExtent) {
        Extent left1(1, 0xffff, true); // full 16 subblks
        Extent right1 = left1.split(8); // half

        EXPECT_EQ(left1.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left1.subblk_cnt(), (uint16_t)(8));
        EXPECT_EQ(left1.blk_bitmap(), (uint16_t)(0xff00));

        EXPECT_EQ(right1.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(right1.subblk_cnt(), (uint16_t)(8));
        EXPECT_EQ(right1.blk_bitmap(), (uint16_t)(0x00ff));

        EXPECT_EQ(left1.subblk_cnt() + right1.subblk_cnt(), (uint16_t)(16));

        Extent left2(1, 0xff0f, true); // half full 12 subblks
        Extent right2 = left2.split(0); // left gets 0 blocks

        EXPECT_EQ(left2.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left2.subblk_cnt(), (uint16_t)(0));
        EXPECT_EQ(left2.blk_bitmap(), (uint16_t)(0x0000));

        EXPECT_EQ(right2.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(right2.subblk_cnt(), (uint16_t)(12));
        EXPECT_EQ(right2.blk_bitmap(), (uint16_t)(0xff0f));

        EXPECT_EQ(left2.subblk_cnt() + right2.subblk_cnt(), (uint16_t)(12));

        Extent left3(1, 0xff0f, true);
        Extent right3 = left3.split(12); // right gets 0 blocks

        EXPECT_EQ(left3.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(left3.subblk_cnt(), (uint16_t)(12));
        EXPECT_EQ(left3.blk_bitmap(), (uint16_t)(0xff0f));

        EXPECT_EQ(right3.blk_nr(), (uint32_t)(0x1));
        EXPECT_EQ(right3.subblk_cnt(), (uint16_t)(0));
        EXPECT_EQ(right3.blk_bitmap(), (uint16_t)(0x0000));

        EXPECT_EQ(left3.subblk_cnt() + right3.subblk_cnt(), (uint16_t)(12));

        {
            Extent left3(1, 0xff0f, true);
            Extent right3 = left3.split(8); // right gets 4 blocks

            EXPECT_EQ(left3.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left3.subblk_cnt(), (uint16_t)(8));
            EXPECT_EQ(left3.blk_bitmap(), (uint16_t)(0xff00));

            EXPECT_EQ(right3.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right3.subblk_cnt(), (uint16_t)(4));
            EXPECT_EQ(right3.blk_bitmap(), (uint16_t)(0x000f));

            EXPECT_EQ(left3.subblk_cnt() + right3.subblk_cnt(), (uint16_t)(12));
        }

        {
            Extent left4(1, 0x8000, true);
            Extent right4 = left4.split(0); // left gets 0 blocks

            EXPECT_EQ(left4.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left4.subblk_cnt(), (uint16_t)(0));
            EXPECT_EQ(left4.blk_bitmap(), (uint16_t)(0x0));

            EXPECT_EQ(right4.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right4.subblk_cnt(), (uint16_t)(1));
            EXPECT_EQ(right4.blk_bitmap(), (uint16_t)(0x8000));

            EXPECT_EQ(left4.subblk_cnt() + right4.subblk_cnt(), (uint16_t)(1));
        }

        {
            Extent left4(1, 0x0100, true);
            Extent right4 = left4.split(0); // left gets 0 blocks

            EXPECT_EQ(left4.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left4.subblk_cnt(), (uint16_t)(0));
            EXPECT_EQ(left4.blk_bitmap(), (uint16_t)(0x0));

            EXPECT_EQ(right4.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right4.subblk_cnt(), (uint16_t)(1));
            EXPECT_EQ(right4.blk_bitmap(), (uint16_t)(0x0100));

            EXPECT_EQ(left4.subblk_cnt() + right4.subblk_cnt(), (uint16_t)(1));
        }

        {
            Extent left4(1, 0x0001, true);
            Extent right4 = left4.split(0); // left gets 0 blocks

            EXPECT_EQ(left4.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left4.subblk_cnt(), (uint16_t)(0));
            EXPECT_EQ(left4.blk_bitmap(), (uint16_t)(0x0));

            EXPECT_EQ(right4.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right4.subblk_cnt(), (uint16_t)(1));
            EXPECT_EQ(right4.blk_bitmap(), (uint16_t)(0x0001));

            EXPECT_EQ(left4.subblk_cnt() + right4.subblk_cnt(), (uint16_t)(1));
        }

        {
            Extent left5(1, 0x8000, true);
            Extent right5 = left5.split(1); // right gets 0 blocks

            EXPECT_EQ(left5.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left5.subblk_cnt(), (uint16_t)(1));
            EXPECT_EQ(left5.blk_bitmap(), (uint16_t)(0x8000));

            EXPECT_EQ(right5.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right5.subblk_cnt(), (uint16_t)(0));
            EXPECT_EQ(right5.blk_bitmap(), (uint16_t)(0x0));

            EXPECT_EQ(left5.subblk_cnt() + right5.subblk_cnt(), (uint16_t)(1));
        }

        {
            Extent left5(1, 0x0100, true);
            Extent right5 = left5.split(1); // right gets 0 blocks

            EXPECT_EQ(left5.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left5.subblk_cnt(), (uint16_t)(1));
            EXPECT_EQ(left5.blk_bitmap(), (uint16_t)(0x0100));

            EXPECT_EQ(right5.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right5.subblk_cnt(), (uint16_t)(0));
            EXPECT_EQ(right5.blk_bitmap(), (uint16_t)(0x0));

            EXPECT_EQ(left5.subblk_cnt() + right5.subblk_cnt(), (uint16_t)(1));
        }

        {
            Extent left5(1, 0x0001, true);
            Extent right5 = left5.split(1); // right gets 0 blocks

            EXPECT_EQ(left5.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(left5.subblk_cnt(), (uint16_t)(1));
            EXPECT_EQ(left5.blk_bitmap(), (uint16_t)(0x0001));

            EXPECT_EQ(right5.blk_nr(), (uint32_t)(0x1));
            EXPECT_EQ(right5.subblk_cnt(), (uint16_t)(0));
            EXPECT_EQ(right5.blk_bitmap(), (uint16_t)(0x0));

            EXPECT_EQ(left5.subblk_cnt() + right5.subblk_cnt(), (uint16_t)(1));
        }
    }
}
