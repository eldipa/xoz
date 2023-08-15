#include "xoz/repo/repo.h"
#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/segm_allocator.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing::IsEmpty;
using ::testing::ElementsAre;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::subvec;
using ::testing_xoz::helpers::ensure_called_once;

// Check that the serialization of the extents in fp are of the
// expected size (call calc_footprint_disk_size) and they match
// byte-by-byte with the expected data (in hexdump)
#define XOZ_EXPECT_REPO_SERIALIZATION(repo, at, len, data) do {           \
    EXPECT_EQ(hexdump((repo).expose_mem_fp(), (at), (len)), (data));              \
} while (0)

#define XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, matcher) do {     \
        std::list<Extent> fr_extents;                           \
        fr_extents.assign((sg_alloc).cbegin_by_blk_nr(), (sg_alloc).cend_by_blk_nr());    \
        EXPECT_THAT(fr_extents, (matcher));                     \
} while (0)

namespace {
    TEST(SegmentAllocatorTest, IterateOverEmptyFreeMap) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        std::list<Extent> fr_extents;
        fr_extents.clear();
        for (auto it = sg_alloc.cbegin_by_blk_nr(); it != sg_alloc.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());
    }

    TEST(SegmentAllocatorTest, NoAllocs) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        XOZ_EXPECT_REPO_SERIALIZATION(repo, 64, -1,
                "0000 0000"
                );

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(0));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocOneByte) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc 1 byte so we expect to have 0 blocks allocated
        // in the repository (and in the segment) and 1 byte
        // inline'd in the segment.
        Segment segm = sg_alloc.alloc(1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocOneSubBlk) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would completely fill a single subblk
        // so we expect to have 1 blocks allocated
        // in the repository and 1 in the segment as for suballocation
        // with 1 sub block inside and 0 bytes inline'd.
        Segment segm = sg_alloc.alloc(repo.subblk_sz());

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x7fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.subblk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocTwoSubBlks) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would completely fill a 2 subblks
        // so we expect to have 1 blocks allocated
        // in the repository and 1 in the segment as for suballocation
        // with 2 sub block inside and 0 bytes inline'd.
        Segment segm = sg_alloc.alloc(repo.subblk_sz() << 1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz() << 1));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x3fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.subblk_sz() << 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(2));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 2) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, IterateOverSingleElementFreeMap) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would completely fill a 2 subblks
        // so we expect to have 1 blocks allocated
        // in the repository and 1 in the segment as for suballocation
        // with 2 sub block inside and 0 bytes inline'd.
        Segment segm = sg_alloc.alloc(repo.subblk_sz() << 1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz() << 1));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // Test *it
        std::list<Extent> fr_extents;
        for (auto it = sg_alloc.cbegin_by_blk_nr(); it != sg_alloc.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        EXPECT_THAT(fr_extents, ElementsAre(
                    Extent(1, 0x3fff, true)
                    ));

        // Test it->
        std::list<uint32_t> fr_blk_nr;
        for (auto it = sg_alloc.cbegin_by_blk_nr(); it != sg_alloc.cend_by_blk_nr(); ++it) {
            fr_blk_nr.push_back(it->blk_nr());
        }

        EXPECT_THAT(fr_blk_nr, ElementsAre(1));
    }

    TEST(SegmentAllocatorTest, AllocAlmostFullSingleBlk) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would "almost" completely fill a single block
        // with only 1 byte missed.
        //
        // So we expect to have 1 blocks allocated
        // in the repository and 1 in the segment as for suballocation
        // with 15 sub block inside and (SUBLK_SZ - 1) bytes inline'd
        // (we are not applying any restriction to use less inline space
        // so the allocator is allocating "full" subblocks and the rest
        // goes to the inline space directly
        Segment segm = sg_alloc.alloc((repo.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK) - 1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(repo.subblk_sz() - 1));

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(Extent::SUBBLK_CNT_PER_BLK - 1));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x0001, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t((repo.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK) - 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(Extent::SUBBLK_CNT_PER_BLK - 1));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(repo.subblk_sz() - 1));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((1) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleBlk) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would completely fill a single block,
        // no more, no less.
        //
        // So we expect to have 1 blocks allocated
        // in the repository and 1 extent in the segment with
        // 1 block and 0 inline'd data.
        Segment segm = sg_alloc.alloc(repo.blk_sz());

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz()));
        EXPECT_EQ((repo.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK), repo.blk_sz());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // The allocator is "tight" or "conservative" and allocated 1 block only
        // as this was the minimum to fulfill the request.
        // There are no free space left.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleBlkPlusOneByte) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would completely fill a single block
        // with 1 additional byte.
        //
        // So we expect to have 1 blocks allocated
        // in the repository and 1 extent in the segment with
        // 1 block and 1 inline'd data.
        Segment segm = sg_alloc.alloc(repo.blk_sz() + 1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() + 1));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() + 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleBlkPlusOneSubBlk) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc N bytes that would completely fill a single block
        // and 1 additional subblock.
        //
        // So we expect to have 2 blocks allocated
        // in the repository: 1 extent of 1 block and 1 extent
        // of 1 subblock and 0 inline'd data.
        Segment segm = sg_alloc.alloc(repo.blk_sz() + repo.subblk_sz());

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() + repo.subblk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)3);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)2);

        EXPECT_EQ(segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[1].blk_nr(), (uint32_t)(2));

        // note the block number: the first blk (1) was used to
        // fulfill the entire block request and the second (2)
        // to fulfill the subblock part
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 0x7fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() + repo.subblk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocMultiBlkAndSubBlkButFitInTwoExtents) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // We expect to have 1 extent with  2 blocks allocated
        // and another extent for suballoc with 3 subblocks
        // plus 1 byte inline'd
        Segment segm = sg_alloc.alloc(2 * repo.blk_sz() + 3 * repo.subblk_sz() + 1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(2 * repo.blk_sz() + 3 * repo.subblk_sz() + 1));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)3);

        EXPECT_EQ(segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)2);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)3);
        EXPECT_EQ(segm.exts()[1].blk_nr(), (uint32_t)(3));

        // The first allocated extent owned 2 blocks, the third
        // block was suballocated so in the free map we have
        // a single extent at block number 3
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(3, 0x1fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(2 * repo.blk_sz() + 3 * repo.subblk_sz() + 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(3));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(3));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 3) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtent) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold.
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * repo.blk_sz());

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(Extent::MAX_BLK_CNT * repo.blk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * repo.blk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneByte) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 byte inline'd
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * repo.blk_sz() + 1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(Extent::MAX_BLK_CNT * repo.blk_sz() + 1));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * repo.blk_sz() + 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneSubBlk) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 additional extent for suballoc
        // for 1 subblock.
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.subblk_sz());

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.subblk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 2));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT + 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[1].blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT+1));

        // N full blocks allocated and the N+1 for suballocation
        // so that the one it is still (partially) free
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(Extent::MAX_BLK_CNT+1, 0x7fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.subblk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneBlk) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 additional extent for another block
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.blk_sz());

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.blk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 2));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT + 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[1].blk_cnt(), (uint16_t)1);
        EXPECT_EQ(segm.exts()[1].blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT+1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.blk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneBlkOneSubBlkPlusOneByte) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 additional extent for 1 block
        // plus another additional extent for suballoc for 1 subblock
        // plus 1 byte inline'd
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.blk_sz() + repo.subblk_sz() + 1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.blk_sz() + repo.subblk_sz() + 1));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 3));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT + 2);

        EXPECT_EQ(segm.ext_cnt(), (size_t)3);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[1].blk_cnt(), (uint16_t)1);
        EXPECT_EQ(segm.exts()[1].blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT+1));

        EXPECT_EQ(segm.exts()[2].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[2].subblk_cnt(), (uint16_t)1);
        EXPECT_EQ(segm.exts()[2].blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT+2));

        // N blocks in the first extent; 1 in the next extent and
        // only then 1 suballocated extent so block number is N+2
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(Extent::MAX_BLK_CNT+2, 0x7fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * repo.blk_sz() + repo.blk_sz() + repo.subblk_sz() + 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT + 2));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(3));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,1,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocWithoutSuballoc) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 2,
            .max_inline_sz = 4,
            .allow_suballoc = false
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // This will not require a full block because it fits in the inline space
        Segment segm1 = sg_alloc.alloc(req.max_inline_sz, req);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(req.max_inline_sz));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(1));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)req.max_inline_sz);


        // This will require a full block because it doesn't fit in the inline space
        // and suballoc is disabled
        Segment segm2 = sg_alloc.alloc(req.max_inline_sz + 1, req);

        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)(2));
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm2.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm2.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm2.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm2.exts()[0].blk_cnt(), (uint16_t)1);
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(req.max_inline_sz + repo.blk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(req.max_inline_sz));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(repo.blk_sz() - (req.max_inline_sz + 1)));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(1,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, DeallocNoneAsAllItsInlined) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc 1 byte so we expect to have 0 blocks allocated
        // in the repository (and in the segment) and 1 byte
        // inline'd in the segment.
        Segment segm = sg_alloc.alloc(1);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        sg_alloc.dealloc(segm);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, DellocAndReleaseSomeBlksThenAllWithCoalescing) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc 3 segments of 1, 2 and 3 blocks each (6 blocks in total)
        Segment segm1 = sg_alloc.alloc(repo.blk_sz() * 1);
        Segment segm2 = sg_alloc.alloc(repo.blk_sz() * 2);
        Segment segm3 = sg_alloc.alloc(repo.blk_sz() * 3);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 1));
        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2));
        EXPECT_EQ(segm3.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 3));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)7);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)6);

        // Dealloc the second segment (2 blocks).
        sg_alloc.dealloc(segm2);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 4));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 2));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        // No block can be freed by the tail allocator
        // (the repository) because the third segment is still in use.
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)7);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)6);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 4));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 2));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));


        // Dealloc the third segment (3 blocks).
        // These 3 blocks  should be coalesced with the blocks
        // of the second segment (2 blocks).
        sg_alloc.dealloc(segm3);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 5, false) // coalesced
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 5));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        // Then all of them released into the tail allocator
        // shrinking the repository size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        // Dealloc the first segment (1 blocks).
        sg_alloc.dealloc(segm1);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        // Then all of them released into the tail allocator
        // shrinking the repository size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(3));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, DellocAndReleaseSomeBlksThenAllWithoutCoalescing) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, false);

        // Alloc 3 segments of 1, 2 and 3 blocks each (6 blocks in total)
        Segment segm1 = sg_alloc.alloc(repo.blk_sz() * 1);
        Segment segm2 = sg_alloc.alloc(repo.blk_sz() * 2);
        Segment segm3 = sg_alloc.alloc(repo.blk_sz() * 3);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 1));
        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2));
        EXPECT_EQ(segm3.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 3));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)7);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)6);

        // Dealloc the second segment (2 blocks).
        sg_alloc.dealloc(segm2);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 4));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 2));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        // No block can be freed by the tail allocator
        // (the repository) because the third segment is still in use.
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)7);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)6);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 4));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 2));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));


        // Dealloc the third segment (3 blocks).
        // These 3 blocks  should not be coalesced with the blocks
        // of the second segment (2 blocks).
        sg_alloc.dealloc(segm3);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false),
                    Extent(4, 3, false)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 5));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        // Then all of them released into the tail allocator
        // shrinking the repository size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 1));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        // Dealloc the first segment (1 blocks).
        sg_alloc.dealloc(segm1);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        // Then all of them released into the tail allocator
        // shrinking the repository size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(3));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, DellocSomeSubBlksThenAll) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc 3 subblocks which requires allocate 1 block
        Segment segm1 = sg_alloc.alloc(repo.subblk_sz() * 3);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz() * 3));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm1.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm1.exts()[0].subblk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm1.exts()[0].blk_bitmap(), (uint16_t)(0xe000));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x1fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.subblk_sz() * 3));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(3));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 3) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        // Alloc 2 subblocks more reusing the previously allocated 1 block
        Segment segm2 = sg_alloc.alloc(repo.subblk_sz() * 2);

        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz() * 2));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm2.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm2.exts()[0].subblk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm2.exts()[0].blk_bitmap(), (uint16_t)(0x1800));

        // Note the extent bitmask 0000 1111 1111 1111
        //                         ^^^^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x07ff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.subblk_sz() * 5));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(5));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 5) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        // Dealloc the first segment, its subblocks should be deallocated
        // but the 1 block holding them should not
        sg_alloc.dealloc(segm1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        // Note the extent bitmask 1110 0111 1111 1111
        //                            ^ ^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0xe7ff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.subblk_sz() * 2));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(2));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 2) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        // Dealloc the second segment, now the 1 block should be deallocated too
        // however this does not implies a reduction of the repository size
        sg_alloc.dealloc(segm2);

        // This is unchanged
        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        // Note how the extent for suballocation was changed
        // to a normal extent. This means that the subblock_free_map
        // released the block back to block_free_map
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        // Free blocks remain which results in external fragmentation
        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 1));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }


    TEST(SegmentAllocatorTest, DellocSomeBlksThenAllWithCoalescing) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Alloc 2 blks + 3 subblocks which requires allocate 3 block
        // in total
        Segment segm1 = sg_alloc.alloc(repo.blk_sz() * 2 + repo.subblk_sz() * 3);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2 + repo.subblk_sz() * 3));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)4);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)3);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm1.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm1.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm1.exts()[1].subblk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm1.exts()[1].blk_nr(), (uint32_t)(3));

        EXPECT_EQ(segm1.exts()[1].blk_bitmap(), (uint16_t)(0xe000));

        // All the remaining subblocks in that last block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(3, 0x1fff, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 2 + repo.subblk_sz() * 3));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(3));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(3));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 3) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        // Alloc 1 block and 2 subblocks more. These subblocks will be
        // reusing the previously allocated 1 block
        Segment segm2 = sg_alloc.alloc(repo.blk_sz() + repo.subblk_sz() * 2);

        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() + repo.subblk_sz() * 2));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)5);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)4);

        EXPECT_EQ(segm2.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm2.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(4));

        EXPECT_EQ(segm2.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm2.exts()[1].subblk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm2.exts()[1].blk_nr(), (uint32_t)(3));

        EXPECT_EQ(segm2.exts()[1].blk_bitmap(), (uint16_t)(0x1800));

        // Note the extent bitmask 0000 1111 1111 1111
        //                         ^^^^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(3, 0x07ff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 3 + repo.subblk_sz() * 5));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(5));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(4));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 5) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,2,0,0,0,0,0));

        // Dealloc the first segment, its blocks and subblocks should be deallocated
        // but the 1 block holding the subblocks should not
        sg_alloc.dealloc(segm1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)5);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)4);

        // Note the extent bitmask 1110 1111 1111 1111
        //                            ^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 2, false),
                    Extent(3, 0xe7ff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(repo.blk_sz() * 1 + repo.subblk_sz() * 2));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(2));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 2));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 2) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        // Dealloc the second segment
        sg_alloc.dealloc(segm2);

        // This is unchanged
        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)5);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)4);

        // Note how freeing the block for suballocation allowed
        // the merge (coalescing) of the extents of the segment 1
        // and the segments 2 to form a single large extent free.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 4, false)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(repo.blk_sz() * 4));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocMoreThanInlineAllow) {
        const GlobalParameters gp = {
            .blk_sz = 128,
            .blk_sz_order = 7,
            .blk_init_cnt = 1
        };

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 2,
            .max_inline_sz = 4,
            .allow_suballoc = true
        };

        const uint8_t MaxInlineSize = req.max_inline_sz;

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        // Sanity check: the point is that we are allocating
        // Max+1 and that trigger to do the allocation in a subblock
        // The test makes no sense *if* that Max+1 is already of
        // the size of a subblk or larger as storing there is the
        // default in that case.
        // So we check that Max+1 is lower than subblock sz
        //EXPECT_EQ((MaxInlineSize + 1 < sg_alloc.subblk_sz()), (bool)(true));

        // Alloc Max bytes, expected to be all inline'd.
        Segment segm1 = sg_alloc.alloc(MaxInlineSize, req);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(MaxInlineSize));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(MaxInlineSize));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(MaxInlineSize));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(MaxInlineSize));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

        // Alloc Max+ bytes, expected to be all in a subblock
        Segment segm2 = sg_alloc.alloc(MaxInlineSize + 1, req);

        // Note that the usable size is the subblock size
        // which it is >= than the requested size as the request couldn't
        // be fit into the inline space because it was larger than
        // the maximum.
        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm2.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm2.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm2.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm2.exts()[0].subblk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm2.exts()[0].blk_bitmap(), (uint16_t)(0x8000));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x7fff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(MaxInlineSize + repo.subblk_sz()));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(MaxInlineSize));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(repo.subblk_sz() - (MaxInlineSize + 1)));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * repo.subblk_sz()));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(1,1,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, AllocAndDeallocZeroBytes) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo);

        Segment segm = sg_alloc.alloc(0);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(0));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

        sg_alloc.dealloc(segm);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        sg_alloc.release();

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));
    }

    TEST(SegmentAllocatorTest, ForceTailAllocCoalescedWithFree) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 1,
            .max_inline_sz = 8,
            .allow_suballoc = true
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, true);

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(repo.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // Now, let's see what happen if we try to allocate an segment
        // of 2 blocks where there is no single 2-block extent free.
        //
        // Because split_above_threshold is 0, the allocator is not
        // allowed to split the 2 blocks into 2 extents of 1 block each,
        // forcing the allocator to request more space from the repository.
        //
        // Because SegmentAllocator is configured with coalescing enabled,
        // the request of 2 blocks can be fulfilled using the last free
        // 1-block extent plus a new 1-block extent from the repository.
        //
        // This is possible because the free extent is at the end of the
        // free map and it will be coalesced with any new extent.
        //
        // This translate in the repository to grow by 1 block and not
        // by 2.

        Segment segm = sg_alloc.alloc(repo.blk_sz() * 2, req);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)17);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)16);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(15));

        // Note how the free map didn't change *except*
        // the last extent at the end of the repository *before*
        // the last allocation that it is *not* longer free.
        //
        // This is because SegmentAllocator used to partially fulfill
        // the request.
        //
        // This works only if coalescing is enabled.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false)
                    ));
    }

    TEST(SegmentAllocatorTest, ForceTailAllocButCoalescedIsDisabled) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 1,
            .max_inline_sz = 8,
            .allow_suballoc = true
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, false);

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(repo.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // Now, let's see what happen if we try to allocate an segment
        // of 2 blocks where there is no single 2-block extent free.
        //
        // Because split_above_threshold is 0, the allocator is not
        // allowed to split the 2 blocks into 2 extents of 1 block each,
        // forcing the allocator to request more space from the repository.
        //
        // Because SegmentAllocator is configured with coalescing disabled,
        // the allocator is forced to allocate the requested blocks without
        // the possibility to combine it with the last free blocks (even
        // if the combination results in a single contiguos extent).
        Segment segm = sg_alloc.alloc(repo.blk_sz() * 2, req);

        EXPECT_EQ(segm.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)18);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)17);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(16));

        // Note how the free map didn't change
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));
    }

    TEST(SegmentAllocatorTest, ForceSplitOnce) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 2,
            .max_inline_sz = 8,
            .allow_suballoc = true
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, true);

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(repo.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // Because we allow up to a segment fragmentation of 2, this 2-block
        // request can be fulfilled allocation 2 separated 1-block extents
        Segment segm1 = sg_alloc.alloc(repo.blk_sz() * 2, req);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));
        EXPECT_EQ(segm1.exts()[1].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm1.exts()[1].blk_nr(), (uint32_t)(3));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // This 3-block request can be fulfilled with one 1-block
        // and one 2-block extents.
        // Because there is no 2-block extents free, this alloc will
        // force the tail allocator to alloc more blocks and the repo
        // will grow (by 1 block)
        Segment segm2 = sg_alloc.alloc(repo.blk_sz() * 3, req);

        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 3));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)17);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)16);

        EXPECT_EQ(segm2.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm2.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm2.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(5));
        EXPECT_EQ(segm2.exts()[1].blk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm2.exts()[1].blk_nr(), (uint32_t)(15));

        // Note how the free extent at blk nr 5 was used and also
        // the one at blk nr 15. This last one, of 1-block, was coalesced
        // with the 1-block new (tail allocator) to fulfill the remaining
        // 2-blocks.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false)
                    ));

        Segment segm3 = sg_alloc.alloc(repo.blk_sz() * 4, req);

        EXPECT_EQ(segm3.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 4));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)20);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)19);

        EXPECT_EQ(segm3.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm3.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm3.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm3.exts()[0].blk_nr(), (uint32_t)(7));
        EXPECT_EQ(segm3.exts()[1].blk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm3.exts()[1].blk_nr(), (uint32_t)(17));

        // Note how the free extent at blk nr 7 was used to fill 1-block.
        // For the remaining 3-blocks an entire 2-block was obtained
        // from the repository.
        // The last free extent at blk nr 13 was *not* used because
        // it is not at the end of the repository.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false)
                    ));
    }

    TEST(SegmentAllocatorTest, ForceSplitTwice) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 3,
            .max_inline_sz = 8,
            .allow_suballoc = true
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, true);

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(repo.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // Because we allow up to a segment fragmentation of 3, this 2-block
        // request can be fulfilled allocation 2 separated 1-block extents
        Segment segm1 = sg_alloc.alloc(repo.blk_sz() * 2, req);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 2));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));
        EXPECT_EQ(segm1.exts()[1].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm1.exts()[1].blk_nr(), (uint32_t)(3));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // This 3-block request can be fulfilled with three 1-block
        // block extents.
        Segment segm2 = sg_alloc.alloc(repo.blk_sz() * 3, req);

        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 3));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)16);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)15);

        EXPECT_EQ(segm2.ext_cnt(), (size_t)3);
        EXPECT_EQ(segm2.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm2.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(5));
        EXPECT_EQ(segm2.exts()[1].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[1].blk_nr(), (uint32_t)(7));
        EXPECT_EQ(segm2.exts()[2].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[2].blk_nr(), (uint32_t)(9));

        // All the 3 blks were taken from three 1-block extents already free
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        Segment segm3 = sg_alloc.alloc(repo.blk_sz() * 4, req);

        EXPECT_EQ(segm3.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.blk_sz() * 4));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)17);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)16);

        EXPECT_EQ(segm3.ext_cnt(), (size_t)3);
        EXPECT_EQ(segm3.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm3.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm3.exts()[0].blk_nr(), (uint32_t)(11));
        EXPECT_EQ(segm3.exts()[1].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm3.exts()[1].blk_nr(), (uint32_t)(13));
        EXPECT_EQ(segm3.exts()[2].blk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm3.exts()[2].blk_nr(), (uint32_t)(15));

        // This last 4-block allocation consumed the first two 1-block free extents.
        // The third and last free extent was of 1-block size so it couldn't
        // fulfill the remaining 2-blocks.
        // This forced to the repo to grow by 1 block, coalesce that
        // block with the last block free to form a 2-block extent
        // and use that to fulfill the request.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
    }
}

