#pragma once

#include <climits>
#include <ios>

#include "xoz/mem/asserts.h"

namespace xoz {

/*
 * Perform a static cast of the argument from source type Src to Dst where
 * both Src and Dst types are integers.
 *
 * The static cast allows for promotion/demotion
 * of the argument and operates between the same or different signness
 * following the rule defined by C++ static_cast<>
 *
 * After the static cast was performed the result is DEBUG-assert checked.
 * The assert fails when the input value n and the output value m differ
 * "semantically".
 *
 * Examples:
 *  - when Src and Dst are have the same sign, then we expect n and m to have the same value
 *    (the absolute value and the sign).
 *  - when they have different sign,
 *      - if Src is signed (therefore Dst is unsigned), n cannot be negative
 *        (otherwise the cast to a unsigned Dst will loose the sign of n)
 *      - if Dst is signed (therefore Src is unsigned), m cannot be negative
 *        (meaning that it gained a sign that n didn't)
 * */
namespace internals {
template <typename Dst, typename Src>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Src> and std::is_integral_v<Dst>, Dst>
        assert_integral_cast_annotated(const Src n, const char* file, unsigned int line, const char* func) noexcept {
    Dst m = static_cast<Dst>(n);
    if constexpr (std::is_signed_v<Src> == std::is_signed_v<Dst>) {
        // If both src and dst have the same "signedness" (both signed or both unsigned),
        // we can see if we lost the value doing a comparison.
        // We are relaying here that the compiler will the correct promotion
        // to compare both
        xoz_internals__assert_annotated("integral cast failed", (m == n), file, line, func);
    } else {
        // If the "signedness" differs, it means we are trying to cast a signed
        // into an unsigned or viceversa. This is ok as long as both are
        // non-negative integers otherwise a negative value cannot be represented
        // by the other int type hence the assert_integral_cast_annotated fails.
        if constexpr (std::is_signed_v<Src>) {
            xoz_internals__assert_annotated("integral cast failed", (n >= 0), file, line, func);
        } else {
            xoz_internals__assert_annotated("integral cast failed", (m >= 0), file, line, func);
        }
    }
    return m;
}
}  // namespace internals

#define assert_u8(n) internals::assert_integral_cast_annotated<uint8_t>(n, __FILE__, __LINE__, __func__)
#define assert_u16(n) internals::assert_integral_cast_annotated<uint16_t>(n, __FILE__, __LINE__, __func__)
#define assert_u32(n) internals::assert_integral_cast_annotated<uint32_t>(n, __FILE__, __LINE__, __func__)
#define assert_u64(n) internals::assert_integral_cast_annotated<uint64_t>(n, __FILE__, __LINE__, __func__)

#define assert_streamsize(n) internals::assert_integral_cast_annotated<std::streamsize>(n, __FILE__, __LINE__, __func__)
#define assert_streamoff(n) internals::assert_integral_cast_annotated<std::streamoff>(n, __FILE__, __LINE__, __func__)

/*
 * The as_char_ptr() and as_u8_ptr() are casts between char and uint8_t.
 * These work under the "reasonable" assumtion that a char is a 8-bits byte.
 * God protect us if that is not true!
 * */
// TODO on modern C++, reinterpret_cast *is* a constexpr so we should tag them as such
[[nodiscard]] /*constexpr*/ inline char* as_char_ptr(uint8_t* p) noexcept {
    static_assert(sizeof(char) == sizeof(uint8_t) and CHAR_BIT == 8);
    return reinterpret_cast<char*>(p);
}

[[nodiscard]] /*constexpr*/ inline const char* as_char_ptr(const uint8_t* p) noexcept {
    static_assert(sizeof(char) == sizeof(uint8_t) and CHAR_BIT == 8);
    return reinterpret_cast<const char*>(p);
}

[[nodiscard]] /*constexpr*/ inline uint8_t* as_u8_ptr(char* p) noexcept {
    static_assert(sizeof(char) == sizeof(uint8_t) and CHAR_BIT == 8);
    return reinterpret_cast<uint8_t*>(p);
}

[[nodiscard]] /*constexpr*/ inline const uint8_t* as_u8_ptr(const char* p) noexcept {
    static_assert(sizeof(char) == sizeof(uint8_t) and CHAR_BIT == 8);
    return reinterpret_cast<const uint8_t*>(p);
}

}  // namespace xoz
