#include "xoz/balloc.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

namespace {
    TEST(BlockAllocatorTest, EmptyThenAllocSoGrow) {
        const GlobalParameters gp = {
            .blk_sz = 4096,
            .blk_sz_order = 12,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        BlockAllocator balloc = BlockAllocator(repo);

        // This will force to request 1 new block from the repo and expand it
        std::list<Extent> exts = balloc.alloc({
                .blk_cnt = 1,
                .group = 0,
                .max_split = 0,
                .max_neighbor_depth = 0,
                .fixed_size_obj = false,
                .obj_size = 1024 // (4kb - 1k) = 3072 bytes "lost" in frag.
                });

        // We expect the allocation of a single extent of blocks
        EXPECT_EQ(exts.size(), (unsigned) 1);

        Extent ext1 = exts.front();

        // Check this singleton extent
        EXPECT_EQ(ext1.blk_nr(), (unsigned) 1);
        EXPECT_EQ(ext1.blk_cnt(), (unsigned) 1);

        // This will force to request 5 new blocks from the repo and expand it
        std::list<Extent> exts2 = balloc.alloc({
                .blk_cnt = 5,
                .group = 0,
                .max_split = 0,
                .max_neighbor_depth = 0,
                .fixed_size_obj = false,
                .obj_size = (4096 * 5) // zero fragmentation
                });

        // We expect the allocation of a single extent of blocks
        EXPECT_EQ(exts2.size(), (unsigned) 1);

        Extent ext2 = exts2.front();

        // Check this singleton extent
        EXPECT_EQ(ext2.blk_nr(), (unsigned) 2);
        EXPECT_EQ(ext2.blk_cnt(), (unsigned) 5);

        // Check the repo size
        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        // We expect 1 block (the header of the repository), plus
        // 1 block from the first allocation, plus 5 more blocks from
        // the second allocation.
        EXPECT_THAT(stats_str, HasSubstr("Repository size: 28672 bytes, 7 blocks"));

        // Check the free blocks in the allocator.
        // We should get none
        ss.str("");
        balloc.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Free: 0 bytes, 0 blocks."));
        EXPECT_THAT(stats_str, HasSubstr("Internal fragmentation: 3072 bytes."));

    }


    TEST(BlockAllocatorTest, FreeButNotShrinkThenFreeAgainAndShrink) {
        const GlobalParameters gp = {
            .blk_sz = 4096,
            .blk_sz_order = 12,
            .blk_init_cnt = 1
        };

        Repository repo = Repository::create_mem_based(0, gp);

        BlockAllocator balloc = BlockAllocator(repo);

        // Alloc and expand the repository by 1 + 2 + 2 blocks
        std::list<Extent> allocd;

        allocd = balloc.alloc({
                .blk_cnt = 1,
                .group = 0,
                .max_split = 0,
                .max_neighbor_depth = 0,
                .fixed_size_obj = false,
                .obj_size = (4096 * 1) // no frag
                });

        EXPECT_EQ(allocd.size(), (unsigned) 1);
        Extent extA = allocd.back();

        allocd = balloc.alloc({
                .blk_cnt = 2,
                .group = 0,
                .max_split = 0,
                .max_neighbor_depth = 0,
                .fixed_size_obj = false,
                .obj_size = (4096 * 2) // no frag
                });

        EXPECT_EQ(allocd.size(), (unsigned) 1);
        Extent extB = allocd.back();

        allocd = balloc.alloc({
                .blk_cnt = 2,
                .group = 0,
                .max_split = 0,
                .max_neighbor_depth = 0,
                .fixed_size_obj = false,
                .obj_size = (4096 * 2) // no frag
                });

        EXPECT_EQ(allocd.size(), (unsigned) 1);
        Extent extC = allocd.back();

        // Check that we have something like this:
        //
        //  (reserved) ====================== allocated =========================
        //  |-------|  |--------|  |--------|  |--------|  |--------|  |--------|
        //             == extA ==  ======== extB ========  ======== extC ========
        //
        EXPECT_EQ(extA.blk_nr(), (unsigned)1);
        EXPECT_EQ(extA.blk_cnt(), (unsigned)1);
        EXPECT_EQ(extB.blk_nr(), (unsigned)2);
        EXPECT_EQ(extB.blk_cnt(), (unsigned)2);
        EXPECT_EQ(extC.blk_nr(), (unsigned)4);
        EXPECT_EQ(extC.blk_cnt(), (unsigned)2);

        // Check the repo size
        std::stringstream ss;
        repo.print_stats(ss);

        auto stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        // We expect 1 block (the header of the repository), plus
        // 5 more blocks from the allocation.
        EXPECT_THAT(stats_str, HasSubstr("Repository size: 24576 bytes, 6 blocks"));

        // Check the free blocks in the allocator.
        // We should get none
        ss.str("");
        balloc.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Free: 0 bytes, 0 blocks."));
        EXPECT_THAT(stats_str, HasSubstr("Internal fragmentation: 0 bytes."));

        // Now lets free some blocks except the ones that are at the end
        // of the repository so the repository cannot shrink.
        balloc.free(extB);
        balloc.try_release();

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        // Event if extB was free, the allocated and still-in-use extC prevents
        // the shrink of the repository.
        EXPECT_THAT(stats_str, HasSubstr("Repository size: 24576 bytes, 6 blocks"));

        // Check the free blocks in the allocator.
        // We should get the ones from extB
        ss.str("");
        balloc.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Free: 8192 bytes, 2 blocks."));
        EXPECT_THAT(stats_str, HasSubstr("Internal fragmentation: 0 bytes."));

        EXPECT_THAT(stats_str, HasSubstr("Bin 1: 1 extents, 8192 bytes"));

        // Let's free extC allowing the repository to shrink 4 blocks
        // (2 blocks from extC and 2 more blocks from extB)
        //
        balloc.free(extC);
        balloc.try_release();

        ss.str("");
        repo.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        // Freeing extC allowed the repository to shrink by extB+extC.
        EXPECT_THAT(stats_str, HasSubstr("Repository size: 8192 bytes, 2 blocks"));

        // Check the free blocks in the allocator.
        // We should get none because extB and extC where released and returned
        // back to the repository and they are not handled anymore by the allocator.
        ss.str("");
        balloc.print_stats(ss);

        stats_str = ss.str();
        // std::cout << stats_str; // for easy debug

        EXPECT_THAT(stats_str, HasSubstr("Free: 0 bytes, 0 blocks."));
        EXPECT_THAT(stats_str, HasSubstr("Internal fragmentation: 0 bytes."));

        EXPECT_THAT(stats_str, HasSubstr("Bin 1: 0 extents, 0 bytes"));
    }
}
