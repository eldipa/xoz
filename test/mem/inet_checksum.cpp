#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include "xoz/err/exceptions.h"
#include "xoz/mem/inet_checksum.h"
#include "xoz/io/iospan.h"

#include <numeric>
#include <vector>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing_xoz::helpers::ensure_called_once;

namespace {
    TEST(InetChecksumTest, GoodChecksum) {
        EXPECT_EQ(is_inet_checksum_good(0), (bool)true);
        EXPECT_EQ(is_inet_checksum_good(0xffff), (bool)true);

        EXPECT_EQ(is_inet_checksum_good(1), (bool)false);
        EXPECT_EQ(is_inet_checksum_good(0x7fff), (bool)false);
        EXPECT_EQ(is_inet_checksum_good(0xfffe), (bool)false);
    }

    TEST(InetChecksumTest, InvalidChecksumValue) {
        EXPECT_THAT(
            [&]() { is_inet_checksum_good(0xffff + 1); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("Checksum value is invalid, its 2 most significant bytes are non-zero.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { is_inet_checksum_good(0x80000000); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("Checksum value is invalid, its 2 most significant bytes are non-zero.")
                    )
                )
        );
    }

    TEST(InetChecksumTest, Uint32Checksum) {
        EXPECT_EQ(inet_checksum(0x00000000), (uint32_t)0x00000000);
        EXPECT_EQ(inet_checksum(0x00000001), (uint32_t)0x00000001);

        EXPECT_EQ(inet_checksum(0x00010000), (uint32_t)0x00000001);
        EXPECT_EQ(inet_checksum(0x00010001), (uint32_t)0x00000002);

        EXPECT_EQ(inet_checksum(0x10010001), (uint32_t)0x00001002);
        EXPECT_EQ(inet_checksum(0x10011001), (uint32_t)0x00002002);

        EXPECT_EQ(inet_checksum(0x01000000), (uint32_t)0x00000100);
        EXPECT_EQ(inet_checksum(0x00000100), (uint32_t)0x00000100);

        // For performance reasons, inet_checksum() for uint32_t does
        // not do the fold at the end so the returned checksum may be larger
        // uint16_t
        EXPECT_EQ(inet_checksum(0x80008000), (uint32_t)0x00010000);
        EXPECT_EQ(fold_inet_checksum(inet_checksum(0x80008000)), (uint32_t)0x00000001);
    }

    TEST(InetChecksumTest, Fold) {
        EXPECT_EQ(fold_inet_checksum(0x00000000), (uint32_t)0x00000000);
        EXPECT_EQ(fold_inet_checksum(0x00000001), (uint32_t)0x00000001);
        EXPECT_EQ(fold_inet_checksum(0x00010001), (uint32_t)0x00000002);

        EXPECT_EQ(fold_inet_checksum(0x80008000), (uint32_t)0x00000001);
        EXPECT_EQ(fold_inet_checksum(0x80018000), (uint32_t)0x00000002);
        EXPECT_EQ(fold_inet_checksum(0x80018001), (uint32_t)0x00000003);

        EXPECT_EQ(fold_inet_checksum(0xffffffff), (uint32_t)0x0000ffff);
    }

    TEST(InetChecksumTest, Uint16BufChecksum) {
        const uint16_t buf[] = {0, 1, 1, 1, 0xff, 0, 0xffff, 0xffff};

        EXPECT_EQ(inet_checksum(buf+0, 1), (uint32_t)0x00000000);
        EXPECT_EQ(inet_checksum(buf+0, 4), (uint32_t)0x00000003);
        EXPECT_EQ(inet_checksum(buf+0, 6), (uint32_t)0x00000102);

        // The inet_checksum over a buffer it will always does the fold
        EXPECT_EQ(inet_checksum(buf+7, 2), (uint32_t)0x0000ffff);
    }
}
