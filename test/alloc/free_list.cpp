#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/free_list.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include <numeric>

using ::testing::IsEmpty;
using ::testing::ElementsAre;

#define XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, matcher) do {     \
        std::list<Extent> fr_extents;                           \
        fr_extents.assign((fr_list).cbegin_by_blk_nr(), (fr_list).cend_by_blk_nr());    \
        EXPECT_THAT(fr_extents, (matcher));                     \
} while (0)

#define XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, matcher) do {     \
        std::list<Extent> fr_extents;                           \
        fr_extents.assign((fr_list).cbegin_by_blk_cnt(), (fr_list).cend_by_blk_cnt());  \
        EXPECT_THAT(fr_extents, (matcher));                     \
} while (0)

namespace {
    TEST(FreeListTest, IterateOverEmptyFreeList) {
        std::list<Extent> fr_extents;
        FreeList fr_list(false, 0);

        fr_extents.clear();
        for (auto it = fr_list.cbegin_by_blk_nr(); it != fr_list.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());

        fr_extents.clear();
        for (auto it = fr_list.cbegin_by_blk_cnt(); it != fr_list.cend_by_blk_cnt(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());
    }

    TEST(FreeListTest, FreeListIteratorDereference) {
        FreeList fr_list(false, 0);

        std::list<Extent> initial_extents = {
            Extent(1, 2, false)
        };
        fr_list.initialize_from_extents(initial_extents);

        // Check that the operator* (dereference) of the iterators
        // yields the correct (single) extent.
        auto it1 = fr_list.cbegin_by_blk_nr();

        EXPECT_EQ((*it1).blk_nr(), (uint32_t)1);
        EXPECT_EQ((*it1).blk_cnt(), (uint16_t)2);
        EXPECT_EQ((*it1).is_suballoc(), (bool)false);

        auto it2 = fr_list.cbegin_by_blk_cnt();

        EXPECT_EQ((*it2).blk_nr(), (uint32_t)1);
        EXPECT_EQ((*it2).blk_cnt(), (uint16_t)2);
        EXPECT_EQ((*it2).is_suballoc(), (bool)false);

        // Operator-> (aka arrow) is supported too.
        EXPECT_EQ(it1->blk_nr(), (uint32_t)1);
        EXPECT_EQ(it1->blk_cnt(), (uint16_t)2);
        EXPECT_EQ(it1->is_suballoc(), (bool)false);

        EXPECT_EQ(it2->blk_nr(), (uint32_t)1);
        EXPECT_EQ(it2->blk_cnt(), (uint16_t)2);
        EXPECT_EQ(it2->is_suballoc(), (bool)false);
    }

    TEST(FreeListTest, IterateOverSingleElementFreeList) {
        FreeList fr_list(false, 0);

        std::list<Extent> initial_extents = {
            Extent(1, 2, false)
        };

        fr_list.initialize_from_extents(initial_extents);

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false)
                    ));
    }

    TEST(FreeListTest, IterateOverTwoElementsFreeList) {
        FreeList fr_list(false, 0);

        std::list<Extent> initial_extents = {
            Extent(1, 2, false),
            Extent(2, 3, false),
        };

        fr_list.initialize_from_extents(initial_extents);

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(2, 3, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(2, 3, false)
                    ));
    }

    TEST(FreeListTest, IterateOverThreeElementsFreeList) {
        FreeList fr_list(false, 0);

        // Note: the initial_extents is not ordered neither
        // by block number nor block count, neither in ascending
        // nor descending order.
        //
        // So when we check the content of the free list we will
        // be checking also that the free list is correctly ordered
        // by block number (cbegin_by_blk_nr / cend_by_blk_nr) and
        // by block count (cbegin_by_blk_cnt / cend_by_blk_cnt)
        std::list<Extent> initial_extents = {
            Extent(6, 3, false),
            Extent(1, 2, false),
            Extent(3, 4, false),
        };

        fr_list.initialize_from_extents(initial_extents);

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(3, 4, false),
                    Extent(6, 3, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(6, 3, false),
                    Extent(3, 4, false)
                    ));
    }
}
