#pragma once

#include <bit>
#include <cassert>
#include <cstdint>


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

constexpr inline bool u32_fits_into_u16(uint32_t a) { return a == (uint16_t(a)); }


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


template <typename T>
constexpr inline T read_bitsfield_from_u32(uint32_t field, uint32_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    return T((field & mask) >> shift);
}

template <typename T>
constexpr inline void write_bitsfield_into_u32(uint32_t& field, T val, uint32_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    field |= uint32_t((val << shift) & mask);
}


constexpr uint8_t assert_u8(uint32_t n) {
    assert(/*0 <= n and*/ n <= (uint8_t)-1);
    return (uint8_t)n;
}

template <typename T>
constexpr inline uint32_t assert_u32(T n) {
    if constexpr (sizeof(T) < sizeof(uint32_t)) {
        // The return stmt should make the compiler to check if the value T
        // can be promoted to uint32_t (checking signness).
        return n;
    } else {
        // We require a check in runtime. Here we let the uint32_t -1 (max)
        // to be promoted to T. This will check signess too.
        assert(0 <= n and n <= (uint32_t)-1);
        return (uint32_t)n;
    }
}

template <typename T>
constexpr inline uint16_t assert_u16(T n) {
    if constexpr (sizeof(T) < sizeof(uint16_t)) {
        // The return stmt should make the compiler to check if the value T
        // can be promoted to uint16_t (checking signness).
        return n;
    } else {
        // We require a check in runtime. Here we let the uint16_t -1 (max)
        // to be promoted to T. This will check signess too.
        assert(0 <= n and n <= (uint16_t)-1);
        return (uint16_t)n;
    }
}
