#include "xoz/io/iospan.h"
#include "xoz/err/exceptions.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <numeric>
#include <vector>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::subvec;

#define XOZ_EXPECT_BUFFER_SERIALIZATION(buf, at, len, data) do {           \
    EXPECT_EQ(hexdump((buf), (at), (len)), (data));                        \
} while (0)

namespace {
    TEST(IOSpanTest, SmallChunk) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf, 4);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        iospan2.readall(rdbuf, 4);

        EXPECT_EQ(rdbuf.size(), (size_t)4);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64 - 4));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, Full) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        IOSpan iospan2(buf);
        iospan2.readall(rdbuf, (uint32_t)64);

        EXPECT_EQ(rdbuf.size(), (size_t)64);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);

        // Call read_extent again but let read_extent to figure out how many bytes needs to read
        // (the size of the extent in bytes)
        rdbuf.clear();
        rdbuf.resize(0);
        iospan2.seek_rd(0);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64));

        iospan2.readall(rdbuf);
        EXPECT_EQ(rdbuf.size(), (size_t)64);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, NoShrink) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf = {'E', 'F', 'G', 'H', 'I', 'J'};

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf, 4);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        iospan2.readall(rdbuf, 4);

        EXPECT_EQ(rdbuf.size(), (size_t)6);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64 - 4));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, subvec(rdbuf, 0, 4));
        EXPECT_EQ(rdbuf[4], 'I');
        EXPECT_EQ(rdbuf[5], 'J');
    }


    TEST(IOSpanTest, RWBeyondBoundary) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf(65); // block size plus 1
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0);

        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        uint32_t n = iospan1.writesome(wrbuf); // try to write 65 bytes, but write only 64

        EXPECT_EQ(n, (uint32_t)64);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        n = iospan1.writesome(wrbuf); // yes, try to write 65 bytes "more"
        EXPECT_EQ(n, (uint32_t)0);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));

        iospan1.seek_wr(99); // try to go past the end but no effect
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));

        IOSpan iospan2(buf);
        n = iospan2.readsome(rdbuf, 65); // try to read 65 but read only 64

        EXPECT_EQ(n, (uint32_t)64);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        n = iospan2.readsome(rdbuf, 65); // try to read 65 more
        EXPECT_EQ(n, (uint32_t)0);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));

        iospan2.seek_rd(99); // try to go past the end but no effect
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));

        EXPECT_EQ(subvec(wrbuf, 0, 64), subvec(rdbuf, 0, 64));
    }

    TEST(IOSpanTest, Seek) {
        std::vector<char> buf(64);

        IOSpan iospan1(buf);

        // Initial positions
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(64));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0));

        // Read/write pointers are independent
        iospan1.seek_wr(5);
        iospan1.seek_rd(9);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64-5));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(5));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(64-9));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(9));

        // Positions are absolute by default (relative to the begin of the segment)
        iospan1.seek_wr(50);
        iospan1.seek_rd(39);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64-50));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(50));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(64-39));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(39));

        // Past the end is clamp to the segment size
        iospan1.seek_wr(9999);
        iospan1.seek_rd(9999);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64));

        // Seek relative the current position in backward direction
        iospan1.seek_wr(2, IOSpan::Seekdir::bwd);
        iospan1.seek_rd(1, IOSpan::Seekdir::bwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(2));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64 - 2));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64 - 1));

        // Seek relative the current position in backward direction (validate that it's relative)
        iospan1.seek_wr(6, IOSpan::Seekdir::bwd);
        iospan1.seek_rd(6, IOSpan::Seekdir::bwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(8));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64 - 8));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64 - 7));

        // Seek past the begin is set to 0; seek relative 0 does not change the pointer
        iospan1.seek_wr(999, IOSpan::Seekdir::bwd);
        iospan1.seek_rd(0, IOSpan::Seekdir::bwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64 - 7));

        // Seek relative the current position in forward direction
        iospan1.seek_wr(4, IOSpan::Seekdir::fwd);
        iospan1.seek_rd(4, IOSpan::Seekdir::fwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7 - 4));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64 - 7 + 4));

        // Seek relative the current position in forward direction, again
        iospan1.seek_wr(2, IOSpan::Seekdir::fwd);
        iospan1.seek_rd(2, IOSpan::Seekdir::fwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4 - 2));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4+2));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7 - 4 - 2));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64 - 7 + 4 + 2));

        // Seek relative the current position in forward direction, past the end
        iospan1.seek_wr(59, IOSpan::Seekdir::fwd);
        iospan1.seek_rd(3, IOSpan::Seekdir::fwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64));

        // Seek relative the end position
        iospan1.seek_wr(0, IOSpan::Seekdir::end);
        iospan1.seek_rd(0, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64));

        // Again
        iospan1.seek_wr(3, IOSpan::Seekdir::end);
        iospan1.seek_rd(3, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(3));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64-3));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(3));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64-3));

        // Again
        iospan1.seek_wr(6, IOSpan::Seekdir::end);
        iospan1.seek_rd(1, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(6));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64-6));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64-1));

        // Past the begin goes to zero
        iospan1.seek_wr(64, IOSpan::Seekdir::end);
        iospan1.seek_rd(65, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(0));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(64));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0));
    }

    TEST(IOSpanTest, RWExactFail) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf(65); // block size plus 1
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0);

        std::vector<char> rdbuf(128, 0); // initialize to 0 so we can check later that nobody written on it

        IOSpan iospan1(buf);
        EXPECT_THAT(
            [&]() { iospan1.writeall(wrbuf); },  // try to write 65 bytes, but 64 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 65 bytes but only 64 bytes are available. "
                              "Write exact-byte-count operation at position 0 failed; "
                              "detected before the write."
                        )
                    )
                )
        );

        // Nothing is written
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Write a few bytes
        iospan1.writeall(subvec(wrbuf, 0, 8));

        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        EXPECT_THAT(
            [&]() { iospan2.readall(rdbuf, 65); },  // try to read 65 bytes, but 64 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 65 bytes but only 64 bytes are available. "
                              "Read exact-byte-count operation at position 0 failed; "
                              "detected before the read."
                        )
                    )
                )
        );

        // Nothing was read
        std::vector<char> zeros = {0, 0, 0, 0, 0, 0, 0, 0};
        EXPECT_EQ(subvec(rdbuf, 0, 8), zeros);
    }
}
