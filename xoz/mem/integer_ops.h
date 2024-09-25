#pragma once

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
}  // namespace xoz
