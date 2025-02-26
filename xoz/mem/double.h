#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <type_traits>

#include "xoz/mem/casts.h"
#include "xoz/mem/endianness.h"
#include "xoz/mem/integer_ops.h"

namespace xoz {

/*
 * Serialize a double with half-float precision to an uint16_t.
 * A half-float has 5 bits for the exponent and 11 bits for the mantisa.
 *
 * If the number is too large or it is too small than 5 bits for the exponent
 * is not enough, throw an exception.
 * */
uint16_t half_float_to_le(double num);
double half_float_from_le(uint16_t num);

/*
 * Serialize a double with float precision to an uint32_t.
 * A float has 8 bits for the exponent and 24 bits for the mantisa.
 *
 * If the number is too large or it is too small than 8 bits for the exponent
 * is not enough, throw an exception.
 * */
uint32_t single_float_to_le(double num);
double single_float_from_le(uint32_t num);

/*
 * Serialize a double with double precision to an uint64_t.
 * A float has 11 bits for the exponent and 53 bits for the mantisa.
 *
 * If the number is too large or it is too small than 11 bits for the exponent
 * is not enough, throw an exception.
 * */
// uint32_t double_float_to_le(double num);
// double double_float_from_le(uint32_t num);

/*
 * Map the double float value <d> to an integer as follow:
 *
 *  - if d is 0, return 0
 *  - if d is between (-1, -0.5], return [lo, 0)
 *  - if d is between [0.5, 1), return (0, hi]
 *
 * where lo < 0 < hi
 * Note: (a, b] denotes an open-half range with 'a' not included and 'b' included.
 *
 * The value <d> *must*:
 *
 *  - be in (-1, -0.5]
 *  - or be in [0.5, 1)
 *  - or be equal to 0
 *
 * Values outside those ranges or NaN or infinite will fail.
 * */
template <typename SInt>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<SInt> and std::is_signed_v<SInt>, SInt>
        rescale_double_to_int(const double d, const SInt lo, const SInt hi) {
    if (d == 0) {
        return 0;
    }

    assert(not std::isinf(d));
    assert(std::isfinite(d));

    assert((-1 < d && d <= -0.5) || (0.5 <= d && d < 1.0));
    assert(lo < 0 && hi > 0);

    if (d > 0) {
        const SInt scaled = SInt(std::round((d - 0.5) * 2 * (hi - 1)));
        return scaled + 1;
    } else {
        const SInt scaled = SInt(std::round(-(d + 0.5) * 2 * (lo + 1)));
        return scaled - 1;
    }
}

/*
 * Reverse operation of rescale_double_to_int.
 *
 * Returns a double value <d>:
 *
 *  - in (-1, -0.5]
 *  - or in [0.5, 1)
 *  - or equal to 0
 *
 * */
template <typename SInt>
[[nodiscard]] constexpr inline typename std::enable_if_t<std::is_integral_v<SInt> and std::is_signed_v<SInt>, double>
        rescale_int_to_double(const SInt i, const SInt lo, const SInt hi) {
    if (i == 0) {
        return 0;
    }

    assert(lo < 0 && hi > 0);
    assert(lo <= i && i <= hi);

    const double d = (i > 0) ? ((double(i - 1) / (hi)) / 2) + 0.5 : ((-double(i + 1) / (lo)) / 2) - 0.5;

    assert(not std::isinf(d));
    assert(std::isfinite(d));

    assert((-1 < d && d <= -0.5) || (0.5 <= d && d < 1.0));
    return d;
}

namespace internals {

template <typename UInt, unsigned exp_bits>
UInt impl_double_to_le(double num) {
    using SInt = std::make_signed_t<UInt>;
    if (std::isinf(num) or not std::isfinite(num)) {
        throw std::invalid_argument("Argument is either infinite or NaN.");  // TODO
    }

    // TODO check T is uint16_t or uint32_t

    const unsigned type_bits = sizeof(UInt) << 3;
    static_assert(type_bits == 16 or type_bits == 32);

    static_assert(exp_bits < type_bits);
    const unsigned mant_bits = type_bits - exp_bits;

    const int min_exp = -(1 << (exp_bits - 1));
    const int max_exp = (1 << (exp_bits - 1)) - 1;

    const SInt min_mant = SInt(-(1 << (mant_bits - 1)));
    const SInt max_mant = SInt((1 << (mant_bits - 1)) - 1);

    const UInt exp_mask = UInt((UInt(-1)) << mant_bits);
    const UInt mant_mask = UInt(~exp_mask);

    int exp;
    double mant = frexp(num, &exp);

    if (exp < min_exp or exp > max_exp) {
        throw std::domain_error("Exponent is out of range.");  // TODO
    }

    unsigned rawexp = xoz::internals::signed_cast_to_2complement(exp);

    SInt scaledmant_int = rescale_double_to_int<SInt>(mant, min_mant, max_mant);
    unsigned rawmant = xoz::internals::signed_cast_to_2complement(scaledmant_int);

    UInt data = 0;
    if constexpr (type_bits == 16) {
        assert_write_bits_into_u16(data, rawexp, exp_mask);
        assert_write_bits_into_u16(data, rawmant, mant_mask);

        return u16_to_le(data);
    } else if (type_bits == 32) {
        assert_write_bits_into_u32(data, rawexp, exp_mask);
        assert_write_bits_into_u32(data, rawmant, mant_mask);

        return u32_to_le(data);
    } else {
        return 0;  // we should never reach here
    }
}

template <typename UInt, unsigned exp_bits>
double impl_double_from_le(UInt data) {
    using SInt = std::make_signed_t<UInt>;

    const unsigned type_bits = sizeof(UInt) << 3;
    static_assert(type_bits == 16 or type_bits == 32);

    // TODO check T is uint16_t or uint32_t

    static_assert(exp_bits < type_bits);
    const unsigned mant_bits = type_bits - exp_bits;

    const UInt exp_mask = UInt((UInt(-1)) << mant_bits);
    const UInt mant_mask = UInt(~exp_mask);

    const UInt exp_hi_bit = UInt(1 << (exp_bits - 1));
    const UInt mant_hi_bit = UInt(1 << (mant_bits - 1));

    const SInt min_mant = -(1 << (mant_bits - 1));
    const SInt max_mant = (1 << (mant_bits - 1)) - 1;

    UInt rawexp, rawmant;
    if constexpr (type_bits == 16) {
        data = u16_from_le(data);

        rawexp = assert_read_bits_from_u16(UInt, data, exp_mask);
        rawmant = assert_read_bits_from_u16(UInt, data, mant_mask);
    } else if (type_bits == 32) {
        data = u32_from_le(data);

        rawexp = assert_read_bits_from_u32(UInt, data, exp_mask);
        rawmant = assert_read_bits_from_u32(UInt, data, mant_mask);
    } else {
        return 0;  // we should never reach here
    }

    // sign extension
    if (rawexp & exp_hi_bit) {
        rawexp = UInt((UInt(-1) << exp_bits) | rawexp);
    }

    if (rawmant & mant_hi_bit) {
        rawmant = UInt((UInt(-1) << mant_bits) | rawmant);
    }

    SInt exp = xoz::internals::signed_cast_from_2complement(rawexp);
    SInt scaledmant_int = xoz::internals::signed_cast_from_2complement(rawmant);

    double mant = rescale_int_to_double<SInt>(scaledmant_int, min_mant, max_mant);

    return std::ldexp(mant, exp);
}


}  // namespace internals


}  // namespace xoz
