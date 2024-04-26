#pragma once

#include <cstdint>

#include "xoz/err/exceptions.h"
#include "xoz/mem/bits.h"

class IOBase;

/*
 * The function computes the checksum of the given value.
 * The returned value must be processed by fold_inet_checksum()
 * to get a valid checksum.
 * This function does not do that for performance reasons.
 * */
constexpr inline uint32_t inet_checksum(const uint32_t val) { return (val >> 16) + (val & 0xffff); }

/*
 * Wrap as 1's complement the 4 bytes checksum into 2 bytes checksum.
 * In other words, the 2 most significant bytes are added to
 * the 2 less significant bytes.
 *
 * The checksum is stored in the two less significant bytes of the returned uint32_t.
 * See inet_checksum().
 * */
constexpr inline uint32_t fold_inet_checksum(uint32_t checksum) {
    while (checksum >> 16) {
        checksum = inet_checksum(checksum);
    }

    return checksum;
}

/*
 * Compute the RFC 1071 "Computing the Internet Checksum" checksum for
 * the 2 bytes words of the buffer.
 *
 * The checksum is stored in the two less significant bytes of the returned uint32_t.
 *
 * The function works up to  uint16_t(0xffff) word count. Larger inputs must be split
 * by the caller.
 * */
constexpr inline uint32_t inet_checksum(const uint16_t* const buf, const uint16_t word_cnt) {
    assert(buf);

    // NOTE: we could work with uint64_t and then fold to uint16_t back and get some speed
    uint32_t checksum = 0;
    for (uint32_t ix = 0; ix < word_cnt; ++ix) {
        checksum += buf[ix];
    }

    // In the worst case, we have 0xffff words all of 0xffff value.
    // So the sum will be at most 0xffff * 0xffff which fits perfectly
    // in a uint32_t without overflow.
    return fold_inet_checksum(checksum);
}

/*
 * The size given is in terms of bytes. It must be a multiple of 2.
 * Internally, the function works up to  uint16_t(0xffff) word count of 2 bytes words
 * so sz cannot be larger than (0xffff << 1).
 * Larger inputs must be split by the caller.
 * */
inline uint32_t inet_checksum(const uint8_t* const buf, const uint32_t sz) {
    assert(sz % 2 == 0);
    assert(is_aligned(buf, 2));
    return inet_checksum((uint16_t*)buf, assert_u16(sz >> 1));
}


/*
 * Compute the RFC 1071 "Computing the Internet Checksum" checksum for
 * the bytes of the io from the begin position to the end position (half open).
 *
 * The amount of bytes must be divisible by two.
 * See inet_checksum() doc for more context, in particular, the count of bytes
 * cannot be larger than twice 0xffff.
 * */
uint32_t inet_checksum(IOBase& io, const uint32_t begin, const uint32_t end);

/*
 * Return true if the checksum is good. We define "good" as the checksum
 * computed over an input data *and* over its bit-wise inversed checksum so
 * the resulting checksum (to be checked) is good if and only if it is
 * 0 in 1's complement. This is a fancy way to say that it is 0 or 0xffff.
 *
 * If the 2 most significant bytes of the given argument (uint32_t) are
 * not zero, it is an error and the function will throw.
 * If it is not an error, the caller should call fold_inet_checksum().
 * */
constexpr inline bool is_inet_checksum_good(const uint32_t checksum) {
    if (checksum >> 16) {
        throw std::runtime_error("Checksum value is invalid, its 2 most significant bytes are non-zero.");
    }

    uint16_t ls16 = uint16_t(checksum);
    return ls16 == 0 or ls16 == 0xffff;
}
