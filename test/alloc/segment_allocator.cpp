#include "xoz/blk/file_block_array.h"
#include "xoz/ext/extent.h"
#include "xoz/err/exceptions.h"
#include "xoz/alloc/segment_allocator.h"
#include "xoz/io/iosegment.h"

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
using ::testing_xoz::helpers::are_all_zeros;

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

#define XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, matcher) do {     \
        std::list<Extent> fr_extents;                           \
        fr_extents.assign((sg_alloc).cbegin_by_blk_nr(), (sg_alloc).cend_by_blk_nr());    \
        EXPECT_THAT(fr_extents, (matcher));                     \
} while (0)

#define XOZ_EXPECT_ALL_ZERO_STATS(st) do { \
        EXPECT_THAT(are_all_zeros((char*)&(st), sizeof(st)), (bool)true); \
} while (0)

namespace {
    TEST(SegmentAllocatorTest, IterateOverEmptyFreeMap) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        std::list<Extent> fr_extents;
        fr_extents.clear();
        for (auto it = sg_alloc.cbegin_by_blk_nr(); it != sg_alloc.cend_by_blk_nr(); ++it) {
            fr_extents.push_back(*it);
        }

        // Expected to be empty
        EXPECT_THAT(fr_extents, IsEmpty());
    }

    TEST(SegmentAllocatorTest, NoAllocs) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();
        XOZ_EXPECT_ALL_ZERO_STATS(stats.current);
        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocOneByte) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 1 byte so we expect to have 0 blocks allocated
        // in the xoz file (and in the segment) and 1 byte
        // inline'd in the segment.
        Segment segm = sg_alloc.alloc(1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)1);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocOneSubBlk) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would completely fill a single subblk
        // so we expect to have 1 blocks allocated
        // in the xoz file and 1 in the segment as for suballocation
        // with 1 sub block inside and 0 bytes inline'd.
        Segment segm = sg_alloc.alloc(blkarr.subblk_sz());

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.subblk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocTwoSubBlks) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would completely fill a 2 subblks
        // so we expect to have 1 blocks allocated
        // in the xoz file and 1 in the segment as for suballocation
        // with 2 sub block inside and 0 bytes inline'd.
        Segment segm = sg_alloc.alloc(blkarr.subblk_sz() << 1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.subblk_sz() << 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz() << 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 2) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, IterateOverSingleElementFreeMap) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would completely fill a 2 subblks
        // so we expect to have 1 blocks allocated
        // in the xoz file and 1 in the segment as for suballocation
        // with 2 sub block inside and 0 bytes inline'd.
        Segment segm = sg_alloc.alloc(blkarr.subblk_sz() << 1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.subblk_sz() << 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would "almost" completely fill a single block
        // with only 1 byte missed.
        //
        // So we expect to have 1 blocks allocated
        // in the xoz file and 1 in the segment as for suballocation
        // with 15 sub block inside and (SUBLK_SZ - 1) bytes inline'd
        // (we are not applying any restriction to use less inline space
        // so the allocator is allocating "full" subblocks and the rest
        // goes to the inline space directly
        Segment segm = sg_alloc.alloc((blkarr.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK) - 1);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(blkarr.subblk_sz() - 1));

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)true);
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)(Extent::SUBBLK_CNT_PER_BLK - 1));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        // All the remaining subblocks in that block remain free
        // to be used later
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0x0001, true)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t((blkarr.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK) - 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(Extent::SUBBLK_CNT_PER_BLK - 1));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(blkarr.subblk_sz() - 1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((1) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleBlk) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would completely fill a single block,
        // no more, no less.
        //
        // So we expect to have 1 blocks allocated
        // in the xoz file and 1 extent in the segment with
        // 1 block and 0 inline'd data.
        Segment segm = sg_alloc.alloc(blkarr.blk_sz());

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.blk_sz()));
        EXPECT_EQ((blkarr.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK), blkarr.blk_sz());

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleBlkPlusOneByte) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would completely fill a single block
        // with 1 additional byte.
        //
        // So we expect to have 1 blocks allocated
        // in the xoz file and 1 extent in the segment with
        // 1 block and 1 inline'd data.
        Segment segm = sg_alloc.alloc(blkarr.blk_sz() + 1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() + 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() + 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleBlkPlusOneSubBlk) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc N bytes that would completely fill a single block
        // and 1 additional subblock.
        //
        // So we expect to have 2 blocks allocated
        // in the xoz file: 1 extent of 1 block and 1 extent
        // of 1 subblock and 0 inline'd data.
        Segment segm = sg_alloc.alloc(blkarr.blk_sz() + blkarr.subblk_sz());

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() + blkarr.subblk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)2);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() + blkarr.subblk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocMultiBlkAndSubBlkButFitInTwoExtents) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // We expect to have 1 extent with  2 blocks allocated
        // and another extent for suballoc with 3 subblocks
        // plus 1 byte inline'd
        Segment segm = sg_alloc.alloc(2 * blkarr.blk_sz() + 3 * blkarr.subblk_sz() + 1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(2 * blkarr.blk_sz() + 3 * blkarr.subblk_sz() + 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)3);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(2 * blkarr.blk_sz() + 3 * blkarr.subblk_sz() + 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(3));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 3) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtent) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold.
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * blkarr.blk_sz());

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(Extent::MAX_BLK_CNT * blkarr.blk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * blkarr.blk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneByte) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 byte inline'd
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * blkarr.blk_sz() + 1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(Extent::MAX_BLK_CNT * blkarr.blk_sz() + 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        EXPECT_EQ(segm.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint16_t)Extent::MAX_BLK_CNT);
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * blkarr.blk_sz() + 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneSubBlk) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 additional extent for suballoc
        // for 1 subblock.
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.subblk_sz());

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.subblk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 2));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT + 1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.subblk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneBlk) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 additional extent for another block
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.blk_sz());

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.blk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 2));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT + 1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.blk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT + 1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocFullSingleExtentPlusOneBlkOneSubBlkPlusOneByte) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // We expect to have 1 extent with N blocks allocated
        // where N is the maximum amount of blocks that a single
        // extent can hold plus 1 additional extent for 1 block
        // plus another additional extent for suballoc for 1 subblock
        // plus 1 byte inline'd
        Segment segm = sg_alloc.alloc(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.blk_sz() + blkarr.subblk_sz() + 1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.blk_sz() + blkarr.subblk_sz() + 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(Extent::MAX_BLK_CNT + 3));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)Extent::MAX_BLK_CNT + 2);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(Extent::MAX_BLK_CNT * blkarr.blk_sz() + blkarr.blk_sz() + blkarr.subblk_sz() + 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(Extent::MAX_BLK_CNT + 2));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,1,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocWithoutSuballoc) {

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 2,
            .max_inline_sz = 4,
            .allow_suballoc = false,
            .single_extent = false
        };

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // This will not require a full block because it fits in the inline space
        Segment segm1 = sg_alloc.alloc(req.max_inline_sz, req);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(req.max_inline_sz));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)req.max_inline_sz);


        // This will require a full block because it doesn't fit in the inline space
        // and suballoc is disabled
        Segment segm2 = sg_alloc.alloc(req.max_inline_sz + 1, req);

        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(2));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        EXPECT_EQ(segm2.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm2.inline_data_sz(), (uint8_t)0);

        EXPECT_EQ(segm2.exts()[0].is_suballoc(), (bool)false);
        EXPECT_EQ(segm2.exts()[0].blk_cnt(), (uint16_t)1);
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(1));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(req.max_inline_sz + blkarr.blk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(req.max_inline_sz));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, DeallocNoneAsAllItsInlined) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 1 byte so we expect to have 0 blocks allocated
        // in the xoz file (and in the segment) and 1 byte
        // inline'd in the segment.
        Segment segm = sg_alloc.alloc(1);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)1);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));

        sg_alloc.dealloc(segm);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, DellocAndReleaseSomeBlksThenAllWithCoalescing) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 3 segments of 1, 2 and 3 blocks each (6 blocks in total)
        Segment segm1 = sg_alloc.alloc(blkarr.blk_sz() * 1);
        Segment segm2 = sg_alloc.alloc(blkarr.blk_sz() * 2);
        Segment segm3 = sg_alloc.alloc(blkarr.blk_sz() * 3);

        auto stats = sg_alloc.stats();
        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32 * 3));

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 1));
        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));
        EXPECT_EQ(segm3.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)7);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)6);

        // Dealloc the second segment (2 blocks).
        sg_alloc.dealloc(segm2);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 4));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 2));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32  * 2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // No block can be freed by the tail allocator
        // (the xoz file) because the third segment is still in use.
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)7);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)6);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 4));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 2));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32 * 2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));


        // Dealloc the third segment (3 blocks).
        // These 3 blocks  should be coalesced with the blocks
        // of the second segment (2 blocks).
        sg_alloc.dealloc(segm3);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 5, false) // coalesced
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 5));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Then all of them released into the tail allocator
        // shrinking the xoz file size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Dealloc the first segment (1 blocks).
        sg_alloc.dealloc(segm1);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        // Then all of them released into the tail allocator
        // shrinking the xoz file size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(3));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, DellocAndReleaseSomeBlksThenAllWithoutCoalescing) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(false);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 3 segments of 1, 2 and 3 blocks each (6 blocks in total)
        Segment segm1 = sg_alloc.alloc(blkarr.blk_sz() * 1);
        Segment segm2 = sg_alloc.alloc(blkarr.blk_sz() * 2);
        Segment segm3 = sg_alloc.alloc(blkarr.blk_sz() * 3);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 1));
        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));
        EXPECT_EQ(segm3.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)7);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)6);

        // Dealloc the second segment (2 blocks).
        sg_alloc.dealloc(segm2);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 4));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 2));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32 * 2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // No block can be freed by the tail allocator
        // (the xoz file) because the third segment is still in use.
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false)
                    ));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)7);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)6);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 4));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 2));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32 * 2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));


        // Dealloc the third segment (3 blocks).
        // These 3 blocks  should not be coalesced with the blocks
        // of the second segment (2 blocks).
        sg_alloc.dealloc(segm3);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(2, 2, false),
                    Extent(4, 3, false)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 5));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Then all of them released into the tail allocator
        // shrinking the xoz file size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Dealloc the first segment (1 blocks).
        sg_alloc.dealloc(segm1);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        // Then all of them released into the tail allocator
        // shrinking the xoz file size (block count).
        sg_alloc.release();
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(3));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, DellocSomeSubBlksThenAll) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 3 subblocks which requires allocate 1 block
        Segment segm1 = sg_alloc.alloc(blkarr.subblk_sz() * 3);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.subblk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz() * 3));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(3));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 3) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Alloc 2 subblocks more reusing the previously allocated 1 block
        Segment segm2 = sg_alloc.alloc(blkarr.subblk_sz() * 2);

        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.subblk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz() * 5));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(5));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(4));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 5) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,2,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Dealloc the first segment, its subblocks should be deallocated
        // but the 1 block holding them should not
        sg_alloc.dealloc(segm1);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        // Note the extent bitmask 1110 0111 1111 1111
        //                            ^ ^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 0xe7ff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz() * 2));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 2) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Dealloc the second segment, now the 1 block should be deallocated too
        // however this does not implies a reduction of the xoz file size
        sg_alloc.dealloc(segm2);

        // This is unchanged
        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        // Note how the extent for suballocation was changed
        // to a normal extent. This means that the subblock_free_map
        // released the block back to block_free_map
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        // Free blocks remain which results in external fragmentation
        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }


    TEST(SegmentAllocatorTest, DellocSomeBlksThenAllWithCoalescing) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 2 blks + 3 subblocks which requires allocate 3 block
        // in total
        Segment segm1 = sg_alloc.alloc(blkarr.blk_sz() * 2 + blkarr.subblk_sz() * 3);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2 + blkarr.subblk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)4);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)3);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 2 + blkarr.subblk_sz() * 3));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(3));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(3));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 3) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Alloc 1 block and 2 subblocks more. These subblocks will be
        // reusing the previously allocated 1 block
        Segment segm2 = sg_alloc.alloc(blkarr.blk_sz() + blkarr.subblk_sz() * 2);

        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() + blkarr.subblk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)4);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 3 + blkarr.subblk_sz() * 5));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(4));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(5));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(4));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(4));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 5) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,2,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Dealloc the first segment, its blocks and subblocks should be deallocated
        // but the 1 block holding the subblocks should not
        sg_alloc.dealloc(segm1);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)4);

        // Note the extent bitmask 1110 1111 1111 1111
        //                            ^
        //                            marked as used
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 2, false),
                    Extent(3, 0xe7ff, true)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1 + blkarr.subblk_sz() * 2));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 2));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 2) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Dealloc the second segment
        sg_alloc.dealloc(segm2);

        // This is unchanged
        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)5);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)4);

        // Note how freeing the block for suballocation allowed
        // the merge (coalescing) of the extents of the segment 1
        // and the segments 2 to form a single large extent free.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 4, false)
                    ));

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(blkarr.blk_sz() * 4));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocMoreThanInlineAllow) {

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 2,
            .max_inline_sz = 4,
            .allow_suballoc = true,
            .single_extent = false
        };

        const uint8_t MaxInlineSize = req.max_inline_sz;

        auto blkarr_ptr = FileBlockArray::create_mem_based(128, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Sanity check: the point is that we are allocating
        // Max+1 and that trigger to do the allocation in a subblock
        // The test makes no sense *if* that Max+1 is already of
        // the size of a subblk or larger as storing there is the
        // default in that case.
        // So we check that Max+1 is lower than subblock sz
        //EXPECT_EQ((MaxInlineSize + 1 < sg_alloc.subblk_sz()), (bool)(true));

        // Alloc Max bytes, expected to be all inline'd.
        Segment segm1 = sg_alloc.alloc(MaxInlineSize, req);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(MaxInlineSize));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(MaxInlineSize));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(MaxInlineSize));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(MaxInlineSize));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        // Alloc Max+ bytes, expected to be all in a subblock
        Segment segm2 = sg_alloc.alloc(MaxInlineSize + 1, req);

        // Note that the usable size is the subblock size
        // which it is >= than the requested size as the request couldn't
        // be fit into the inline space because it was larger than
        // the maximum.
        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.subblk_sz()));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(MaxInlineSize + blkarr.subblk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(MaxInlineSize));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(4));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((Extent::SUBBLK_CNT_PER_BLK - 1) * blkarr.subblk_sz()));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, AllocAndDeallocZeroBytes) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        Segment segm = sg_alloc.alloc(0);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(0));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        sg_alloc.dealloc(segm);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        sg_alloc.release();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());

        stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, ForceTailAllocCoalescedWithFree) {

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 1,
            .max_inline_sz = 8,
            .allow_suballoc = true,
            .single_extent = false
        };

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(blkarr.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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
        // forcing the allocator to request more space from the xoz file.
        //
        // Because SegmentAllocator is configured with coalescing enabled,
        // the request of 2 blocks can be fulfilled using the last free
        // 1-block extent plus a new 1-block extent from the xoz file.
        //
        // This is possible because the free extent is at the end of the
        // free map and it will be coalesced with any new extent.
        //
        // This translate in the xoz file to grow by 1 block and not
        // by 2.

        Segment segm = sg_alloc.alloc(blkarr.blk_sz() * 2, req);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)17);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)16);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)(2));
        EXPECT_EQ(segm.exts()[0].blk_nr(), (uint32_t)(15));

        // Note how the free map didn't change *except*
        // the last extent at the end of the xoz file *before*
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

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 1,
            .max_inline_sz = 8,
            .allow_suballoc = true,
            .single_extent = false
        };

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(false);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(blkarr.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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
        // forcing the allocator to request more space from the xoz file.
        //
        // Because SegmentAllocator is configured with coalescing disabled,
        // the allocator is forced to allocate the requested blocks without
        // the possibility to combine it with the last free blocks (even
        // if the combination results in a single contiguos extent).
        Segment segm = sg_alloc.alloc(blkarr.blk_sz() * 2, req);

        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)18);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)17);

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

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 2,
            .max_inline_sz = 8,
            .allow_suballoc = true,
            .single_extent = false
        };

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(blkarr.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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
        Segment segm1 = sg_alloc.alloc(blkarr.blk_sz() * 2, req);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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
        // force the tail allocator to alloc more blocks and the blkarr
        // will grow (by 1 block)
        Segment segm2 = sg_alloc.alloc(blkarr.blk_sz() * 3, req);

        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)17);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)16);

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

        Segment segm3 = sg_alloc.alloc(blkarr.blk_sz() * 4, req);

        EXPECT_EQ(segm3.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 4));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)20);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)19);

        EXPECT_EQ(segm3.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm3.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm3.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm3.exts()[0].blk_nr(), (uint32_t)(7));
        EXPECT_EQ(segm3.exts()[1].blk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm3.exts()[1].blk_nr(), (uint32_t)(17));

        // Note how the free extent at blk nr 7 was used to fill 1-block.
        // For the remaining 3-blocks an entire 2-block was obtained
        // from the xoz file.
        // The last free extent at blk nr 13 was *not* used because
        // it is not at the end of the xoz file.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false)
                    ));
    }

    TEST(SegmentAllocatorTest, ForceSplitTwice) {

        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 3,
            .max_inline_sz = 8,
            .allow_suballoc = true,
            .single_extent = false
        };

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(blkarr.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        for (size_t i = 0; i < segments.size(); i += 2) {
            sg_alloc.dealloc(segments[i]);
        }

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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
        Segment segm1 = sg_alloc.alloc(blkarr.blk_sz() * 2, req);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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
        Segment segm2 = sg_alloc.alloc(blkarr.blk_sz() * 3, req);

        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

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

        Segment segm3 = sg_alloc.alloc(blkarr.blk_sz() * 4, req);

        EXPECT_EQ(segm3.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 4));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)17);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)16);

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
        // This forced to the blkarr to grow by 1 block, coalesce that
        // block with the last block free to form a 2-block extent
        // and use that to fulfill the request.
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, IsEmpty());
    }

    TEST(SegmentAllocatorTest, InitializeAllocatorSegmentsOfOneExtentOfOneBlock) {

        /*
        const SegmentAllocator::req_t req = {
            .segm_frag_threshold = 3,
            .max_inline_sz = 8,
            .allow_suballoc = true,
            .single_extent = false
        };
        */

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Alloc 15 segments, each of 1 block size
        std::vector<Segment> segments;
        for (size_t i = 0; i < 15; ++i) {
            auto segm = sg_alloc.alloc(blkarr.blk_sz());
            segments.push_back(segm);
        }

        // Now, dealloc every 2 segment, leaving an alternating allocated/free pattern
        // Keep the still-allocated in a separated list
        std::list<Segment> allocated;
        for (size_t i = 0; i < segments.size(); ++i) {
            if (i % 2 == 0) {
                sg_alloc.dealloc(segments[i]);
            } else {
                allocated.push_back(segments[i]);
            }
        }


        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        auto stats = sg_alloc.stats();

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(448));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(7));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(7));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(15));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(8));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(512));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(224));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,7,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

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

        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);
        sg_alloc1.initialize_from_allocated(allocated);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        auto stats1 = sg_alloc1.stats();

        EXPECT_EQ(stats1.current.in_use_by_user_sz, uint64_t(448));
        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(7));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(7));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        // Alloc/Dealloc call count cannot be deduced reliable cross
        // multiple segment allocators. The safest thing is to set them to 0
        EXPECT_EQ(stats1.current.alloc_call_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.external_frag_sz, uint64_t(512));
        EXPECT_EQ(stats1.current.internal_frag_avg_sz, uint64_t(224));
        EXPECT_EQ(stats1.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats1.current.in_use_ext_per_segm, ElementsAre(0,7,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats1.before_reset);
        EXPECT_EQ(stats1.reset_cnt, uint64_t(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(3, 1, false),
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // The new allocator is fully functional
        auto segm1 = sg_alloc1.alloc(blkarr.blk_sz() * 2);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));
        EXPECT_EQ(segm1.exts()[1].blk_cnt(), (uint8_t)(1));
        EXPECT_EQ(segm1.exts()[1].blk_nr(), (uint32_t)(3));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false),
                    Extent(15, 1, false)
                    ));

        // We can release the extents that can be reclaimed by the Tail allocator
        sg_alloc1.release();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)15);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)14);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(5, 1, false),
                    Extent(7, 1, false),
                    Extent(9, 1, false),
                    Extent(11, 1, false),
                    Extent(13, 1, false)
                    ));

    }

    TEST(SegmentAllocatorTest, InitializeAllocatorSegmentsOfMultipleExtentsOfMultipleBlocks) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        // Alloc 15 blocks
        auto main_segm = sg_alloc.alloc(blkarr.blk_sz() * 15);
        auto main_ext = main_segm.exts().back();

        // Hand-craft segments using those 15 blocks
        // Note that there are unused blocks at the begin and at the end
        std::list<Segment> allocated;
        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 9, 2, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 3, false));

        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 6, 1, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 7, 2, false));


        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);


        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);
        sg_alloc1.initialize_from_allocated(allocated);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        auto stats1 = sg_alloc1.stats();

        EXPECT_EQ(stats1.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * (2+3+1+2)));
        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(2+3+1+2));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(2+2));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        // Alloc/Dealloc call count cannot be deduced reliable cross
        // multiple segment allocators. The safest thing is to set them to 0
        EXPECT_EQ(stats1.current.alloc_call_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.external_frag_sz, uint64_t(blkarr.blk_sz() * (15 - (2+3+1+2))));
        EXPECT_EQ(stats1.current.internal_frag_avg_sz, uint64_t((blkarr.blk_sz() >> 1) * (1+1)));
        EXPECT_EQ(stats1.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats1.current.in_use_ext_per_segm, ElementsAre(0,0,2,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats1.before_reset);
        EXPECT_EQ(stats1.reset_cnt, uint64_t(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(5, 2, false),
                    Extent(12, 4, false)
                    ));

        // The new allocator is fully functional
        auto segm1 = sg_alloc1.alloc(blkarr.blk_sz() * 3);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(12));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(5, 2, false),
                    Extent(15, 1, false)
                    ));

        // We can release the extents that can be reclaimed by the Tail allocator
        sg_alloc1.release();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)15);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)14);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(5, 2, false)
                    ));
    }

    TEST(SegmentAllocatorTest, InitializeAllocatorSegmentsWithLargeGaps) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        auto main_segm = sg_alloc.alloc(blkarr.blk_sz() * (0xffff + 2));

        EXPECT_EQ(main_segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(main_segm.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(main_segm.exts()[0].blk_cnt(), (uint16_t)(0xffff));
        EXPECT_EQ(main_segm.exts()[0].blk_nr(), (uint32_t)(1));
        EXPECT_EQ(main_segm.exts()[1].blk_cnt(), (uint16_t)(2));
        EXPECT_EQ(main_segm.exts()[1].blk_nr(), (uint32_t)(0xffff + 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(0xffff + 2 + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)(0xffff + 2));


        // Hand-craft segment: simulate a single block allocated at the end
        std::list<Segment> allocated;
        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(blkarr.past_end_blk_nr()-1, 1, false));


        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);
        sg_alloc1.initialize_from_allocated(allocated);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(0xffff + 2 + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)(0xffff + 2));

        auto stats1 = sg_alloc1.stats();

        EXPECT_EQ(stats1.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats1.current.external_frag_sz, uint64_t(blkarr.blk_sz() * ((0xffff + 2) - 1)));
        EXPECT_EQ(stats1.current.internal_frag_avg_sz, uint64_t((blkarr.blk_sz() >> 1) * (1)));
        EXPECT_EQ(stats1.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats1.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats1.before_reset);
        EXPECT_EQ(stats1.reset_cnt, uint64_t(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 0xffff, false),
                    Extent(0xffff + 1, 1, false)
                    ));

        // The new allocator is fully functional
        auto segm1 = sg_alloc1.alloc(blkarr.blk_sz() * 2);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(0xffff + 2 + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)(0xffff + 2));

        EXPECT_EQ(segm1.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint16_t)(2));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(1));

        // Note how the alloc() does not trigger a coalescing between
        // these 2 consecutive extents
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(3, 0xffff - 2, false),
                    Extent(0xffff + 1, 1, false)
                    ));

        // Note how this dealloc() does not trigger a coalescing either
        // because the coalesced extent cannot be represented in a single extent
        // (the concatenation is too large)
        sg_alloc1.dealloc(allocated.back());
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(3, 0xffff - 2, false),
                    Extent(0xffff + 1, 2, false)
                    ));

        // We can release the extents that can be reclaimed by the Tail allocator
        sg_alloc1.release();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)3);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)2);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, IsEmpty());
    }

    TEST(SegmentAllocatorTest, InitializeAllocatorSegmentsWithLargeGapsAtEnd) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        auto main_segm = sg_alloc.alloc(blkarr.blk_sz() * (0xffff + 2));

        EXPECT_EQ(main_segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(main_segm.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(main_segm.exts()[0].blk_cnt(), (uint16_t)(0xffff));
        EXPECT_EQ(main_segm.exts()[0].blk_nr(), (uint32_t)(1));
        EXPECT_EQ(main_segm.exts()[1].blk_cnt(), (uint16_t)(2));
        EXPECT_EQ(main_segm.exts()[1].blk_nr(), (uint32_t)(0xffff + 1));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(0xffff + 2 + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)(0xffff + 2));


        // Hand-craft segment: simulate a single block allocated at the begin
        std::list<Segment> allocated;
        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(blkarr.begin_blk_nr(), 1, false));


        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);
        sg_alloc1.initialize_from_allocated(allocated);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(0xffff + 2 + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)(0xffff + 2));

        auto stats1 = sg_alloc1.stats();

        EXPECT_EQ(stats1.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * 1));
        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats1.current.external_frag_sz, uint64_t(blkarr.blk_sz() * ((0xffff + 2) - 1)));
        EXPECT_EQ(stats1.current.internal_frag_avg_sz, uint64_t((blkarr.blk_sz() >> 1) * (1)));
        EXPECT_EQ(stats1.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats1.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats1.before_reset);
        EXPECT_EQ(stats1.reset_cnt, uint64_t(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(2, 0xffff, false),
                    Extent(0xffff + 2, 1, false)
                    ));

        // The new allocator is fully functional
        auto segm1 = sg_alloc1.alloc(blkarr.blk_sz()*2);
        auto segm2 = sg_alloc1.alloc(blkarr.blk_sz()*2);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));
        EXPECT_EQ(segm2.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 2));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)(0xffff + 2 + 1));
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)(0xffff + 2));

        EXPECT_EQ(segm1.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm2.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm2.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint16_t)(2));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(2));

        EXPECT_EQ(segm2.exts()[0].blk_cnt(), (uint16_t)(2));
        EXPECT_EQ(segm2.exts()[0].blk_nr(), (uint32_t)(4));

        // Note how the alloc() does not trigger a coalescing between
        // these 2 consecutive extents
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(6, 0xffff - 4, false),
                    Extent(0xffff + 2, 1, false)
                    ));

        // Note how this dealloc() does a coalescing
        sg_alloc1.dealloc(segm2);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(4, 0xffff-2, false),
                    Extent(0xffff + 2, 1, false)
                    ));

        // Note how this alloc() will alloc the last extent and then
        // the dealloc() will do a coalescing
        sg_alloc1.dealloc(sg_alloc1.alloc(blkarr.blk_sz()));
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(4, 0xffff-1, false)
                    ));

        // But this will not
        sg_alloc1.dealloc(segm1);
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(2, 2, false),
                    Extent(4, 0xffff-1, false)
                    ));

        // We can release the extents that can be reclaimed by the Tail allocator
        sg_alloc1.release();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, IsEmpty());
    }

    TEST(SegmentAllocatorTest, InitializeAllocatorSegmentsOfMultipleExtentsOfMultipleBlocksAndSubblocks) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        // Alloc 15 blocks
        auto main_segm = sg_alloc.alloc(blkarr.blk_sz() * 15);
        auto main_ext = main_segm.exts().back();

        // Hand-craft segments using those 15 blocks
        // Note that there are unused blocks at the begin and at the end
        // and some extent are for sub allocation (some share the same block,
        // others don't; some combined fully use the block, others don't)
        //
        // Segment A -> 5 Extents:
        //  - 2 + 1 == 3 full blks
        //  - 2 blks for sub alloc:
        //      - 0x000f + 0x0f00 = 0x0f0f bitmap for 1 of those blocks
        //      - 0x0fff bitmap for the other block
        //
        // Segment B -> 4 Extents:
        //  - 1 + 2 == 3 full blks
        //  - 2 blks for sub alloc:
        //      - 0xf000 bitmap for one of those blocks
        //      - 0xf000 bitmap for the other
        //
        // Total:
        //  - 6 full blks
        //  - 2 blks for suballoc
        //      - 0xffff bitmap for one of those blks (full, no subblk is free)
        //      - 0xff0f bitmap for the other (4 subblks remain free)
        //  - 7 free blks
        //
        // free blks   v       v-v           v-----v
        // blk nr      0 1 2 3 4 5 6 7 8 9 a b c d e  File of 15 blks (0 to e inclusive)
        //               B C D           AAA          Segment 1 (Extents B and C are for suballoc)
        //               E H       F GGG              Segment 2 (Extents E and H are for suballoc)
        //               | |
        //               | \-> bitmap 0xffff (full)
        //               \-> bitmap 0xff0f
        //
        std::list<Segment> allocated;
        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 9, 2, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0x000f, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0x0f00, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 2, 0x0fff, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 3, 1, false));

        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0xf000, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 6, 1, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 7, 2, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 2, 0xf000, true));


        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);


        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);
        sg_alloc1.initialize_from_allocated(allocated);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        auto stats1 = sg_alloc1.stats();

        EXPECT_EQ(stats1.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * (2+1+1+2) + blkarr.subblk_sz() * (4+4+(4*3)+4+4)));
        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(2+1+1+1+1+2));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(2));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(4+4+(4*3)+4+4));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(5+4));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        // Alloc/Dealloc call count cannot be deduced reliable cross
        // multiple segment allocators. The safest thing is to set them to 0
        EXPECT_EQ(stats1.current.alloc_call_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.external_frag_sz, uint64_t(blkarr.blk_sz() * (15 - (2+1+1+1+1+2))));
        EXPECT_EQ(stats1.current.internal_frag_avg_sz, uint64_t((blkarr.subblk_sz() >> 1) * (1+1)));
        EXPECT_EQ(stats1.current.allocable_internal_frag_sz, uint64_t(blkarr.subblk_sz() * 4));

        EXPECT_THAT(stats1.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,1,1,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats1.before_reset);
        EXPECT_EQ(stats1.reset_cnt, uint64_t(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(2, 0x00f0, true),
                    Extent(5, 2, false),
                    Extent(12, 4, false)
                    ));

        // The new allocator is fully functional
        auto segm1 = sg_alloc1.alloc(blkarr.blk_sz() * 3);

        EXPECT_EQ(segm1.calc_data_space_size(), (uint32_t)(blkarr.blk_sz() * 3));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        EXPECT_EQ(segm1.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm1.inline_data_sz(), (uint8_t)(0));

        EXPECT_EQ(segm1.exts()[0].blk_cnt(), (uint8_t)(3));
        EXPECT_EQ(segm1.exts()[0].blk_nr(), (uint32_t)(12));

        auto segm2 = sg_alloc1.alloc(blkarr.subblk_sz());

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(2, 0x0070, true), // took 1 subblock
                    Extent(5, 2, false),
                    Extent(15, 1, false) // took 3 blocks
                    ));

        // We can release the extents that can be reclaimed by the Tail allocator
        sg_alloc1.release();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)15);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)14); // released 1 block from the end of the blkarro
        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(2, 0x0070, true),
                    Extent(5, 2, false)
                    ));
    }

    TEST(SegmentAllocatorTest, InitializeAllocatorWithErrors) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        // Alloc 15 blocks
        auto main_segm = sg_alloc.alloc(blkarr.blk_sz() * 15);
        auto main_ext = main_segm.exts().back();

        // Hand-craft segments using those 15 blocks
        std::list<Segment> allocated;
        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0x000f, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0x0f00, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 2, 0x0fff, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 3, 1, false));

        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0xf000, true));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 6, 1, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 7, 2, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 2, 0xf000, true));

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);


        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);

        // This one is buggy: it is positioned *before* the begin of
        // the blkarr's data space
        allocated.back().add_extent(Extent(main_ext.blk_nr() - 1, 2, false));

        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc1.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 2 blocks that starts at block 0 "
                        "and ends at block 1 partially falls out of bounds. "
                        "The blocks from 1 to 15 (inclusive) are within the bounds and allowed. "
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it is positioned *after* the end of
        // the blkarr's data space
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 15, 2, false));

        SegmentAllocator sg_alloc2(true);
        sg_alloc2.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc2.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 2 blocks that starts at block 16 "
                        "and ends at block 17 completely falls out of bounds. "
                        "The blocks from 1 to 15 (inclusive) are within the bounds and allowed. "
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it is larger than the original blkarr
        allocated.back().add_extent(Extent(main_ext.blk_nr() - 1, 25, false));

        SegmentAllocator sg_alloc3(true);
        sg_alloc3.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc3.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOutOfBounds>(
                AllOf(
                    HasSubstr(
                        "The extent of 25 blocks that starts at block 0 "
                        "and ends at block 24 partially falls out of bounds. "
                        "The blocks from 1 to 15 (inclusive) are within the bounds and allowed. "
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it overlaps with a full block
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 1, false));

        SegmentAllocator sg_alloc4(true);
        sg_alloc4.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc4.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 00002 00003 [   1] overlaps "
                        "with the extent 00002 00003 [   1] (reference extent): "
                        "(at same start)"
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it overlaps with another full block
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 5, 2, false));

        SegmentAllocator sg_alloc5(true);
        sg_alloc5.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc5.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 00007 00008 [   1] "
                        "overlaps with the extent 00006 00008 [   2] (reference extent): "
                        "(ext start is ahead ref)"
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it overlaps with a block for suballocation
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 2, 1, false));

        SegmentAllocator sg_alloc7(true);
        sg_alloc7.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc7.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 00003 00004 [   1] overlaps "
                        "with the extent 00003 00004 [   1] (reference extent): "
                        "(at same start)"
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it overlaps with a block for suballocation
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 2, 0xf000, true));

        SegmentAllocator sg_alloc8(true);
        sg_alloc8.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc8.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 00003 [1111000000000000] (pending to allocate) "
                        "overlaps with the suballoc'd block 00003 [1111111111111111] (allocated): "
                        "error found during SegmentAllocator initialization"
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it overlaps with a another block for suballocation
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 0xf000, true));

        SegmentAllocator sg_alloc9(true);
        sg_alloc9.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc9.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The suballoc'd block 00002 [1111000000000000] (pending to allocate) "
                        "overlaps with the suballoc'd block 00002 [1111111100001111] (allocated): "
                        "error found during SegmentAllocator initialization"
                        )
                    )
                )
        );
        allocated.back().remove_last_extent();

        // This one is also buggy: it overlaps with a full block
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 6, 0xf000, true));

        SegmentAllocator sg_allocA(true);
        sg_allocA.manage_block_array(blkarr);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_allocA.initialize_from_allocated(allocated); }),
            ThrowsMessage<ExtentOverlapError>(
                AllOf(
                    HasSubstr(
                        "The extent 00007 00008 [   1] overlaps "
                        "with the extent 00007 00008 [   1] (reference extent): "
                        "(at same start)"
                        )
                    )
                )
        );
    }

    TEST(SegmentAllocatorTest, AllocSingleExtent) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        // Alloc a single extent of some size. No suballoc is allowed so full blks are allocated
        Extent ext = sg_alloc.alloc_single_extent(23);

        // Just for reusing the testing engine of this test suite,
        // I will create a segment.
        Segment segm(blk_sz_order);
        segm.add_extent(ext);

        // Full block was required to fulfill the requested size
        EXPECT_EQ(segm.calc_data_space_size(), (uint32_t)(blkarr.blk_sz()));
        EXPECT_EQ((blkarr.subblk_sz() * Extent::SUBBLK_CNT_PER_BLK), blkarr.blk_sz());

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)2);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)1);

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

        EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz()));
        EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
        EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(32));
        EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
        EXPECT_EQ(stats.reset_cnt, uint64_t(0));

        sg_alloc.dealloc_single_extent(ext);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc, ElementsAre(
                    Extent(1, 1, false)
                    ));

        auto stats2 = sg_alloc.stats();

        EXPECT_EQ(stats2.current.in_use_by_user_sz, uint64_t(0));
        EXPECT_EQ(stats2.current.in_use_blk_cnt, uint64_t(0));
        EXPECT_EQ(stats2.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats2.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats2.current.in_use_ext_cnt, uint64_t(0));
        EXPECT_EQ(stats2.current.in_use_inlined_sz, uint64_t(0));

        EXPECT_EQ(stats2.current.alloc_call_cnt, uint64_t(1));
        EXPECT_EQ(stats2.current.dealloc_call_cnt, uint64_t(1));

        EXPECT_EQ(stats2.current.external_frag_sz, uint64_t(blkarr.blk_sz()));
        EXPECT_EQ(stats2.current.internal_frag_avg_sz, uint64_t(0));
        EXPECT_EQ(stats2.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats2.current.in_use_ext_per_segm, ElementsAre(0,0,0,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats2.before_reset);
        EXPECT_EQ(stats2.reset_cnt, uint64_t(0));
    }

    TEST(SegmentAllocatorTest, BlockUnblock) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        // Block: any call to alloc/dealloc/release should fail
        sg_alloc.block_all_alloc_dealloc();

        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc.alloc(1); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "SegmentAllocator is blocked: no allocation/deallocation/release is allowed."
                        )
                    )
                )
        );

        Segment segm(blk_sz_order);
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc.dealloc(segm); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "SegmentAllocator is blocked: no allocation/deallocation/release is allowed."
                        )
                    )
                )
        );

        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc.release(); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "SegmentAllocator is blocked: no allocation/deallocation/release is allowed."
                        )
                    )
                )
        );

        // Blocks are accumulative, like in a stack
        sg_alloc.block_all_alloc_dealloc();

        // Unblock once is not enough: we did 2 blocks so it remains 1
        sg_alloc.unblock_all_alloc_dealloc();
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc.alloc(1); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "SegmentAllocator is blocked: no allocation/deallocation/release is allowed."
                        )
                    )
                )
        );

        // Unblock: alloc/dealloc/release are functional again
        sg_alloc.unblock_all_alloc_dealloc();

        segm = sg_alloc.alloc(1);
        sg_alloc.dealloc(segm);
        sg_alloc.release();

        // Unblock when no other blocking is active is a bug (like popping an empty stack)
        EXPECT_THAT(
            ensure_called_once([&]() { sg_alloc.unblock_all_alloc_dealloc(); }),
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr(
                        "SegmentAllocator cannot be unblocked because it is not blocked in the first place."
                        )
                    )
                )
        );

        // Test that creating object <l> blocks the allocator and on its destructions, unblocks the allocator
        {
            auto l = sg_alloc.block_all_alloc_dealloc_guard();

            EXPECT_THAT(
                ensure_called_once([&]() { sg_alloc.alloc(1); }),
                ThrowsMessage<std::runtime_error>(
                    AllOf(
                        HasSubstr(
                            "SegmentAllocator is blocked: no allocation/deallocation/release is allowed."
                            )
                        )
                    )
            );
        }
        // No problem
        segm = sg_alloc.alloc(1);
    }

    TEST(SegmentAllocatorTest, InitializeAllocatorSegmentsOfMultipleExtentsOfMultipleBlocksThenReset) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc(true);
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        const auto blk_sz_order = blkarr.blk_sz_order();

        // Alloc 15 blocks
        auto main_segm = sg_alloc.alloc(blkarr.blk_sz() * 15);
        auto main_ext = main_segm.exts().back();

        // Hand-craft segments using those 15 blocks
        // Note that there are unused blocks at the begin and at the end
        std::list<Segment> allocated;
        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 9, 2, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 1, 3, false));

        allocated.push_back(Segment(blk_sz_order));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 6, 1, false));
        allocated.back().add_extent(Extent(main_ext.blk_nr() + 7, 2, false));


        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);


        SegmentAllocator sg_alloc1(true);
        sg_alloc1.manage_block_array(blkarr);
        sg_alloc1.initialize_from_allocated(allocated);

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)16);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)15);

        auto stats1 = sg_alloc1.stats();

        EXPECT_EQ(stats1.current.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * (2+3+1+2)));
        EXPECT_EQ(stats1.current.in_use_blk_cnt, uint64_t(2+3+1+2));
        EXPECT_EQ(stats1.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.in_use_ext_cnt, uint64_t(2+2));
        EXPECT_EQ(stats1.current.in_use_inlined_sz, uint64_t(0));

        // Alloc/Dealloc call count cannot be deduced reliable cross
        // multiple segment allocators. The safest thing is to set them to 0
        EXPECT_EQ(stats1.current.alloc_call_cnt, uint64_t(0));
        EXPECT_EQ(stats1.current.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats1.current.external_frag_sz, uint64_t(blkarr.blk_sz() * (15 - (2+3+1+2))));
        EXPECT_EQ(stats1.current.internal_frag_avg_sz, uint64_t((blkarr.blk_sz() >> 1) * (1+1)));
        EXPECT_EQ(stats1.current.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats1.current.in_use_ext_per_segm, ElementsAre(0,0,2,0,0,0,0,0));

        XOZ_EXPECT_ALL_ZERO_STATS(stats1.before_reset);
        EXPECT_EQ(stats1.reset_cnt, uint64_t(0));

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, ElementsAre(
                    Extent(1, 1, false),
                    Extent(5, 2, false),
                    Extent(12, 4, false)
                    ));

        // Reset, dealloc everything and reset the stats. This should also release()
        // any pending-to-free block in the allocator and in the underlying blk array
        sg_alloc1.reset();

        EXPECT_EQ(blkarr.begin_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.past_end_blk_nr(), (uint32_t)1);
        EXPECT_EQ(blkarr.blk_cnt(), (uint32_t)0);

        XOZ_EXPECT_FREE_MAPS_CONTENT_BY_BLK_NR(sg_alloc1, IsEmpty());

        // Current stats were zero'd
        auto stats2 = sg_alloc1.stats();

        XOZ_EXPECT_ALL_ZERO_STATS(stats2.current);
        EXPECT_EQ(stats2.reset_cnt, uint64_t(1));

        // But the stats "before reset" were preserved
        EXPECT_EQ(stats2.before_reset.in_use_by_user_sz, uint64_t(blkarr.blk_sz() * (2+3+1+2)));
        EXPECT_EQ(stats2.before_reset.in_use_blk_cnt, uint64_t(2+3+1+2));
        EXPECT_EQ(stats2.before_reset.in_use_blk_for_suballoc_cnt, uint64_t(0));
        EXPECT_EQ(stats2.before_reset.in_use_subblk_cnt, uint64_t(0));

        EXPECT_EQ(stats2.before_reset.in_use_ext_cnt, uint64_t(2+2));
        EXPECT_EQ(stats2.before_reset.in_use_inlined_sz, uint64_t(0));

        // Alloc/Dealloc call count cannot be deduced reliable cross
        // multiple segment allocators. The safest thing is to set them to 0
        EXPECT_EQ(stats2.before_reset.alloc_call_cnt, uint64_t(0));
        EXPECT_EQ(stats2.before_reset.dealloc_call_cnt, uint64_t(0));

        EXPECT_EQ(stats2.before_reset.external_frag_sz, uint64_t(blkarr.blk_sz() * (15 - (2+3+1+2))));
        EXPECT_EQ(stats2.before_reset.internal_frag_avg_sz, uint64_t((blkarr.blk_sz() >> 1) * (1+1)));
        EXPECT_EQ(stats2.before_reset.allocable_internal_frag_sz, uint64_t(0));

        EXPECT_THAT(stats2.before_reset.in_use_ext_per_segm, ElementsAre(0,0,2,0,0,0,0,0));
    }

    namespace {
        void writeall(BlockArray& blkarr, Segment& segm, const std::string& s) {
            IOSegment io(blkarr, segm);
            io.writeall(s.c_str(), assert_u32(s.size()));
        }

        std::string readall(BlockArray& blkarr, Segment& segm, const uint32_t len) {
            std::vector<char> buf;

            IOSegment io(blkarr, segm);
            io.readall(buf, len);

            return std::string(buf.data(), buf.size());
        }
    }

    TEST(SegmentAllocatorTest, IncreaseSizeByRealloc) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Initially, zero length segment
        Segment segm = sg_alloc.alloc(0);
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(0));

        // Realloc to 1 byte (expected inline data)
        sg_alloc.realloc(segm, 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(1));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(1));

        writeall(blkarr, segm, "A");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((0) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to 3 bytes (still inline data)
        sg_alloc.realloc(segm, 3);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(3));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(3));

        EXPECT_EQ(readall(blkarr, segm, 1), "A");
        writeall(blkarr, segm, "ABC");

        // Realloc to subblk_sz (expected 1 extent)
        sg_alloc.realloc(segm, blkarr.subblk_sz());

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(4));
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)1);

        EXPECT_EQ(readall(blkarr, segm, 3), "ABC");
        writeall(blkarr, segm, "ABCD");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz()));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(4));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(0));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((15) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to subblk_sz + 1 byte (expected 1 extent and 1 byte of inline data)
        sg_alloc.realloc(segm, blkarr.subblk_sz() + 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(1));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(5));
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)1);

        EXPECT_EQ(readall(blkarr, segm, 4), "ABCD");
        writeall(blkarr, segm, "ABCDE");

        // Realloc to 3 subblk_sz + 1 byte (expected 1 extents and 1 byte of inline data)
        sg_alloc.realloc(segm, blkarr.subblk_sz() * 3 + 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(1));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(13));
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)3);

        EXPECT_EQ(readall(blkarr, segm, 5), "ABCDE");
        writeall(blkarr, segm, "ABCDEFGHIJKLM");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(1 + 3 * blkarr.subblk_sz()));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(3));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(6));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((13) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to 17 subblk_sz (expected 2 extents)
        sg_alloc.realloc(segm, blkarr.subblk_sz() * 17);

        EXPECT_EQ(segm.ext_cnt(), (size_t)2);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(68));
        EXPECT_EQ(segm.exts()[0].blk_cnt(), (uint8_t)1);
        EXPECT_EQ(segm.exts()[1].subblk_cnt(), (uint8_t)1);

        EXPECT_EQ(readall(blkarr, segm, 13), "ABCDEFGHIJKLM");
        writeall(blkarr, segm, "AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMMNNNNOOOOPPPPQQQQ");

        EXPECT_EQ(readall(blkarr, segm, 68), "AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMMNNNNOOOOPPPPQQQQ");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t((blkarr.subblk_sz() * 17)));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(2));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(2));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(7));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(3));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(0));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((15) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,0,1,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }
    }

    TEST(SegmentAllocatorTest, DecreaseSizeByRealloc) {

        auto blkarr_ptr = FileBlockArray::create_mem_based(64, 1);
        FileBlockArray& blkarr = *blkarr_ptr.get();
        SegmentAllocator sg_alloc;
        sg_alloc.manage_block_array(blkarr);
        sg_alloc.initialize_from_allocated(std::list<Segment>());

        // Initially, a segment with 1 extent of 1 block and another extent with 1 subblock
        Segment segm = sg_alloc.alloc(68);
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(68));

        writeall(blkarr, segm, "AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIIIJJJJKKKKLLLLMMMMNNNNOOOOPPPPQQQQ");

        // Realloc to 3 subblk_sz + 1 byte (expected 1 extents and 1 byte of inline data)
        sg_alloc.realloc(segm, blkarr.subblk_sz() * 3 + 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(1));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(13));
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)3);

        EXPECT_EQ(readall(blkarr, segm, 13), "AAAABBBBCCCCD");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(1 + 3 * blkarr.subblk_sz()));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(3));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(2));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(1));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(64));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((13) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to subblk_sz + 1 byte (expected 1 extent and 1 byte of inline data)
        sg_alloc.realloc(segm, blkarr.subblk_sz() + 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(1));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(5));
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)1);

        EXPECT_EQ(readall(blkarr, segm, 5), "AAAAB");

        // Realloc to subblk_sz (expected 1 extent)
        sg_alloc.realloc(segm, blkarr.subblk_sz());

        EXPECT_EQ(segm.ext_cnt(), (size_t)1);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(4));
        EXPECT_EQ(segm.exts()[0].subblk_cnt(), (uint8_t)1);

        EXPECT_EQ(readall(blkarr, segm, 4), "AAAA");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(blkarr.subblk_sz()));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(1));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(3));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(2));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(64));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(2));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((15) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(0,1,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to 3 bytes (only inline data)
        sg_alloc.realloc(segm, 3);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(3));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(3));

        EXPECT_EQ(readall(blkarr, segm, 3), "AAA");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(3));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(3));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(4));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(3));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(128));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((0) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to 1 byte (expected inline data)
        sg_alloc.realloc(segm, 1);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(1));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(1));

        EXPECT_EQ(readall(blkarr, segm, 1), "A");

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(1));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(1));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(4));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(3));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(128));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((0) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }

        // Realloc to 0 byte
        sg_alloc.realloc(segm, 0);

        EXPECT_EQ(segm.ext_cnt(), (size_t)0);
        EXPECT_EQ(segm.inline_data_sz(), (uint8_t)(0));
        EXPECT_EQ(segm.calc_data_space_size(), (uint8_t)(0));

        {
            auto stats = sg_alloc.stats();

            EXPECT_EQ(stats.current.in_use_by_user_sz, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_blk_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_blk_for_suballoc_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_subblk_cnt, uint64_t(0));

            EXPECT_EQ(stats.current.in_use_ext_cnt, uint64_t(0));
            EXPECT_EQ(stats.current.in_use_inlined_sz, uint64_t(0));

            EXPECT_EQ(stats.current.alloc_call_cnt, uint64_t(4));
            EXPECT_EQ(stats.current.dealloc_call_cnt, uint64_t(3));

            EXPECT_EQ(stats.current.external_frag_sz, uint64_t(128));
            EXPECT_EQ(stats.current.internal_frag_avg_sz, uint64_t(0));
            EXPECT_EQ(stats.current.allocable_internal_frag_sz, uint64_t((0) * blkarr.subblk_sz()));

            EXPECT_THAT(stats.current.in_use_ext_per_segm, ElementsAre(1,0,0,0,0,0,0,0));

            XOZ_EXPECT_ALL_ZERO_STATS(stats.before_reset);
            EXPECT_EQ(stats.reset_cnt, uint64_t(0));
        }
    }
}

