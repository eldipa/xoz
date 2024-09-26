#pragma once

#include "xoz/mem/asserts.h"

namespace xoz {

namespace internals {

/*
 * Check that if we add the two operands the resulting number is still representable
 * by the same Src type.
 *
 * Note: due how promotions works, using this template function may not work as expected
 * If two uint16_t are given but for some reason there is an promotion to uint32_t,
 * the caller may be calling this template function for uint32_t, getting a positive
 * test and then failing when doing the real addition with uint16_t.
 *
 * For this reason, callers should use test_uNN_add functions family.
 * */
template <typename UInt>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<UInt> and std::is_unsigned_v<UInt>, bool>
        test_unsigned_int_add(const UInt a, const UInt b) {
    UInt tmp = a + b;
    return tmp < a;
}
}  // namespace internals

[[nodiscard]] constexpr inline bool test_u16_add(uint16_t a, uint16_t b) {
    return internals::test_unsigned_int_add<uint16_t>(a, b);
}
[[nodiscard]] constexpr inline bool test_u32_add(uint32_t a, uint32_t b) {
    return internals::test_unsigned_int_add<uint32_t>(a, b);
}
[[nodiscard]] constexpr inline bool test_u64_add(uint64_t a, uint64_t b) {
    return internals::test_unsigned_int_add<uint64_t>(a, b);
}


namespace internals {
/*
 * Return (a + b) and check that the result didn't wrap around/overflow.
 * */
template <typename UInt>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<UInt> and std::is_unsigned_v<UInt>, UInt>
        unsigned_int_add_nowrap_annotated(const UInt a, const UInt b, const char* file, unsigned int line,
                                          const char* func) {
    xoz_internals__assert_annotated("add wrapped around", internals::test_unsigned_int_add<UInt>(a, b), file, line,
                                    func);
    return a + b;
}
/*
 * Return (a - b) and check that the result is still representable by the same unsigned Src type.
 * (aka, remains positive)
 * */
template <typename UInt>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<UInt> and std::is_unsigned_v<UInt>, UInt>
        unsigned_int_sub_is_nonneg_annotated(const UInt a, const UInt b, const char* file, unsigned int line,
                                             const char* func) {
    xoz_internals__assert_annotated("sub went negative", (a >= b), file, line, func);
    return a - b;
}
}  // namespace internals


#define assert_u32_add_nowrap(a, b) \
    internals::unsigned_int_add_nowrap_annotated<uint32_t>(a, b, __FILE__, __LINE__, __func__)

#define assert_u8_sub_nonneg(a, b) \
    internals::unsigned_int_sub_is_nonneg_annotated<uint8_t>(a, b, __FILE__, __LINE__, __func__)
#define assert_u32_sub_nonneg(a, b) \
    internals::unsigned_int_sub_is_nonneg_annotated<uint32_t>(a, b, __FILE__, __LINE__, __func__)


namespace internals {
/*
 * Read the selected bits specified by mask from the given field. The value returned
 * is cast to the return type T.
 *
 * Note: mask must be non-zero.
 * */
template <typename Dst, typename Src>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Src> and std::is_integral_v<Dst> and
                                                                 std::is_unsigned_v<Src> and std::is_unsigned_v<Dst>,
                                                         Dst>
        assert_read_bits_annotated(const Src field, const Src mask, const char* file, unsigned int line,
                                   const char* func) {
    xoz_internals__assert_annotated("bad mask", mask, file, line, func);
    int shift = std::countr_zero(mask);

    const Src v1 = assert_integral_cast_annotated<Src>(((field & mask) >> shift), file, line, func);
    return assert_integral_cast_annotated<Dst>(v1, file, line, func);
}

/*
 * Write the value of type T into the selected bits specified by mask of the given field.
 *
 * Note: mask must be non-zero.
 * */
template <typename Dst, typename Src>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<Src> and std::is_integral_v<Dst> and
                                                                 std::is_unsigned_v<Src> and std::is_unsigned_v<Dst>,
                                                         void>
        assert_write_bits_annotated(Dst& field, const Src val, const Dst mask, const char* file, unsigned int line,
                                    const char* func) {
    xoz_internals__assert_annotated("bad mask", mask, file, line, func);
    int shift = std::countr_zero(mask);
    field |= assert_integral_cast_annotated<Dst>((val << shift) & mask, file, line, func);
}
}  // namespace internals


#define assert_read_bits_from_u16(dst_type, field, mask) \
    internals::assert_read_bits_annotated<dst_type, uint16_t>((field), (mask), __FILE__, __LINE__, __func__)
#define assert_read_bits_from_u32(dst_type, field, mask) \
    internals::assert_read_bits_annotated<dst_type, uint32_t>((field), (mask), __FILE__, __LINE__, __func__)

#define assert_write_bits_into_u16(field, val, mask) \
    internals::assert_write_bits_annotated<uint16_t>((field), (val), (mask), __FILE__, __LINE__, __func__)
#define assert_write_bits_into_u32(field, val, mask) \
    internals::assert_write_bits_annotated<uint32_t>((field), (val), (mask), __FILE__, __LINE__, __func__)


// Calculate the log2 of a uint16_t or uint32_t values
[[nodiscard]] constexpr inline uint8_t u16_log2_floor(uint16_t x) { return uint8_t(16 - std::countl_zero(x) - 1); }
constexpr inline uint8_t u32_log2_floor(uint32_t x) { return uint8_t(32 - std::countl_zero(x) - 1); }

[[nodiscard]] constexpr inline uint8_t u16_count_bits(uint16_t x) { return uint8_t(std::popcount(x)); }

}  // namespace xoz
