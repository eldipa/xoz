#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/subblock_free_map.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include <numeric>

using ::testing::IsEmpty;
using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::zbreak;
using ::testing_xoz::helpers::ensure_called_once;

#define XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, matcher) do {     \
        std::list<Extent> fr_extents;                           \
        fr_extents.assign((fr_map).cbegin_by_blk_nr(), (fr_map).cend_by_blk_nr());    \
        EXPECT_THAT(fr_extents, (matcher));                     \
} while (0)


namespace {
    TEST(SubBlockFreeMapTest, IterateOverEmptyFreeMap) {
        std::list<Extent> fr_extents;
        SubBlockFreeMap fr_map;

        fr_extents.clear();
        for (auto it = fr_map.cbegin_by_blk_nr(); it != fr_map.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());
    }

    TEST(SubBlockFreeMapTest, FreeMapIteratorDereference) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 0b1000000001010011, true)
        };
        fr_map.provide(assign_extents);

        // Check that the operator* (dereference) of the iterators
        // yields the correct (single) extent.
        auto it1 = fr_map.cbegin_by_blk_nr();

        EXPECT_EQ((*it1).blk_nr(), (uint32_t)1);
        EXPECT_EQ((*it1).subblk_cnt(), (uint16_t)5);
        EXPECT_EQ((*it1).blk_bitmap(), (uint16_t)0b1000000001010011);
        EXPECT_EQ((*it1).is_suballoc(), (bool)true);

        // Operator-> (aka arrow) is supported too.
        EXPECT_EQ(it1->blk_nr(), (uint32_t)1);
        EXPECT_EQ(it1->subblk_cnt(), (uint16_t)5);
        EXPECT_EQ(it1->blk_bitmap(), (uint16_t)0b1000000001010011);
        EXPECT_EQ(it1->is_suballoc(), (bool)true);
    }

    TEST(SubBlockFreeMapTest, IterateOverSingleElementFreeMap) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 2, true)
        };

        fr_map.provide(assign_extents);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, true)
                    ));

    }

    TEST(SubBlockFreeMapTest, IterateOverTwoElementsFreeMap) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 1, true),
            Extent(2, 3, true),
        };

        fr_map.provide(assign_extents);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 1, true),
                    Extent(2, 3, true)
                    ));

    }

    TEST(SubBlockFreeMapTest, IterateOverThreeElementsFreeMap) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(7, 3, true),
            Extent(1, 2, true),
            Extent(3, 4, true),
        };

        fr_map.provide(assign_extents);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, true),
                    Extent(3, 4, true),
                    Extent(7, 3, true)
                    ));

    }

    TEST(SubBlockFreeMapTest, DeallocPartiallyIntoANewFreeBlock) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 0b0000000011110000, true), // subblk_cnt 4
        };

        fr_map.provide(assign_extents);

        // Dealloc a novel extent. It will be stored in the same
        // bin that the Extent at blk_nr 1
        fr_map.dealloc(Extent(2, 0b0000000011110000, true));    // subblk_cnt 4
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0000000011110000, true),
                    Extent(2, 0b0000000011110000, true)
                    ));

        // Dealloc a novel extent. It will be stored in a new bin.
        fr_map.dealloc(Extent(7, 0b0000000000000011, true));  // subblk_cnt 2
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0000000011110000, true),
                    Extent(2, 0b0000000011110000, true),
                    Extent(7, 0b0000000000000011, true)
                    ));
    }

    TEST(SubBlockFreeMapTest, DeallocFullyIntoANewFreeBlock) {
        SubBlockFreeMap fr_map;

        // Dealloc a novel extent. It will be open a new bin.
        fr_map.dealloc(Extent(2, 0b1111111111111111, true));    // subblk_cnt 16
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(2, 0b1111111111111111, true)
                    ));

        // Dealloc a novel extent. It will be stored in the same bin
        // above
        fr_map.dealloc(Extent(7, 0b1111111111111111, true));  // subblk_cnt 16
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(2, 0b1111111111111111, true),
                    Extent(7, 0b1111111111111111, true)
                    ));
    }


    TEST(SubBlockFreeMapTest, DeallocPartiallyIntoAPartiallyFreeBlock) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 0b0000000011110000, true), // subblk_cnt 4
        };

        fr_map.provide(assign_extents);

        // Dealloc the same block number but with a different bitmask.
        fr_map.dealloc(Extent(1, 0b0000000100001000, true));    // subblk_cnt 2
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0000000111111000, true)  // subblk_cnt 2+4 = 6
                    ));

        // Dealloc the same block, making it fully deallocated (free)
        fr_map.dealloc(Extent(1, 0b1111111000000111, true));  // subblk_cnt 10
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b1111111111111111, true)  // subblk_cnt 6+10 = 16
                    ));
    }


    TEST(SubBlockFreeMapTest, AllocPartiallyFromFullyFreeBlock) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 0b1111111111111111, true), // subblk_cnt 16
        };

        fr_map.provide(assign_extents);

        // Alloc 4 subblocks. The first MSB bits should be used.
        auto result1 = fr_map.alloc(4);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0000111111111111, true)
                    ));

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(1, 0b1111000000000000, true));

        // Free 1 subblock so the free mask is not contiguous
        fr_map.dealloc(Extent(1, 0b0110000000000000, true));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0110111111111111, true)
                    ));

        // Alloc 1 subblocks. The first MSB bits should be used.
        auto result2 = fr_map.alloc(1);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0010111111111111, true)
                    ));

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(1, 0b0100000000000000, true));

        // Alloc 6 subblocks. The first MSB bits should be used.
        auto result3 = fr_map.alloc(6);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0000000001111111, true)
                    ));

        EXPECT_EQ(result3.success, (bool)true);
        EXPECT_EQ(result3.ext, Extent(1, 0b0010111110000000, true));
    }

    TEST(SubBlockFreeMapTest, AllocPartiallyFromSameBinAndBlockGetsFullyUsed) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(1, 0b0000000000000111, true), // subblk_cnt 3
        };

        fr_map.provide(assign_extents);

        // Alloc 3 subblocks. Perfect match. Extent removed from
        // the free map.
        auto result1 = fr_map.alloc(3);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, IsEmpty());

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(1, 0b0000000000000111, true));
    }

    TEST(SubBlockFreeMapTest, AllocPartiallyFromBestBinPossible) {
        SubBlockFreeMap fr_map;

        std::list<Extent> assign_extents = {
            Extent(4, 0b0000000000000111, true), // subblk_cnt 3
            Extent(7, 0b0010000111111100, true), // subblk_cnt 8
            Extent(2, 0b0011100000000000, true), // subblk_cnt 3
            Extent(1, 0b0010000000000000, true), // subblk_cnt 1
        };

        fr_map.provide(assign_extents);

        // Alloc 3 subblocks. Perfect match. Extent(2, ...) removed from
        // the free map.
        auto result1 = fr_map.alloc(3);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0010000000000000, true), // subblk_cnt 1
                    Extent(4, 0b0000000000000111, true), // subblk_cnt 3
                    Extent(7, 0b0010000111111100, true)  // subblk_cnt 8
                    ));

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(2, 0b0011100000000000, true));

        // Alloc 2 subblocks. No perfect match, extract subblocks from
        // the one with the smallest blkcount.
        auto result2 = fr_map.alloc(2);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 0b0010000000000000, true), // subblk_cnt 1
                    Extent(4, 0b0000000000000001, true), // subblk_cnt 1 <--
                    Extent(7, 0b0010000111111100, true)  // subblk_cnt 8
                    ));

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(4, 0b0000000000000110, true));
    }


    TEST(SubBlockFreeMapTest, AssignWithDuplicatedBlkNumberError) {
        // Despite having different bitmaps, these two extent have
        // the same block number and provide does not support that
        std::list<Extent> assign_extents = {
            Extent(4, 0b1111000000000000, true),
            Extent(4, 0b0000000011111111, true),
        };

        SubBlockFreeMap fr_map;

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.provide(assign_extents); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block "
                        "00004 [0000000011111111]"
                        " (to be freed) overlaps with the suballoc'd block "
                        "00004 [1111000000000000]"
                        " (already freed): "
                        "both have the same block number (bitmap ignored in the check)"
                        )
                    )
                )
        );
    }

    TEST(SubBlockFreeMapTest, AssignWithZeroSubBlocksOrNonSubAllocExtentsIsAnError) {
        std::list<Extent> assign_extents_1 = {
            Extent(4, 0x0000, true), // subblk_cnt = 0
        };

        std::list<Extent> assign_extents_2 = {
            Extent(4, 0x00ff, false), // is_suballoc is False
        };

        SubBlockFreeMap fr_map;

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.provide(assign_extents_1); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc 0 subblocks")
                    )
                )
        );

        fr_map.clear();
        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.provide(assign_extents_2); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc extent that it is not for suballocation")
                    )
                )
        );
    }

    TEST(SubBlockFreeMapTest, InvalidAllocOfZeroSubBlocks) {
        SubBlockFreeMap fr_map;

        EXPECT_THAT(
            [&]() { fr_map.alloc(0); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot alloc 0 subblocks")
                    )
                )
        );
    }

    TEST(SubBlockFreeMapTest, InvalidDeallocOfZeroSubBlocks) {
        SubBlockFreeMap fr_map;

        EXPECT_THAT(
            [&]() { fr_map.dealloc(Extent(4, 0, true)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc 0 subblocks")
                    )
                )
        );
    }

    TEST(SubBlockFreeMapTest, InvalidDeallocOfBlockNotForSuballocation) {
        SubBlockFreeMap fr_map;

        EXPECT_THAT(
            [&]() { fr_map.dealloc(Extent(4, 4, false)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc extent that it is not for suballocation")
                    )
                )
        );
    }

    TEST(SubBlockFreeMapTest, InvalidDoubleFree) {
        std::list<Extent> assign_extents = {
            Extent(4, 0b0000111100000000, true),
        };

        SubBlockFreeMap fr_map;
        fr_map.provide(assign_extents);

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(4, 0b0000100000000000, true)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block "
                        "00004 [0000100000000000]"
                        " (to be freed) overlaps with the suballoc'd block "
                        "00004 [0000111100000000]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(4, 0b0000111100000000, true)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block "
                        "00004 [0000111100000000]"
                        " (to be freed) overlaps with the suballoc'd block "
                        "00004 [0000111100000000]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(4, 0b1000111100000000, true)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block "
                        "00004 [1000111100000000]"
                        " (to be freed) overlaps with the suballoc'd block "
                        "00004 [0000111100000000]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

    }
}

