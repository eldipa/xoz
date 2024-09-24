#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <ios>

#ifndef NDEBUG
#include <cstdio>
#endif

namespace xoz {

#ifdef NDEBUG
#define xoz_internals__assert_annotated(msg, cond, file, line, func) ((void)0)
#define xoz_assert(msg, cond) ((void)0)
#else
namespace internals {
void print_error(const char* msg, const char* cond_str, const char* file, unsigned int line, const char* func_str);
void abort_execution();
}  // namespace internals

#define xoz_internals__assert_annotated(msg, cond, file, line, func)      \
    do {                                                                  \
        if (cond) {                                                       \
            internals::print_error((msg), #cond, (file), (line), (func)); \
            internals::abort_execution();                                 \
        }                                                                 \
    } while (0)

/*
 * This macro checks the condition and it yields false, it will abort the
 * program printing to stderr the given message next to the file, line and
 * function where the xoz_assert was called.
 *
 * This macro is a no-op if NDEBUG is defined. Essentially, it works
 * as the good old C assert() macro.
 * */
#define xoz_assert(msg, cond) xoz_internals__assert_annotated(msg, cond, __FILE__, __LINE__, __func__)
#endif


/*
 * Perform a subtraction between the arguments:
 *  chk_subtraction(a, b) --> a - b
 *
 * The operation is defined for unsigned integers only.
 * If the operation would end up in an underflow (when a < b),
 * a DEBUG-assert will fail.
 * */
template <typename Int>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Int> and std::is_unsigned_v<Int>, Int>
        assert_subtraction(const Int a, const Int b) noexcept {
    assert("(a > b), subtraction will underflow" && (a >= b));
    return a - b;
}

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
template <typename Dst, typename Src>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Src> and std::is_integral_v<Dst>, Dst>
        assert_integral_cast(const Src n) noexcept {
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
        // by the other int type hence the assert_integral_cast fails.
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
    return assert_integral_cast<uint8_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline uint16_t assert_u16(const Src n) noexcept {
    return assert_integral_cast<uint16_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline uint32_t assert_u32(const Src n) noexcept {
    return assert_integral_cast<uint32_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline uint64_t assert_u64(const Src n) noexcept {
    return assert_integral_cast<uint64_t>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline std::streamsize assert_streamsize(const Src n) noexcept {
    return assert_integral_cast<std::streamsize>(n);
}

template <typename Src>
[[nodiscard]] constexpr inline std::streamsize assert_streamoff(const Src n) noexcept {
    return assert_integral_cast<std::streamoff>(n);
}
}  // namespace xoz
