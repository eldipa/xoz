#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/free_map.h"

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

#define XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, matcher) do {     \
        std::list<Extent> fr_extents;                           \
        fr_extents.assign((fr_map).cbegin_by_blk_cnt(), (fr_map).cend_by_blk_cnt());  \
        EXPECT_THAT(fr_extents, (matcher));                     \
} while (0)

namespace {
    TEST(FreeMapTest, IterateOverEmptyFreeMap) {
        std::list<Extent> fr_extents;
        FreeMap fr_map(false, 0);

        fr_extents.clear();
        for (auto it = fr_map.cbegin_by_blk_nr(); it != fr_map.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());

        fr_extents.clear();
        for (auto it = fr_map.cbegin_by_blk_cnt(); it != fr_map.cend_by_blk_cnt(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());
    }

    TEST(FreeMapTest, FreeMapIteratorDereference) {
        FreeMap fr_map(false, 0);

        std::list<Extent> assign_extents = {
            Extent(1, 2, false)
        };
        fr_map.provide(assign_extents);

        // Check that the operator* (dereference) of the iterators
        // yields the correct (single) extent.
        auto it1 = fr_map.cbegin_by_blk_nr();

        EXPECT_EQ((*it1).blk_nr(), (uint32_t)1);
        EXPECT_EQ((*it1).blk_cnt(), (uint16_t)2);
        EXPECT_EQ((*it1).is_suballoc(), (bool)false);

        auto it2 = fr_map.cbegin_by_blk_cnt();

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

    TEST(FreeMapTest, IterateOverSingleElementFreeMap) {
        FreeMap fr_map(false, 0);

        std::list<Extent> assign_extents = {
            Extent(1, 2, false)
        };

        fr_map.provide(assign_extents);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false)
                    ));
    }

    TEST(FreeMapTest, IterateOverTwoElementsFreeMap) {
        FreeMap fr_map(false, 0);

        std::list<Extent> assign_extents = {
            Extent(1, 1, false),
            Extent(2, 3, false),
        };

        fr_map.provide(assign_extents);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 1, false),
                    Extent(2, 3, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 1, false),
                    Extent(2, 3, false)
                    ));

        // Test iterate by blk number in reverse order
        std::list<Extent> fr_extents;
        for (auto it = fr_map.crbegin_by_blk_nr(); it != fr_map.crend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        EXPECT_THAT(fr_extents, ElementsAre(
                    Extent(2, 3, false),
                    Extent(1, 1, false)
                    ));
    }

    TEST(FreeMapTest, IterateOverThreeElementsFreeMap) {
        FreeMap fr_map(false, 0);

        // Note: the assign_extents is not ordered neither
        // by block number nor block count, neither in ascending
        // nor descending order.
        //
        // So when we check the content of the free map we will
        // be checking also that the free map is correctly ordered
        // by block number (cbegin_by_blk_nr / cend_by_blk_nr) and
        // by block count (cbegin_by_blk_cnt / cend_by_blk_cnt)
        std::list<Extent> assign_extents = {
            Extent(7, 3, false),
            Extent(1, 2, false),
            Extent(3, 4, false),
        };

        fr_map.provide(assign_extents);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(3, 4, false),
                    Extent(7, 3, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(7, 3, false),
                    Extent(3, 4, false)
                    ));
    }

    TEST(FreeMapTest, NonCoalescingDealloc) {
        // Deallocating extents in a non-coalescing free map is kind
        // of boring.
        // The test focus on the order of the extents returned by
        // the two iterators.
        FreeMap fr_map(false, 0);

        fr_map.dealloc(Extent(10, 4, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "before" the previously deallocated
        // and with a block count different
        fr_map.dealloc(Extent(1, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "between" the other two
        // and with the same block count than Extent(1, 2)
        fr_map.dealloc(Extent(5, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        // another with the same block count of 2
        fr_map.dealloc(Extent(7, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "after" the others
        // and with the same block count than Extent(1, 2)
        fr_map.dealloc(Extent(16, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "after" the others
        // and with the smallest of the block counts
        fr_map.dealloc(Extent(30, 1, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false),
                    Extent(30, 1, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(30, 1, false),
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is the largest
        fr_map.dealloc(Extent(18, 10, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false),
                    Extent(18, 10, false),
                    Extent(30, 1, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(30, 1, false),
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false),
                    Extent(18, 10, false)
                    ));
    }

    TEST(FreeMapTest, DeallocCoalescedWithNone) {
        // This test uses a free map with coalescing enabled but
        // the deallocated extents don't coalesce as they are not
        // one near the other (on purpose)
        //
        // This test cover the deallocation and addition of the
        // new freed extent at the begin of, at the end of and
        // when the free map was empty.
        FreeMap fr_map(true, 0);

        // Testing when the free map is empty
        fr_map.dealloc(Extent(10, 4, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "before" the previously deallocated
        // and with a block count different
        fr_map.dealloc(Extent(1, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "between" the other two
        // and with the same block count than Extent(1, 2)
        fr_map.dealloc(Extent(5, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "after" the others
        // and with the same block count than Extent(1, 2)
        fr_map.dealloc(Extent(16, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false)
                    ));

    }

    TEST(FreeMapTest, DeallocCoalescedWithPrev) {
        // We test a new freed extent coalescing with another
        // "at its left" (or better, the previous extent
        // with a block number lower than the one being freed)
        //
        // This kind of coalescing does *not* change the block
        // number of the extents but it *does* change their
        // block count
        std::list<Extent> assign_extents = {
            Extent(1, 2, false),
            Extent(10, 2, false),
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);

        fr_map.dealloc(Extent(3, 4, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(10, 2, false),
                    Extent(1, 6, false)
                    ));

        fr_map.dealloc(Extent(12, 4, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));

        // note: in the fr_by_cnt, the extent are ordered by block count
        // only. In this case we also got an order by block number but
        // it is only an illusion.
        // This is because the coalesced extent Extent(10, 6, false)
        // was removed and readded to fr_by_cnt and as a side effect
        // it was put on front of the rest.
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));
    }

    TEST(FreeMapTest, DeallocCoalescedWithNext) {
        // Like in DeallocCoalescedWithPrev but we test when
        // the new freed extent is "before" the already freed
        // (aka the new is coalescing with the "next" free chunk)
        //
        // This kind of coalescing does *not* change the block
        // count of the extents but it *does* change their
        // block number
        std::list<Extent> assign_extents = {
            Extent(3, 4, false),
            Extent(12, 4, false),
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);

        fr_map.dealloc(Extent(1, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 6, false),
                    Extent(12, 4, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(12, 4, false),
                    Extent(1, 6, false)
                    ));

        fr_map.dealloc(Extent(10, 2, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));
    }

    TEST(FreeMapTest, DeallocCoalescedWithPrevAndNext) {
        // We test a new freed extent coalescing with both
        // the previous and the next chunks already in the free map .
        //
        // This kind of coalescing does *not* change the block
        // number of the prev extent but it *does* change their
        // block count (as in DeallocCoalescedWithPrev)
        // but it *also* deletes the "next" chunk
        // (technically this is also what happen in DeallocCoalescedWithNext)
        //
        // Because of this "delete" effect, this kind of coalescing
        // is the only one that can "shrink" the free map with
        // less and less chunks (but with each surviving chunk larger
        // than before).
        std::list<Extent> assign_extents = {
            Extent(1, 2, false),
            Extent(4, 2, false),
            Extent(10, 2, false),
            Extent(16, 6, false),
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);

        fr_map.dealloc(Extent(3, 1, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2+1+2, false),
                    Extent(10, 2, false),
                    Extent(16, 6, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(10, 2, false),
                    Extent(1, 2+1+2, false),
                    Extent(16, 6, false)
                    ));

        // as side effect, there are 2 chunks now
        fr_map.dealloc(Extent(12, 4, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 2+1+2, false),
                    Extent(10, 2+4+6, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 2+1+2, false),
                    Extent(10, 2+4+6, false)
                    ));

        // as side effect, there is 1 chunk now
        fr_map.dealloc(Extent(6, 4, false));
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, (2+1+2) + 4 + (2+4+6), false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, (2+1+2) + 4 + (2+4+6), false)
                    ));
    }

    TEST(FreeMapTest, AllocCoalescedPerfectFit) {
        // Perfect fit means that a free chunk is entirely used
        // for the allocation and therefore, removed from
        // the free map .
        //
        // Eventually we will get with an empty free map
        std::list<Extent> assign_extents = {
            Extent(1, 3, false),
            Extent(5, 1, false),
            Extent(7, 2, false),
            Extent(10, 1, false),
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);

        // alloc from between chunks, bucket for 2-blocks chunks
        // get empty
        auto result1 = fr_map.alloc(2);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 3, false),
                    Extent(5, 1, false),
                    Extent(10, 1, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(5, 1, false),
                    Extent(10, 1, false),
                    Extent(1, 3, false)
                    ));

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(7, 2, false));

        // alloc from the end of the free map, the 1-block chunks
        // still has 1 other chunk left
        auto result2 = fr_map.alloc(1);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 3, false),
                    Extent(10, 1, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(10, 1, false),
                    Extent(1, 3, false)
                    ));

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(5, 1, false));

        // alloc from the begin of the free map, the 3-blocks chunks
        // get empty
        auto result3 = fr_map.alloc(3);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(10, 1, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(10, 1, false)
                    ));

        EXPECT_EQ(result3.success, (bool)true);
        EXPECT_EQ(result3.ext, Extent(1, 3, false));

        // alloc again and the free map gets empty
        auto result4 = fr_map.alloc(1);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, IsEmpty());
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, IsEmpty());

        EXPECT_EQ(result4.success, (bool)true);
        EXPECT_EQ(result4.ext, Extent(10, 1, false));

    }

    TEST(FreeMapTest, AllocCoalescedDoesntSuccessButClose) {
        // We are going to try to alloc more than it is free
        // and allocable so we expect to fail but also
        // the free map should recommend us which smaller
        // extent could be allocated without split.
        //
        std::list<Extent> assign_extents = {
            Extent(4, 1, false),
            Extent(8, 2, false)
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);

        // There is no extent free of 3 or more blocks so the
        // allocation fails but we should get at least a hint
        // of the closest extent that could work if a smaller
        // request is issued
        auto result1 = fr_map.alloc(3);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 2, false)
                    ));

        EXPECT_EQ(result1.success, (bool)false);
        EXPECT_EQ(result1.ext, Extent(0, 2, false));


        // The same but this time the free map is empty and
        // the closest extent has 0 blocks
        fr_map.release_all();
        auto result2 = fr_map.alloc(2);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, IsEmpty());

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, IsEmpty());

        EXPECT_EQ(result2.success, (bool)false);
        EXPECT_EQ(result2.ext, Extent(0, 0, false));
    }

    TEST(FreeMapTest, AllocCoalescedDoesntSplitButClose) {
        std::list<Extent> assign_extents = {
            Extent(4, 1, false),
            Extent(8, 3, false)
        };

        FreeMap fr_map(true, /* split_above_threshold */ 1);
        fr_map.provide(assign_extents);

        // The free chunk of 3 blocks could be split and used
        // to allocate 2 blocks but it would leave a 1 block
        // free. The split_above_threshold == 1 forbids that
        // so the allocation fails
        auto result1 = fr_map.alloc(2);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 3, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 3, false)
                    ));

        EXPECT_EQ(result1.success, (bool)false);
        EXPECT_EQ(result1.ext, Extent(0, 1, false));


        // The same but this time there is no free chunk close enough
        // (and smaller than)
        fr_map.alloc(1); // remove Extent(4, 1, false)
        auto result2 = fr_map.alloc(2);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(8, 3, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(8, 3, false)
                    ));

        EXPECT_EQ(result2.success, (bool)false);
        EXPECT_EQ(result2.ext, Extent(0, 0, false));
    }

    TEST(FreeMapTest, AllocCoalescedDoesntSplitButCloseSuboptimalHint) {
        std::list<Extent> assign_extents = {
            Extent(4, 1, false),
            Extent(8, 10, false)
        };

        FreeMap fr_map(true, /* split_above_threshold */ 1);
        fr_map.provide(assign_extents);

        // The free chunk of 10 blocks could be split and used
        // to allocate 9 blocks but it would leave a 1 block
        // free. The split_above_threshold == 1 forbids that
        // so the allocation fails
        auto result1 = fr_map.alloc(9);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 10, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 10, false)
                    ));

        // The issue:
        //
        // The implementation is suggesting a smaller allocation of
        // 1 block because that can be done without split but
        // this is suboptimal and the implementation *can do it better*
        //
        // The extent Extent(8, 10, false) cannot be split into 9 and 1 blocks
        // but it *can* be split into 8 and 2 blocks as this is above
        // the split_above_threshold threshold and it can be a better
        // choice for the caller
        EXPECT_EQ(result1.success, (bool)false);
        EXPECT_EQ(result1.ext, Extent(0, 1, false));

    }

    TEST(FreeMapTest, AllocCoalescedSplitNoThreshold) {
        std::list<Extent> assign_extents = {
            Extent(4, 2, false),
            Extent(8, 5, false),
            Extent(15, 6, false)
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);


        // Alloc 4 blocks: take the first free chunk large enough and
        // split it.
        auto result1 = fr_map.alloc(4);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 2, false),
                    Extent(12, 1, false),
                    Extent(15, 6, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(12, 1, false),
                    Extent(4, 2, false),
                    Extent(15, 6, false)
                    ));

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(8, 4, false));


        auto result2 = fr_map.alloc(4);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 2, false),
                    Extent(12, 1, false),
                    Extent(19, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(12, 1, false),
                    Extent(4, 2, false),
                    Extent(19, 2, false)
                    ));

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(15, 4, false));
    }

    TEST(FreeMapTest, AllocCoalescedSplitWithThreshold) {
        std::list<Extent> assign_extents = {
            Extent(4, 2, false),
            Extent(8, 5, false),
            Extent(15, 6, false)
        };

        FreeMap fr_map(true, /* split_above_threshold */ 1);
        fr_map.provide(assign_extents);


        // Alloc 4 blocks: take the first free chunk large enough and
        // split it but only if after the split the remaining free blocks
        // are more than split_above_threshold
        //
        // So the Extent(8, 5, false) is skipped and Extent(15, 6, false)
        // is used instead
        auto result1 = fr_map.alloc(4);
        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 2, false),
                    Extent(8, 5, false),
                    Extent(19, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(4, 2, false),
                    Extent(19, 2, false),
                    Extent(8, 5, false)
                    ));

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(15, 4, false));
    }

    TEST(FreeMapTest, ProvideTwice) {
        std::list<Extent> assign_extents_1 = {
            Extent(4, 2, false),
        };
        std::list<Extent> assign_extents_2 = {
            Extent(1, 3, false),
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents_1);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(4, 2, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(4, 2, false)
                    ));

        fr_map.provide(assign_extents_2);

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_NR(fr_map, ElementsAre(
                    Extent(1, 5, false)
                    ));

        XOZ_EXPECT_FREE_MAP_CONTENT_BY_BLK_CNT(fr_map, ElementsAre(
                    Extent(1, 5, false)
                    ));
    }

    TEST(FreeMapTest, AssignWithOverlappingIsAnError) {
        std::list<Extent> assign_extents = {
            Extent(4, 2, false),
            Extent(3, 2, false),
        };

        FreeMap fr_map(true, 0);

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.provide(assign_extents); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00003 00005 [   2]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );
    }

    TEST(FreeMapTest, AssignWithZeroBlockExtentsIsAnError) {
        std::list<Extent> assign_extents = {
            Extent(4, 0, false),
        };

        FreeMap fr_map(true, 0);

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.provide(assign_extents); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc 0 blocks")
                    )
                )
        );
    }

    TEST(FreeMapTest, InvalidAllocOfZeroBlocks) {
        FreeMap fr_map(true, 0);

        EXPECT_THAT(
            [&]() { fr_map.alloc(0); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot alloc 0 blocks")
                    )
                )
        );
    }

    TEST(FreeMapTest, InvalidDeallocOfZeroBlocks) {
        FreeMap fr_map(true, 0);

        EXPECT_THAT(
            [&]() { fr_map.dealloc(Extent(4, 0, false)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc 0 blocks")
                    )
                )
        );
    }

    TEST(FreeMapTest, InvalidDeallocOfSuballocatedBlock) {
        FreeMap fr_map(true, 0);

        EXPECT_THAT(
            [&]() { fr_map.dealloc(Extent(4, 4, true)); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("cannot dealloc suballoc extent")
                    )
                )
        );
    }

    TEST(FreeMapTest, InvalidDoubleFree) {
        std::list<Extent> assign_extents = {
            Extent(4, 2, false),
        };

        FreeMap fr_map(true, 0);
        fr_map.provide(assign_extents);

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(4, 4, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00004 00008 [   4]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(4, 1, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00004 00005 [   1]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(4, 2, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00004 00006 [   2]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(5, 2, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00005 00007 [   2]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(5, 1, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00005 00006 [   1]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(3, 2, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00003 00005 [   2]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { fr_map.dealloc(Extent(3, 4, false)); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent "
                        "00003 00007 [   4]"
                        " (to be freed) overlaps with the extent "
                        "00004 00006 [   2]"
                        " (already freed): "
                        "possible double free detected"
                        )
                    )
                )
        );
    }
}
