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
        /*
        sz = 0                          -> err ?
        sz = SUBLK_SZ / 2               -> inline ?
        sz = SUBLK_SZ - 1               -> inline ?
        */
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

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x3fff, true)
                    ));
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

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)1);

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

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)3);

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

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)1);

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

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[1].blk_cnt(), (uint16_t)1);

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

        EXPECT_EQ(segm.exts()[1].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[1].blk_cnt(), (uint16_t)1);

        EXPECT_EQ(segm.exts()[2].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[2].subblk_cnt(), (uint16_t)1);

        // N blocks in the first extent; 1 in the next extent and
        // only then 1 suballocated extent so block number is N+2
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(Extent::MAX_BLK_CNT+2, 0x7fff, true)
                    ));
    }
}

