#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

namespace {
    TEST(ExtentTest, BlockNumberBits) {
        // Block numbers are 26 bits long
        // This test check that the 25th bit is preserved (being 0th the lowest)
        // and the 26th is dropped (because it would require 27 bits)
        Extent ext1((1 << 25) | (1 << 26), 1, false);
        EXPECT_EQ(ext1.blk_nr(), (uint32_t)(1 << 25));

        // Suballoc'd does not change the above
        Extent ext2((1 << 25) | (1 << 26), 1, true);
        EXPECT_EQ(ext2.blk_nr(), (uint32_t)(1 << 25));

        // Check higher bits are preserved when hi_blk_nr() is used
        Extent ext3((1 << 25) | (1 << 26), 1, false);
        EXPECT_EQ(ext3.hi_blk_nr(), (uint16_t)((1 << 25) >> 16));

        // Check lower bits
        Extent ext4((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext4.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));

        // Suballoc'd does not change the above
        Extent ext5((1 << 15) | (1 << 3), 1, true);
        EXPECT_EQ(ext5.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));

        // Check higher and lower bits
        Extent ext6((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext6.hi_blk_nr(), (uint16_t)(0));
        EXPECT_EQ(ext6.lo_blk_nr(), (uint16_t)(((1 << 15) | (1 << 3))));
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
                        "The extent of blocks [500 to 600) "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (at same start)"
                        )
                    )
                )
            );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 20, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [500 to 520) "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 0, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [500 to 500) "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(550, 10, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [550 to 560) "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (ext start is ahead ref)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(550, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [550 to 650) "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (ext start is ahead ref)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(450, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [450 to 550) "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (ext start is behind ref)"
                        )
                    )
                )
        );
    }

    TEST(ExtentTest, BlockDistanceOverlapFailRefIsSuballoc) {
        Extent ref(500, 100, true);
        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [500 to 600) "
                        "overlaps with the reference suballoc'd block "
                        "500. (at same start)"
                        )
                    )
                )
            );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 20, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [500 to 520) "
                        "overlaps with the reference suballoc'd block "
                        "500. (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 0, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [500 to 500) "
                        "overlaps with the reference suballoc'd block "
                        "500. (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(450, 100, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [450 to 550) "
                        "overlaps with the reference suballoc'd block "
                        "500. (ext start is behind ref)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(450, 51, false)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent of blocks [450 to 501) "
                        "overlaps with the reference suballoc'd block "
                        "500. (ext start is behind ref)"
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
                        "The suballoc'd block 500 "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (at same start)"
                        )
                    )
                )
            );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(500, 0, true)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 500 "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (at same start)"
                        )
                    )
                )
        );

        EXPECT_THAT(
            [&]() { Extent::distance_in_blks(ref, Extent(550, 100, true)); },
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 550 "
                        "overlaps with the reference extent of "
                        "blocks [500 to 600). (ext start is ahead ref)"
                        )
                    )
                )
        );

    }
}
