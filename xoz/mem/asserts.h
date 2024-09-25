#pragma once

#include <bit>
#include <cassert>
#include <cstdint>

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
        if (!(cond)) {                                                    \
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
 * Read the selected bits specified by mask from the given field, both of type Src.
 * The value returned is cast to the return type Dst.
 *
 * Note: mask must be non-zero and both Src and Dst must be integral unsigned types.
 * */
template <typename Dst, typename Src>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Src> and std::is_integral_v<Dst> and
                                                                 std::is_unsigned_v<Src> and std::is_unsigned_v<Dst>,
                                                         Dst>
        assert_read_bits_annotated(const Src field, const Src mask, const char* file, unsigned int line,
                                   const char* func) {
    assert(mask);
    int shift = std::countr_zero(mask);

    Src v1 = assert_integral_cast_annotated<Src>(((field & mask) >> shift), file, line, func);
    return assert_integral_cast_annotated<Dst>(v1, file, line, func);
}

/*
 * Write the value of type T into the selected bits specified by mask of the given field.
 *
 * Note: mask must be non-zero.
 * */
/*
template <typename T>
constexpr inline typename std::enable_if_t<std::is_integral_v<T> and std::is_unsigned_v<T>, void>
        write_bitsfield_into_u16(uint16_t& field, T val, uint16_t mask) {
    assert(mask);
    int shift = std::countr_zero(mask);
    field |= assert_u16(uint16_t(val << shift) & mask);
}
*/

}  // namespace xoz
