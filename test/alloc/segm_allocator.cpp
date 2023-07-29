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

    TEST(SegmentAllocatorTest, AllocAndGrow) {
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x7fff, true)
                    ));
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x3fff, true)
                    ));
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

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
        EXPECT_EQ(segm.inline_data().size(), (size_t)(repo.subblk_sz() - 1));

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(Extent::SUBBLK_CNT_PER_BLK - 1));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x0001, true)
                    ));
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // The allocator is "tight" or "conservative" and allocated 1 block only
        // as this was the minimum to fulfill the request.
        // There are no free space left.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

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
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

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
        EXPECT_EQ(segm.inline_data().size(), (size_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[1].blk_cnt(), (uint16_t)1);
        EXPECT_EQ(segm.exts()[1].blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT+1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

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
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        sg_alloc.dealloc(segm);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data().size(), (size_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
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

        // No block can be freed by the tail allocator
        // (the repository) because the third segment is still in use.
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)7);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)6);


        // Dealloc the third segment (3 blocks).
        // These 3 blocks  should be coalesced with the blocks
        // of the second segment (2 blocks).
        sg_alloc.dealloc(segm3);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 5, false) // coalesced
                    ));

        // Then all of them released into the tail allocator
        // shrinking the repository size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

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
    }

    TEST(SegmentAllocatorTest, DellocAndReleaseSomeBlksThenAllWithoutCoalescing) {
        const GlobalParameters gp = {
            .blk_sz = 64,
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, SegmentAllocator::MaxInlineSize, false);

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

        // No block can be freed by the tail allocator
        // (the repository) because the third segment is still in use.
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)7);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)6);


        // Dealloc the third segment (3 blocks).
        // These 3 blocks  should not be coalesced with the blocks
        // of the second segment (2 blocks).
        sg_alloc.dealloc(segm3);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false),
                    Extent(4, 3, false)
                    ));

        // Then all of them released into the tail allocator
        // shrinking the repository size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

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
        EXPECT_EQ(segm1.inline_data().size(), (size_t)0);

        EXPECT_EQ(segm1.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm1.exts()[0].subblk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm1.exts()[0].blk_bitmap(), (uint16_t)(0xe000));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x1fff, true)
                    ));

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

        // Dealloc the first segment, its subblocks should be deallocated
        // but the 1 block holding them should not
        sg_alloc.dealloc(segm1);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        // Note the extent bitmask 1110 1111 1111 1111
        //                            ^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0xe7ff, true)
                    ));

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
        EXPECT_EQ(segm1.inline_data().size(), (size_t)0);

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
    }

    TEST(SegmentAllocatorTest, AllocMoreThanInlineAllow) {
        const GlobalParameters gp = {
            .blk_sz = 128,
            .blk_sz_order = 7,
            .blk_init_cnt = 1
        };

        const uint16_t MaxInlineSize = 4;

        Repository repo = Repository::create_mem_based(0, gp);
        SegmentAllocator sg_alloc(repo, MaxInlineSize);

        // Sanity check: the point is that we are allocating
        // Max+1 and that trigger to do the allocation in a subblock
        // The test makes no sense *if* that Max+1 is already of
        // the size of a subblk or larger as storing there is the
        // default in that case.
        // So we check that Max+1 is lower than subblock sz
        //EXPECT_EQ((MaxInlineSize + 1 < sg_alloc.subblk_sz()), (bool)(true));

        // Alloc Max bytes, expected to be all inline'd.
        Segment segm1 = sg_alloc.alloc(MaxInlineSize);

        EXPECT_EQ(segm1.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(MaxInlineSize));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm1.inline_data().size(), (size_t)(MaxInlineSize));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        // Alloc Max+ bytes, expected to be all in a subblock
        Segment segm2 = sg_alloc.alloc(MaxInlineSize + 1);

        // Note that the usable size is the subblock size
        // which it is >= than the requested size as the request couldn't
        // be fit into the inline space because it was larger than
        // the maximum.
        EXPECT_EQ(segm2.calc_usable_space_size(repo.params().blk_sz_order), (uint32_t)(repo.subblk_sz()));

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)2);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm2.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm2.inline_data().size(), (size_t)(0));

        EXPECT_EQ(segm2.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm2.exts()[0].subblk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(1));

        EXPECT_EQ(segm2.exts()[0].blk_bitmap(), (uint16_t)(0x8000));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x7fff, true)
                    ));
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
        EXPECT_EQ(segm.inline_data().size(), (size_t)(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        sg_alloc.dealloc(segm);

        EXPECT_EQ(repo.begin_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.past_end_data_blk_nr(), (uint32_t)1);
        EXPECT_EQ(repo.data_blk_cnt(), (uint32_t)0);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
    }
}

