#include "xoz/io/iospan.h"
#include "xoz/err/exceptions.h"
#include "xoz/mem/casts.h"

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
using ::testing_xoz::helpers::are_all_zeros;

using namespace ::xoz;

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

    TEST(IOSpanTest, SmallChunkUInt8) {
        std::vector<char> buf(64);

        std::vector<uint8_t> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<uint8_t> rdbuf(4); // preallocate space, it's needed when using the ptr interface

        IOSpan iospan1(buf);
        iospan1.writeall(as_char_ptr(wrbuf.data()), 4);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        iospan2.readall(as_char_ptr(rdbuf.data()), 4);

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

        // No shrink of the buffer/vector should happen
        EXPECT_EQ(rdbuf.size(), (size_t)6);

        // Check that indeed we read 4 bytes into a 6 bytes buffer
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

        std::stringstream iss;
        iss.write(wrbuf.data(), wrbuf.size());

        EXPECT_THAT(
            [&]() { iospan1.writeall(iss); },  // try to write 65 bytes, but 64 is max and fail
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

        std::stringstream oss;
        EXPECT_THAT(
            [&]() { iospan2.readall(oss, 65); },  // try to read 65 bytes, but 64 is max and fail
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
        // NOTE: this is true only because we tried to read very few bytes but if we try
        // to read much more, because how io.readall works, it may read partially and write
        // something into the output.
        EXPECT_EQ(are_all_zeros(oss, 0, 8), (bool)true);
    }

    TEST(IOSpanTest, WriteExactFailBadArgSize) {
        std::vector<char> buf(64); // buffer large enough for any write

        std::vector<char> wrbuf(32);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0);

        IOSpan iospan1(buf);
        EXPECT_THAT(
            [&]() { iospan1.writeall(wrbuf, 33); },
            ThrowsMessage<std::overflow_error>(
                AllOf(
                    HasSubstr(
                        "Requested to write 33 bytes but input vector has only 32 bytes."
                        )
                    )
                )
        );

        std::istringstream iss("1234");
        EXPECT_THAT(
            [&]() { iospan1.writeall(iss, 5); },
            ThrowsMessage<std::overflow_error>(
                AllOf(
                    HasSubstr(
                        "Requested to write 5 bytes but input file has only 4 bytes."
                        )
                    )
                )
        );

        // Nothing is written
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

    }

    TEST(IOSpanTest, SmallChunkStream) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        std::stringstream iss, oss;
        iss.write(wrbuf.data(), wrbuf.size());

        IOSpan iospan1(buf);
        iospan1.writeall(iss, 4);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        iospan2.readall(oss, 4);
        auto s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

        EXPECT_EQ(rdbuf.size(), (size_t)4);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64 - 4));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, FullStream) {
        std::vector<char> buf(64);

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::stringstream iss, oss;
        iss.write(wrbuf.data(), wrbuf.size());

        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        iospan1.writeall(iss);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        IOSpan iospan2(buf);
        iospan2.readall(oss, (uint32_t)64);
        auto s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

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
        oss = std::stringstream();
        iospan2.seek_rd(0);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64));

        iospan2.readall(oss);
        s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

        EXPECT_EQ(rdbuf.size(), (size_t)64);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, SmallChunkStreamBuffered) {
        std::vector<char> buf(64);
        const uint32_t bufsz = 2;

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        std::stringstream iss, oss;
        iss.write(wrbuf.data(), wrbuf.size());

        IOSpan iospan1(buf);
        iospan1.writeall(iss, 4, bufsz);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        iospan2.readall(oss, 4, bufsz);
        auto s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

        EXPECT_EQ(rdbuf.size(), (size_t)4);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64 - 4));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, FullStreamBuffered) {
        std::vector<char> buf(64);
        const uint32_t bufsz = 2;

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::stringstream iss, oss;
        iss.write(wrbuf.data(), wrbuf.size());

        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        iospan1.writeall(iss, uint32_t(-1), bufsz);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        IOSpan iospan2(buf);
        iospan2.readall(oss, (uint32_t)64, bufsz);
        auto s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

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
        oss = std::stringstream();
        iospan2.seek_rd(0);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64));

        iospan2.readall(oss, uint32_t(-1), bufsz);
        s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

        EXPECT_EQ(rdbuf.size(), (size_t)64);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, SmallChunkStreamUnbuffered) {
        std::vector<char> buf(64);
        const uint32_t bufsz = 1;

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D'};
        std::vector<char> rdbuf;

        std::stringstream iss, oss;
        iss.write(wrbuf.data(), wrbuf.size());

        IOSpan iospan1(buf);
        iospan1.writeall(iss, 4, bufsz);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(64 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        IOSpan iospan2(buf);
        iospan2.readall(oss, 4, bufsz);
        auto s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

        EXPECT_EQ(rdbuf.size(), (size_t)4);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64 - 4));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(4));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "4142 4344 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, FullStreamUnbuffered) {
        std::vector<char> buf(64);
        const uint32_t bufsz = 1;

        std::vector<char> wrbuf(64);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..64

        std::stringstream iss, oss;
        iss.write(wrbuf.data(), wrbuf.size());

        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        iospan1.writeall(iss, uint32_t(-1), bufsz);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        IOSpan iospan2(buf);
        iospan2.readall(oss, (uint32_t)64, bufsz);
        auto s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

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
        oss = std::stringstream();
        iospan2.seek_rd(0);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(64));

        iospan2.readall(oss, uint32_t(-1), bufsz);
        s = oss.str();
        rdbuf.assign(s.cbegin(), s.cend());

        EXPECT_EQ(rdbuf.size(), (size_t)64);
        EXPECT_EQ(iospan2.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan2.tell_rd(), uint32_t(64));
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f"
                );

        EXPECT_EQ(wrbuf, rdbuf);
    }

    TEST(IOSpanTest, LimitsOnReadWrite) {
        std::vector<char> buf(8);

        std::vector<char> wrbuf = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
        std::vector<char> rdbuf;

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(8));

        // Limit for RW from position 2 to 2+4.
        // Initially the rw pointer is beyond the allowed range so it is moved
        // to one past the end of the new range: the position 6
        iospan1.limit_wr(2, 4);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(6));

        // No change on RD pointer/remaining
        EXPECT_EQ(iospan1.remain_rd(), uint32_t(8));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0));

        // Limit for RD from position 1 to 1+1.
        // Initially the rd pointer is behind the allowed range so it is moved
        // to the begin of the new range: the position 1
        iospan1.limit_rd(1, 1);
        EXPECT_EQ(iospan1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(1));

        // We can read the full range
        iospan1.readall(rdbuf);
        EXPECT_EQ(rdbuf.size(), (size_t)1);
        EXPECT_EQ(rdbuf[0], (char)'B');

        // For writing, we cannot write anything else: the rw is at the end already
        EXPECT_THAT(
            [&]() { iospan1.writeall(wrbuf); },  // try to write 8 bytes, but 0 is max and fail
            ThrowsMessage<NotEnoughRoom>(
                AllOf(
                    HasSubstr("Requested 8 bytes but only 0 bytes are available. "
                              "Write exact-byte-count operation at position 6 failed; "
                              "detected before the write."
                        )
                    )
                )
        );

        {
            // Save the rd/wr pointers and limits to be restored at the end of the scope
            auto rewind_guard = iospan1.auto_rewind();
            auto restore_guard = iospan1.auto_restore_limits();

            // Limits can be expanded/redefined
            // New sizes larger than the real size are truncated to it (8 bytes in this case)
            iospan1.limit_wr(0, uint32_t(-1));
            EXPECT_EQ(iospan1.remain_wr(), uint32_t(2));
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(6));

            iospan1.writeall(wrbuf, 1); // no throw
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(7)); // wr position is at 7

            // Make the io read only
            iospan1.limit_to_read_only();
            EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(7)); // wr position at 7 is preserved
            EXPECT_THAT(
                    [&]() { iospan1.writeall(wrbuf, 1); },  // try to write 1 bytes, but 0 is available and fail
                    ThrowsMessage<NotEnoughRoom>(
                        AllOf(
                            HasSubstr("Requested 1 bytes but only 0 bytes are available. "
                                "Write exact-byte-count operation at position 7 failed; "
                                "detected before the write."
                                )
                            )
                        )
                    );
        }

        // Check that the pointers were rewinded and the limits restored
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(6));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(2));

        iospan1.seek_wr(0);
        iospan1.seek_rd(0);

        // Restored to limit: min pos 2; size 4
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(2));

        // Restored to limit: min pos 1; size 1
        EXPECT_EQ(iospan1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(1));
    }

    TEST(IOSpanTest, SeekIsLimitAware) {
        std::vector<char> buf(64);

        IOSpan iospan1(buf);
        iospan1.limit_wr(1, 60);
        iospan1.limit_rd(10, 30);

        // Initial positions
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(60));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(1));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(30));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(10));

        // Read/write pointers are independent
        iospan1.seek_wr(5);
        iospan1.seek_rd(19);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(60-5+1));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(5));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(30-19+10));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(19));

        {
            auto rewind_guard = iospan1.auto_rewind();

            // Past the end is clamp to the segment size by a lot
            iospan1.seek_wr(64);
            iospan1.seek_rd(65);
            EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(61));

            EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
            EXPECT_EQ(iospan1.tell_rd(), uint32_t(40));
        }

        // Past the end is clamp to the segment size
        iospan1.seek_wr(62);
        iospan1.seek_rd(40);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(61));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40));

        // Seek relative the current position in backward direction
        iospan1.seek_wr(2, IOSpan::Seekdir::bwd);
        iospan1.seek_rd(1, IOSpan::Seekdir::bwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(2));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(61 - 2));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(1));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40 - 1));

        // Seek relative the current position in backward direction (validate that it's relative)
        iospan1.seek_wr(6, IOSpan::Seekdir::bwd);
        iospan1.seek_rd(6, IOSpan::Seekdir::bwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(8));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(61 - 8));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40 - 7));

        {
            auto rewind_guard = iospan1.auto_rewind();

            // Past the end is clamp to the segment size by a lot
            iospan1.seek_wr(61 - 8, IOSpan::Seekdir::bwd);
            iospan1.seek_rd(65, IOSpan::Seekdir::bwd);
            EXPECT_EQ(iospan1.remain_wr(), uint32_t(60));
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(1));

            EXPECT_EQ(iospan1.remain_rd(), uint32_t(30));
            EXPECT_EQ(iospan1.tell_rd(), uint32_t(10));
        }

        // Seek past the begin is set to 0; seek relative 0 does not change the pointer
        iospan1.seek_wr(62, IOSpan::Seekdir::bwd);
        iospan1.seek_rd(0, IOSpan::Seekdir::bwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(60));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(1));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40 - 7));

        // Seek relative the current position in forward direction
        iospan1.seek_wr(4, IOSpan::Seekdir::fwd);
        iospan1.seek_rd(4, IOSpan::Seekdir::fwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(60 - 4));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4+1));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7 - 4));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40 - 7 + 4));

        // Seek relative the current position in forward direction, again
        iospan1.seek_wr(2, IOSpan::Seekdir::fwd);
        iospan1.seek_rd(2, IOSpan::Seekdir::fwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(60 - 4 - 2));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(4+2+1));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(7 - 4 - 2));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40 - 7 + 4 + 2));

        {
            auto rewind_guard = iospan1.auto_rewind();

            // Past the end is clamp to the segment size by a lot
            iospan1.seek_wr(64-7, IOSpan::Seekdir::fwd);
            iospan1.seek_rd(30, IOSpan::Seekdir::fwd);
            EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(61));

            EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
            EXPECT_EQ(iospan1.tell_rd(), uint32_t(40));
        }

        // Seek relative the current position in forward direction, past the end
        iospan1.seek_wr(59, IOSpan::Seekdir::fwd);
        iospan1.seek_rd(2, IOSpan::Seekdir::fwd);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(61));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40));

        // Seek relative the end position
        iospan1.seek_wr(0, IOSpan::Seekdir::end);
        iospan1.seek_rd(0, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(61));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40));

        // Again
        iospan1.seek_wr(4, IOSpan::Seekdir::end);
        iospan1.seek_rd(4, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(1));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64-4));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(0));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(40));

        // Again
        iospan1.seek_wr(6, IOSpan::Seekdir::end);
        iospan1.seek_rd(30, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(3));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(64-6));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(6));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(64-30));

        {
            auto rewind_guard = iospan1.auto_rewind();

            // Past the end is clamp to the begin
            iospan1.seek_wr(66, IOSpan::Seekdir::end);
            iospan1.seek_rd(65, IOSpan::Seekdir::end);
            EXPECT_EQ(iospan1.remain_wr(), uint32_t(60));
            EXPECT_EQ(iospan1.tell_wr(), uint32_t(1));

            EXPECT_EQ(iospan1.remain_rd(), uint32_t(30));
            EXPECT_EQ(iospan1.tell_rd(), uint32_t(10));
        }

        // Past the begin goes to the begin
        iospan1.seek_wr(63, IOSpan::Seekdir::end);
        iospan1.seek_rd(60, IOSpan::Seekdir::end);
        EXPECT_EQ(iospan1.remain_wr(), uint32_t(60));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(1));

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(30));
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(10));
    }

    TEST(IOSpanTest, CopyIntoSelfNoOverlap) {
        std::vector<char> buf(256, 0); // zeros

        std::vector<char> wrbuf(256);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..256
        std::vector<char> rdbuf(256);

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf, 64);

        // Initial setup
        iospan1.readall(rdbuf);
        XOZ_EXPECT_BUFFER_SERIALIZATION(rdbuf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iospan1.seek_rd(0);
        iospan1.seek_wr(128);

        // Copy small: read starting from 0 writing starting from 128
        iospan1.copy_into_self(32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(128 + 32));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Now, copy into the end of the io
        iospan1.seek_rd(0);
        iospan1.seek_wr(32, IOBase::Seekdir::end);
        iospan1.copy_into_self(32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(256));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f"
                );

        // Now, copy from and into non-overlapping but very close areas
        iospan1.seek_rd(32);
        iospan1.seek_wr(32+32);
        iospan1.copy_into_self(32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(32 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(32+32+32));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f"
                );

        // The same but the write zone is before the read zone
        iospan1.seek_rd(32);
        iospan1.seek_wr(0);
        iospan1.copy_into_self(32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(32 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(32));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f"
                );

        // Test copy a large odd chunk
        iospan1.seek_rd(0);
        iospan1.seek_wr(128);
        iospan1.copy_into_self(127); // leave one byte out of the 128 to use a weird size

        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 127));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(128 + 127));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 001f"
                );
    }

    TEST(IOSpanTest, CopyIntoSelfOverlap) {
        std::vector<char> buf(256, 0); // zeros

        std::vector<char> wrbuf(256);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..256
        std::vector<char> rdbuf(256);

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf, 64);

        // Initial setup
        iospan1.readall(rdbuf);
        XOZ_EXPECT_BUFFER_SERIALIZATION(rdbuf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iospan1.seek_rd(0);
        iospan1.seek_wr(16);

        // Overlap 16 bytes of these 32; read area is before write area
        iospan1.copy_into_self(32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(16 + 32));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 0001 0203 0405 0607 0809 0a0b 0c0d 0e0f "
                "1011 1213 1415 1617 1819 1a1b 1c1d 1e1f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Let's write overlaps read
        iospan1.seek_rd(32);
        iospan1.seek_wr(16);

        // Overlap 16 bytes of these 32; read area is after write area
        iospan1.copy_into_self(32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(32 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(16 + 32));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Test tiny overlap
        iospan1.seek_rd(1);
        iospan1.seek_wr(0);

        iospan1.copy_into_self(2);

        EXPECT_EQ(iospan1.tell_rd(), uint32_t(1 + 2));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(0 + 2));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0102 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Test full overlap: expected no real copy and no change
        iospan1.seek_rd(16);
        iospan1.seek_wr(16);

        iospan1.copy_into_self(32);

        EXPECT_EQ(iospan1.tell_rd(), uint32_t(16 + 32));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(16 + 32));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0102 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Test copy a medium chunk
        iospan1.seek_rd(0);
        iospan1.seek_wr(62);
        iospan1.copy_into_self(64);

        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 64));
        EXPECT_EQ(iospan1.tell_wr(), uint32_t(62 + 64));

        // Read everything and check
        iospan1.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf, 0, -1,
                "0102 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "3031 3233 3435 3637 3839 3a3b 3c3d 3e3f 3031 3233 3435 3637 3839 3a3b 3c3d 0102 "
                "0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f 3031 "
                "3233 3435 3637 3839 3a3b 3c3d 3e3f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );
    }

    TEST(IOSpanTest, CopyIntoOtherNoOverlap) {
        std::vector<char> buf(256, 0); // zeros
        std::vector<char> buf2(256, 0); // zeros

        std::vector<char> wrbuf(256);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..256
        std::vector<char> rdbuf(256);

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf, 64);

        IOSpan iospan2(buf2);

        // Initial setup
        iospan2.readall(rdbuf);
        XOZ_EXPECT_BUFFER_SERIALIZATION(rdbuf, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iospan1.seek_rd(0);
        iospan2.seek_wr(128);

        // Copy small: read starting from 0 writing starting from 128
        iospan1.copy_into(iospan2, 32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 32));
        EXPECT_EQ(iospan2.tell_wr(), uint32_t(128 + 32));

        // Read everything and check
        iospan2.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf2, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        // Now, copy into the end of the io
        iospan1.seek_rd(0);
        iospan2.seek_wr(32, IOBase::Seekdir::end);
        iospan1.copy_into(iospan2, 32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(0 + 32));
        EXPECT_EQ(iospan2.tell_wr(), uint32_t(256));

        // Read everything and check
        iospan2.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf2, 0, -1,
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f"
                );

        // The same but the write zone is before the read zone
        iospan1.seek_rd(32);
        iospan2.seek_wr(0);
        iospan1.copy_into(iospan2, 32);

        // rd/wr pointers are correctly 32 bytes after their initial positions
        EXPECT_EQ(iospan1.tell_rd(), uint32_t(32 + 32));
        EXPECT_EQ(iospan2.tell_wr(), uint32_t(32));

        // Read everything and check
        iospan2.seek_rd(0);
        XOZ_EXPECT_BUFFER_SERIALIZATION(buf2, 0, -1,
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f"
                );
    }

    TEST(IOSpanTest, CopyIntoSelfNotEnoughRoom) {
        std::vector<char> buf(256, 0); // zeros

        std::vector<char> wrbuf(256);
        std::iota (std::begin(wrbuf), std::end(wrbuf), 0); // fill with 0..256
        std::vector<char> rdbuf(256);

        IOSpan iospan1(buf);
        iospan1.writeall(wrbuf, 64);

        // Initial setup
        iospan1.readall(rdbuf);
        XOZ_EXPECT_BUFFER_SERIALIZATION(rdbuf, 0, -1,
                "0001 0203 0405 0607 0809 0a0b 0c0d 0e0f 1011 1213 1415 1617 1819 1a1b 1c1d 1e1f "
                "2021 2223 2425 2627 2829 2a2b 2c2d 2e2f 3031 3233 3435 3637 3839 3a3b 3c3d 3e3f "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                "0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000 0000"
                );

        iospan1.seek_rd(0);
        iospan1.seek_wr(256);

        EXPECT_EQ(iospan1.remain_wr(), uint32_t(0));
        EXPECT_THAT(
                [&]() { iospan1.copy_into_self(1); },
                ThrowsMessage<NotEnoughRoom>(
                    AllOf(
                        HasSubstr(
                            "Requested 1 bytes but only 0 bytes are available. "
                            "Copy into self IO 1 bytes from read position 0 (this/src) "
                            "to write position 256 (dst) failed due not enough space "
                            "to copy-into (dst:wr); detected before the copy even started."
                            )
                        )
                    )
                );

        iospan1.seek_rd(127);
        iospan1.seek_wr(0);

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(128+1));
        EXPECT_THAT(
                [&]() { iospan1.copy_into_self(128+2); },
                ThrowsMessage<NotEnoughRoom>(
                    AllOf(
                        HasSubstr(
                            "Requested 130 bytes but only 129 bytes are available. "
                            "Copy into self IO 130 bytes from read position 127 (this/src) "
                            "to write position 0 (dst) failed due not enough data "
                            "to copy-from (src:rd); detected before the copy even started."
                            )
                        )
                    )
                );
    }


    TEST(IOSpanTest, CopyIntoOtherNotEnoughRoom) {
        std::vector<char> buf(256, 0); // zeros
        std::vector<char> buf2(256, 0); // zeros

        IOSpan iospan1(buf);
        IOSpan iospan2(buf2);

        iospan1.seek_rd(0);
        iospan2.seek_wr(256);

        EXPECT_EQ(iospan2.remain_wr(), uint32_t(0));
        EXPECT_THAT(
                [&]() { iospan1.copy_into(iospan2, 1); },
                ThrowsMessage<NotEnoughRoom>(
                    AllOf(
                        HasSubstr(
                            "Requested 1 bytes but only 0 bytes are available. "
                            "Copy into another IO 1 bytes from read position 0 (this/src) "
                            "to write position 256 (dst) failed due not enough space "
                            "to copy-into (dst:wr); detected before the copy even started."
                            )
                        )
                    )
                );

        iospan1.seek_rd(127);
        iospan2.seek_wr(0);

        EXPECT_EQ(iospan1.remain_rd(), uint32_t(128+1));
        EXPECT_THAT(
                [&]() { iospan1.copy_into(iospan2, 128+2); },
                ThrowsMessage<NotEnoughRoom>(
                    AllOf(
                        HasSubstr(
                            "Requested 130 bytes but only 129 bytes are available. "
                            "Copy into another IO 130 bytes from read position 127 (this/src) "
                            "to write position 0 (dst) failed due not enough data "
                            "to copy-from (src:rd); detected before the copy even started."
                            )
                        )
                    )
                );
    }
}
