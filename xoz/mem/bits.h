#pragma once

#include <bit>
#include <cassert>
#include <cstdint>

namespace xoz {

// Calculate the log2 of a uint16_t or uint32_t values
[[nodiscard]] constexpr inline uint8_t u16_log2_floor(uint16_t x) { return uint8_t(16 - std::countl_zero(x) - 1); }
constexpr inline uint8_t u32_log2_floor(uint32_t x) { return uint8_t(32 - std::countl_zero(x) - 1); }

[[nodiscard]] constexpr inline uint8_t u16_count_bits(uint16_t x) { return uint8_t(std::popcount(x)); }

[[nodiscard]] constexpr inline bool u16_add_will_overflow(uint16_t a, uint16_t b) {
    uint16_t tmp = a + b;
    return tmp < a;
}

[[nodiscard]] constexpr inline bool u32_add_will_overflow(uint32_t a, uint32_t b) {
    uint32_t tmp = a + b;
    return tmp < a;
}

[[nodiscard]] constexpr inline bool u64_add_will_overflow(uint64_t a, uint64_t b) {
    uint64_t tmp = a + b;
    return tmp < a;
}

[[nodiscard]] constexpr inline bool u32_fits_into_u16(uint32_t a) { return a == (uint16_t(a)); }

template <typename Dst, typename Src>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Src> and std::is_integral_v<Dst>, Dst>
        integral_cast_checked(const Src n) noexcept {
    Dst m = static_cast<Dst>(n);
    if constexpr (std::is_signed_v<Src> == std::is_signed_v<Dst>) {
        // If both src and dst have the same "signedness" (both signed or both unsigned),
        // we can see if we lost the value doing a comparison.
        // We are relaying here that the compiler will the correct promotion
        // to compare both
        assert("integral cast failed" && (m == n));
    } else {
        // If the "signedness" differs, it means we are trying to cast a signed
        // into an unsigned or viceversa. This is ok as long as both are
        // non-negative integers otherwise a negative value cannot be represented
        // by the other int type hence the integral_cast_checked fails.
        if constexpr (std::is_signed_v<Src>) {
            assert("integral cast failed" && (n >= 0));
        } else {
            assert("integral cast failed" && (m >= 0));
        }
    }
    return m;
}

template <typename Src>
[[nodiscard]] constexpr inline uint8_t assert_u8(const Src n) noexcept {
    return integral_cast_checked<uint8_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline uint16_t assert_u16(const Src n) noexcept {
    return integral_cast_checked<uint16_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline uint32_t assert_u32(const Src n) noexcept {
    return integral_cast_checked<uint32_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline uint64_t assert_u64(const Src n) noexcept {
    return integral_cast_checked<uint64_t>(n);
}

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
