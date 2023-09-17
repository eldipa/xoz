#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

constexpr uint16_t u16_byteswap(uint16_t x) noexcept { return (uint16_t)((x >> 8) | (x << 8)); }

constexpr uint32_t u32_byteswap(uint32_t x) noexcept {
    return ((x >> 24) & 0x000000ff) | ((x << 8) & 0x00ff0000) | ((x >> 8) & 0x0000ff00) | ((x << 24) & 0xff000000);
}

constexpr uint64_t u64_byteswap(uint64_t x) noexcept {
    x = (x & 0x00000000FFFFFFFF) << 32 | (x & 0xFFFFFFFF00000000) >> 32;
    x = (x & 0x0000FFFF0000FFFF) << 16 | (x & 0xFFFF0000FFFF0000) >> 16;
    x = (x & 0x00FF00FF00FF00FF) << 8 | (x & 0xFF00FF00FF00FF00) >> 8;
    return x;
}

// I know, talking about "endianness" of a single-byte variable makes
// little sense.
//
// This function is to make explicit the intention of using "little endian"
// and to check that the argument/return value are of uint8_t
constexpr inline uint8_t u8_to_le(uint8_t x) { return x; }

constexpr inline uint16_t u16_to_le(uint16_t x) {
    if constexpr (std::endian::native == std::endian::big) {
        return u16_byteswap(x);
    } else {
        return x;
    }
}

constexpr inline uint32_t u32_to_le(uint32_t x) {
    if constexpr (std::endian::native == std::endian::big) {
        return u32_byteswap(x);
    } else {
        return x;
    }
}

constexpr inline uint64_t u64_to_le(uint64_t x) {
    if constexpr (std::endian::native == std::endian::big) {
        return u64_byteswap(x);
    } else {
        return x;
    }
}

// Converting from little endian to native is the same as going
// from native to little endian.
#define u8_from_le(X) u8_to_le(X)
#define u16_from_le(X) u16_to_le(X)
#define u32_from_le(X) u32_to_le(X)
#define u64_from_le(X) u64_to_le(X)

// Calculate the log2 of a uint16_t or uint32_t values
constexpr inline int u16_log2_floor(uint16_t x) { return (16 - std::countl_zero(x) - 1); }
constexpr inline int u32_log2_floor(uint32_t x) { return (32 - std::countl_zero(x) - 1); }

constexpr inline uint8_t u16_count_bits(uint16_t x) { return (uint8_t)std::popcount(x); }

constexpr inline bool u16_add_will_overflow(uint16_t a, uint16_t b) {
    uint16_t tmp = a + b;
    return tmp < a;
}

constexpr inline bool u32_add_will_overflow(uint32_t a, uint32_t b) {
    uint32_t tmp = a + b;
    return tmp < a;
}

constexpr inline bool u64_add_will_overflow(uint64_t a, uint64_t b) {
    uint64_t tmp = a + b;
    return tmp < a;
}

constexpr inline uint16_t read_u16_from_le(const char** dataptr) {
    uint16_t x = *(uint16_t*)(*dataptr);
    *dataptr += sizeof(uint16_t);

    return u16_from_le(x);
}

constexpr inline void write_u16_to_le(char** dataptr, uint16_t x) {
    x = u16_to_le(x);
    *(uint16_t*)(*dataptr) = x;
    *dataptr += sizeof(uint16_t);
}

inline std::span<const char> viewof(const std::vector<char>& datavec) { return {datavec.data(), datavec.size()}; }

inline std::span<char> viewof(std::vector<char>& datavec) { return {datavec.data(), datavec.size()}; }


/*
 * Read the selected bits specified by mask from the given field. The value returned
 * is cast to the return type T.
 *
 * Note: mask must be non-zero.
 * */
template <typename T>
constexpr inline T read_bitsfield_from_u16(uint16_t field, uint16_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    return T((field & mask) >> shift);
}

/*
 * Write the value of type T into the selected bits specified by mask of the given field.
 *
 * Note: mask must be non-zero.
 * */
template <typename T>
constexpr inline void write_bitsfield_into_u16(uint16_t& field, T val, uint16_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    field |= uint16_t((val << shift) & mask);
}
