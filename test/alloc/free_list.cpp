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

using ::testing_xoz::zbreak;

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

    TEST(FreeListTest, NonCoalescingDealloc) {
        // Deallocating extents in a non-coalescing free list is kind
        // of boring.
        // The test focus on the order of the extents returned by
        // the two iterators.
        FreeList fr_list(false, 0);

        fr_list.dealloc(Extent(10, 4, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "before" the previously deallocated
        // and with a block count different
        fr_list.dealloc(Extent(1, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "between" the other two
        // and with the same block count than Extent(1, 2)
        fr_list.dealloc(Extent(5, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        // another with the same block count of 2
        fr_list.dealloc(Extent(7, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "after" the others
        // and with the same block count than Extent(1, 2)
        fr_list.dealloc(Extent(16, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "after" the others
        // and with the smallest of the block counts
        fr_list.dealloc(Extent(30, 1, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false),
                    Extent(30, 1, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(30, 1, false),
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is the largest
        fr_list.dealloc(Extent(18, 10, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false),
                    Extent(18, 10, false),
                    Extent(30, 1, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(30, 1, false),
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(7, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false),
                    Extent(18, 10, false)
                    ));
    }

    TEST(FreeListTest, DeallocCoalescedWithNone) {
        // This test uses a free list with coalescing enabled but
        // the deallocated extents don't coalesce as they are not
        // one near the other (on purpose)
        //
        // This test cover the deallocation and addition of the
        // new freed extent at the begin of, at the end of and
        // when the free list was empty.
        FreeList fr_list(true, 0);

        // Testing when the free list is empty
        fr_list.dealloc(Extent(10, 4, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "before" the previously deallocated
        // and with a block count different
        fr_list.dealloc(Extent(1, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "between" the other two
        // and with the same block count than Extent(1, 2)
        fr_list.dealloc(Extent(5, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false)
                    ));

        // this deallocated extent is "after" the others
        // and with the same block count than Extent(1, 2)
        fr_list.dealloc(Extent(16, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(10, 4, false),
                    Extent(16, 2, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2, false),
                    Extent(5, 2, false),
                    Extent(16, 2, false),
                    Extent(10, 4, false)
                    ));

    }

    TEST(FreeListTest, DeallocCoalescedWithPrev) {
        // We test a new freed extent coalescing with another
        // "at its left" (or better, the previous extent
        // with a block number lower than the one being freed)
        //
        // This kind of coalescing does *not* change the block
        // number of the extents but it *does* change their
        // block count
        std::list<Extent> initial_extents = {
            Extent(1, 2, false),
            Extent(10, 2, false),
        };

        FreeList fr_list(true, 0);
        fr_list.initialize_from_extents(initial_extents);

        fr_list.dealloc(Extent(3, 4, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 2, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(10, 2, false),
                    Extent(1, 6, false)
                    ));

        fr_list.dealloc(Extent(12, 4, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));

        // note: in the fr_by_cnt, the extent are ordered by block count
        // only. In this case we also got an order by block number but
        // it is only an illusion.
        // This is because the coalesced extent Extent(10, 6, false)
        // was removed and readded to fr_by_cnt and as a side effect
        // it was put on front of the rest.
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));
    }

    TEST(FreeListTest, DeallocCoalescedWithNext) {
        // Like in DeallocCoalescedWithPrev but we test when
        // the new freed extent is "before" the already freed
        // (aka the new is coalescing with the "next" free chunk)
        //
        // This kind of coalescing does *not* change the block
        // count of the extents but it *does* change their
        // block number
        std::list<Extent> initial_extents = {
            Extent(3, 4, false),
            Extent(12, 4, false),
        };

        FreeList fr_list(true, 0);
        fr_list.initialize_from_extents(initial_extents);

        fr_list.dealloc(Extent(1, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 6, false),
                    Extent(12, 4, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(12, 4, false),
                    Extent(1, 6, false)
                    ));

        fr_list.dealloc(Extent(10, 2, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 6, false),
                    Extent(10, 6, false)
                    ));
    }

    TEST(FreeListTest, DeallocCoalescedWithPrevAndNext) {
        // We test a new freed extent coalescing with both
        // the previous and the next chunks already in the free list.
        //
        // This kind of coalescing does *not* change the block
        // number of the prev extent but it *does* change their
        // block count (as in DeallocCoalescedWithPrev)
        // but it *also* deletes the "next" chunk
        // (technically this is also what happen in DeallocCoalescedWithNext)
        //
        // Because of this "delete" effect, this kind of coalescing
        // is the only one that can "shrink" the free list with
        // less and less chunks (but with each surviving chunk larger
        // than before).
        std::list<Extent> initial_extents = {
            Extent(1, 2, false),
            Extent(4, 2, false),
            Extent(10, 2, false),
            Extent(16, 6, false),
        };

        FreeList fr_list(true, 0);
        fr_list.initialize_from_extents(initial_extents);

        fr_list.dealloc(Extent(3, 1, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2+1+2, false),
                    Extent(10, 2, false),
                    Extent(16, 6, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(10, 2, false),
                    Extent(1, 2+1+2, false),
                    Extent(16, 6, false)
                    ));

        // as side effect, there are 2 chunks now
        fr_list.dealloc(Extent(12, 4, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 2+1+2, false),
                    Extent(10, 2+4+6, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, 2+1+2, false),
                    Extent(10, 2+4+6, false)
                    ));

        // as side effect, there is 1 chunk now
        fr_list.dealloc(Extent(6, 4, false));
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, (2+1+2) + 4 + (2+4+6), false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(1, (2+1+2) + 4 + (2+4+6), false)
                    ));
    }

    TEST(FreeListTest, AllocCoalescedPerfectFit) {
        // Perfect fit means that a free chunk is entirely used
        // for the allocation and therefore, removed from
        // the free list.
        //
        // Eventually we will get with an empty free list
        std::list<Extent> initial_extents = {
            Extent(1, 3, false),
            Extent(4, 1, false),
            Extent(6, 2, false),
            Extent(9, 1, false),
        };

        FreeList fr_list(true, 0);
        fr_list.initialize_from_extents(initial_extents);

        // alloc from between chunks, bucket for 2-blocks chunks
        // get empty
        auto result1 = fr_list.alloc(2);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 3, false),
                    Extent(4, 1, false),
                    Extent(9, 1, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(4, 1, false),
                    Extent(9, 1, false),
                    Extent(1, 3, false)
                    ));

        EXPECT_EQ(result1.success, (bool)true);
        EXPECT_EQ(result1.ext, Extent(6, 2, false));

        // alloc from the end of the free list, the 1-block chunks
        // still has 1 other chunk left
        auto result2 = fr_list.alloc(1);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(1, 3, false),
                    Extent(9, 1, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(9, 1, false),
                    Extent(1, 3, false)
                    ));

        EXPECT_EQ(result2.success, (bool)true);
        EXPECT_EQ(result2.ext, Extent(4, 1, false));

        // alloc from the begin of the free list, the 3-blocks chunks
        // get empty
        auto result3 = fr_list.alloc(3);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(9, 1, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(9, 1, false)
                    ));

        EXPECT_EQ(result3.success, (bool)true);
        EXPECT_EQ(result3.ext, Extent(1, 3, false));

        // alloc again and the free list gets empty
        auto result4 = fr_list.alloc(1);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, IsEmpty());
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, IsEmpty());

        EXPECT_EQ(result4.success, (bool)true);
        EXPECT_EQ(result4.ext, Extent(9, 1, false));

    }

    TEST(FreeListTest, AllocCoalescedDoesntSuccessButClose) {
        // We are going to try to alloc more than it is free
        // and allocable so we expect to fail but also
        // the free list should recommend us which smaller
        // extent could be allocated without split.
        //
        std::list<Extent> initial_extents = {
            Extent(4, 1, false),
            Extent(8, 2, false)
        };

        FreeList fr_list(true, 0);
        fr_list.initialize_from_extents(initial_extents);

        // There is no extent free of 3 or more blocks so the
        // allocation fails but we should get at least a hint
        // of the closest extent that could work if a smaller
        // request is issued
        auto result1 = fr_list.alloc(3);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 2, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 2, false)
                    ));

        EXPECT_EQ(result1.success, (bool)false);
        EXPECT_EQ(result1.ext, Extent(0, 2, false));


        // The same but this time the free list is empty and
        // the closest extent has 0 blocks
        fr_list.clear();
        auto result2 = fr_list.alloc(2);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, IsEmpty());

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, IsEmpty());

        EXPECT_EQ(result2.success, (bool)false);
        EXPECT_EQ(result2.ext, Extent(0, 0, false));
    }

    TEST(FreeListTest, AllocCoalescedDoesntSplitButClose) {
        std::list<Extent> initial_extents = {
            Extent(4, 1, false),
            Extent(8, 3, false)
        };

        FreeList fr_list(true, /* dont_split_fr_threshold */ 1);
        fr_list.initialize_from_extents(initial_extents);

        // The free chunk of 3 blocks could be split and used
        // to allocate 2 blocks but it would leave a 1 block
        // free. The dont_split_fr_threshold == 1 forbids that
        // so the allocation fails
        auto result1 = fr_list.alloc(2);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 3, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 3, false)
                    ));

        EXPECT_EQ(result1.success, (bool)false);
        EXPECT_EQ(result1.ext, Extent(0, 1, false));


        // The same but this time there is no free chunk close enough
        // (and smaller than)
        fr_list.alloc(1); // remove Extent(4, 1, false)
        auto result2 = fr_list.alloc(2);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(8, 3, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
                    Extent(8, 3, false)
                    ));

        EXPECT_EQ(result2.success, (bool)false);
        EXPECT_EQ(result2.ext, Extent(0, 0, false));
    }

    TEST(FreeListTest, AllocCoalescedDoesntSplitButCloseSuboptimalHint) {
        std::list<Extent> initial_extents = {
            Extent(4, 1, false),
            Extent(8, 10, false)
        };

        FreeList fr_list(true, /* dont_split_fr_threshold */ 1);
        fr_list.initialize_from_extents(initial_extents);

        // The free chunk of 10 blocks could be split and used
        // to allocate 9 blocks but it would leave a 1 block
        // free. The dont_split_fr_threshold == 1 forbids that
        // so the allocation fails
        auto result1 = fr_list.alloc(9);
        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_NR(fr_list, ElementsAre(
                    Extent(4, 1, false),
                    Extent(8, 10, false)
                    ));

        XOZ_EXPECT_FREE_LIST_CONTENT_BY_BLK_CNT(fr_list, ElementsAre(
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
        // the dont_split_fr_threshold threshold and it can be a better
        // choice for the caller
        EXPECT_EQ(result1.success, (bool)false);
        EXPECT_EQ(result1.ext, Extent(0, 1, false));

    }
}
