#pragma once

#include <bit>
#include <cassert>
#include <cstdint>

#include "xoz/mem/asserts.h"
#include "xoz/mem/casts.h"

namespace xoz {

// Calculate the log2 of a uint16_t or uint32_t values
[[nodiscard]] constexpr inline uint8_t u16_log2_floor(uint16_t x) { return uint8_t(16 - std::countl_zero(x) - 1); }
constexpr inline uint8_t u32_log2_floor(uint32_t x) { return uint8_t(32 - std::countl_zero(x) - 1); }

[[nodiscard]] constexpr inline uint8_t u16_count_bits(uint16_t x) { return uint8_t(std::popcount(x)); }


/*
 * Read the selected bits specified by mask from the given field. The value returned
 * is cast to the return type T.
 *
 * Note: mask must be non-zero.
 * */
template <typename T>
[[nodiscard]] constexpr inline T read_bitsfield_from_u16(uint16_t field, uint16_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    return T(assert_u16((field & mask) >> shift));
}

/*
 * Write the value of type T into the selected bits specified by mask of the given field.
 *
 * Note: mask must be non-zero.
 * */
template <typename T>
constexpr inline typename std::enable_if_t<std::is_integral_v<T> and std::is_unsigned_v<T>, void>
        write_bitsfield_into_u16(uint16_t& field, T val, uint16_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    field |= assert_u16(uint16_t(val << shift) & mask);
}


template <typename T>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<T> and std::is_unsigned_v<T>, T>
        read_bitsfield_from_u32(uint32_t field, uint32_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    return T(assert_u32((field & mask) >> shift));
}

template <typename T>
constexpr inline typename std::enable_if_t<std::is_integral_v<T> and std::is_unsigned_v<T>, void>
        write_bitsfield_into_u32(uint32_t& field, T val, uint32_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    field |= assert_u32(uint32_t(val << shift) & mask);
}
}  // namespace xoz
