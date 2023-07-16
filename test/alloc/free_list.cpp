#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/free_list.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include <numeric>

namespace {
    TEST(FreeListTest, IterateOverEmptyFreeList) {
        std::list<Extent> fr_extents;
        FreeList fr_list(false, 0);

        fr_extents.clear();
        for (auto it = fr_list.cbegin_by_blk_nr(); it != fr_list.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_EQ(fr_extents.size(), (size_t)0);

        fr_extents.clear();
        for (auto it = fr_list.cbegin_by_blk_cnt(); it != fr_list.cend_by_blk_cnt(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_EQ(fr_extents.size(), (size_t)0);
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

}
