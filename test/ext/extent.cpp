#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "xoz/ext/extent.h"

namespace {
    TEST(ExtentTest, BlockNumberBits) {
        // Block numbers are 26 bits long
        // This test check that the 25th bit is preserved (being 0th the lowest)
        // and the 26th is dropped (because it would require 27 bits)
        Extent ext1((1 << 25) | (1 << 26), 1, false);
        EXPECT_EQ(ext1.blk_nr(), (uint32_t)(1 << 25));

        // Suballoc'd does not change the above
        Extent ext2((1 << 25) | (1 << 26), 1, true);
        EXPECT_EQ(ext2.blk_nr(), (uint32_t)(1 << 25));

        // Check higher bits are preserved when hi_blk_nr() is used
        Extent ext3((1 << 25) | (1 << 26), 1, false);
        EXPECT_EQ(ext3.hi_blk_nr(), (uint16_t)((1 << 25) >> 16));

        // Check lower bits
        Extent ext4((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext4.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));

        // Suballoc'd does not change the above
        Extent ext5((1 << 15) | (1 << 3), 1, true);
        EXPECT_EQ(ext5.blk_nr(), (uint32_t)((1 << 15) | (1 << 3)));

        // Check higher and lower bits
        Extent ext6((1 << 15) | (1 << 3), 1, false);
        EXPECT_EQ(ext6.hi_blk_nr(), (uint16_t)(0));
        EXPECT_EQ(ext6.lo_blk_nr(), (uint16_t)(((1 << 15) | (1 << 3))));
    }

    TEST(ExtentTest, BlockSuballoced) {
        Extent ext1(1, 0x8142, true);
        EXPECT_EQ(ext1.blk_bitmap(), (uint16_t)(0x8142));
        EXPECT_EQ(ext1.is_suballoc(), true);
    }
}
