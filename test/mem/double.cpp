#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"

#include "xoz/mem/double.h"
#include "xoz/mem/casts.h"

#include <numeric>
#include <vector>

// TODO HEY
#include "xoz/mem/endianness.h"
#include "xoz/mem/integer_ops.h"
#include <type_traits>
#include <cassert>
#include <cmath>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;
using ::testing_xoz::helpers::ensure_called_once;

using namespace ::xoz;

namespace {
    TEST(DoubleTest, TwoComplement) {
        const int16_t lo = -32768, hi = 32767;
        for (int16_t si = lo; si <  hi; ++si) { // TODO <=
            uint16_t ui = xoz::internals::signed_cast_to_2complement(si);
            int16_t sj = xoz::internals::signed_cast_from_2complement(ui);

            EXPECT_EQ(si, sj);
        }
    }

    TEST(DoubleTest, RescaleDoubleToIntSmall) {
        const int16_t lo = -16384, hi = 16383; // [-2**14 , 2**14)
        EXPECT_EQ(rescale_double_to_int<int16_t>(0, lo, hi), (int16_t)0);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.5, lo, hi), (int16_t)1);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.5, lo, hi), (int16_t)-1);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.999999999999999, lo, hi), (int16_t)hi);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.99999999999999, lo, hi), (int16_t)lo);
    }

    TEST(DoubleTest, RescaleDoubleToIntLarge) {
        const int16_t lo = -32768, hi = 32767; // [-2**15 , 2**15)
        EXPECT_EQ(rescale_double_to_int<int16_t>(0, lo, hi), (int16_t)0);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.5, lo, hi), (int16_t)1);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.5, lo, hi), (int16_t)-1);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.999999999999999, lo, hi), (int16_t)hi);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.99999999999999, lo, hi), (int16_t)lo);
    }

    TEST(DoubleTest, RescaleDoubleToIntAsymmetric) {
        {
        const int16_t lo = -16384, hi = 32767; // [-2**14 , 2**15)
        EXPECT_EQ(rescale_double_to_int<int16_t>(0, lo, hi), (int16_t)0);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.5, lo, hi), (int16_t)1);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.5, lo, hi), (int16_t)-1);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.999999999999999, lo, hi), (int16_t)hi);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.99999999999999, lo, hi), (int16_t)lo);
        }

        {
        const int16_t lo = -32768, hi = 16383; // [-2**15 , 2**14)
        EXPECT_EQ(rescale_double_to_int<int16_t>(0, lo, hi), (int16_t)0);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.5, lo, hi), (int16_t)1);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.5, lo, hi), (int16_t)-1);

        EXPECT_EQ(rescale_double_to_int<int16_t>(0.999999999999999, lo, hi), (int16_t)hi);
        EXPECT_EQ(rescale_double_to_int<int16_t>(-0.99999999999999, lo, hi), (int16_t)lo);
        }
    }

    TEST(DoubleTest, RescaleIntToDoubleSmall) {
        const int16_t lo = -16384, hi = 16383; // [-2**14 , 2**14)
        const double eps = 1.0 / hi;
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(0, lo, hi), (double)0);

        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(1, lo, hi), (double)0.5);
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(-1, lo, hi), (double)-0.5);

        EXPECT_NEAR(rescale_int_to_double<int16_t>(hi, lo, hi), (double)0.999999999999999, eps);
        EXPECT_NEAR(rescale_int_to_double<int16_t>(lo, lo, hi), (double)-0.99999999999999, eps);
    }

    TEST(DoubleTest, RescaleIntToDoubleLarge) {
        const int16_t lo = -32768, hi = 32767; // [-2**15 , 2**15)
        const double eps = 1.0 / hi;
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(0, lo, hi), (double)0);

        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(1, lo, hi), (double)0.5);
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(-1, lo, hi), (double)-0.5);

        EXPECT_NEAR(rescale_int_to_double<int16_t>(hi, lo, hi), (double)0.999999999999999, eps);
        EXPECT_NEAR(rescale_int_to_double<int16_t>(lo, lo, hi), (double)-0.99999999999999, eps);
    }

    TEST(DoubleTest, RescaleIntToDoubleAsymmetric) {
        {
        const int16_t lo = -16384, hi = 32767; // [-2**14 , 2**15)
        const double eps1 = 1.0 / hi;
        const double eps2 = 1.0 / (-lo);
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(0, lo, hi), (double)0);

        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(1, lo, hi), (double)0.5);
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(-1, lo, hi), (double)-0.5);

        EXPECT_NEAR(rescale_int_to_double<int16_t>(hi, lo, hi), (double)0.999999999999999, eps1);
        EXPECT_NEAR(rescale_int_to_double<int16_t>(lo, lo, hi), (double)-0.99999999999999, eps2);
        }

        {
        const int16_t lo = -32768, hi = 16383; // [-2**15 , 2**14)
        const double eps1 = 1.0 / hi;
        const double eps2 = 1.0 / (-lo);
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(0, lo, hi), (double)0);

        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(1, lo, hi), (double)0.5);
        EXPECT_DOUBLE_EQ(rescale_int_to_double<int16_t>(-1, lo, hi), (double)-0.5);

        EXPECT_NEAR(rescale_int_to_double<int16_t>(hi, lo, hi), (double)0.999999999999999, eps1);
        EXPECT_NEAR(rescale_int_to_double<int16_t>(lo, lo, hi), (double)-0.99999999999999, eps2);
        }
    }
template <typename UInt, unsigned exp_bits>
UInt impl_double_to_le(double num) {
    using SInt = std::make_signed_t<UInt>;
    if (std::isinf(num) or not std::isfinite(num)) {
        throw std::invalid_argument("Argument is either infinite or NaN.");  // TODO
    }

    // TODO check T is uint16_t or uint32_t

    const unsigned type_bits = sizeof(UInt) << 3;
    static_assert(type_bits == 16 or type_bits == 32 or type_bits == 64);

    static_assert(exp_bits < type_bits);
    const unsigned mant_bits = type_bits - exp_bits;

    const int min_exp = -(1 << (exp_bits - 1));
    const int max_exp = (1 << (exp_bits - 1)) - 1;

    const SInt min_mant = SInt(-(UInt(1) << (mant_bits - 1)));
    const SInt max_mant = SInt((UInt(1) << (mant_bits - 1)) - 1);

    const UInt exp_mask = UInt((UInt(-1)) << mant_bits);
    const UInt mant_mask = UInt(~exp_mask);

    int exp;
    double mant = frexp(num, &exp);

    if (exp < min_exp or exp > max_exp) {
        throw std::domain_error("Exponent is out of range.");  // TODO
    }

    unsigned rawexp = xoz::internals::signed_cast_to_2complement<int>(exp);

    SInt scaledmant_int = rescale_double_to_int<SInt>(mant, min_mant, max_mant);
    UInt rawmant = xoz::internals::signed_cast_to_2complement<SInt>(scaledmant_int);

    if constexpr (type_bits == 16) {
        uint16_t data = 0;
        assert_write_bits_into_u16(data, rawexp, exp_mask);
        assert_write_bits_into_u16(data, rawmant, mant_mask);

        return u16_to_le(data);
    } else if (type_bits == 32) {
        uint32_t data = 0;
        assert_write_bits_into_u32(data, rawexp, exp_mask);
        assert_write_bits_into_u32(data, rawmant, mant_mask);

        return u32_to_le(data);
    } else if (type_bits == 64) {
        uint64_t data = 0;
        assert_write_bits_into_u64(data, rawexp, exp_mask);
        assert_write_bits_into_u64(data, rawmant, mant_mask);

        return u64_to_le(data);
    } else {
        return 0;  // we should never reach here
    }
}

template <typename UInt, unsigned exp_bits>
double impl_double_from_le(UInt data) {
    using SInt = std::make_signed_t<UInt>;

    const unsigned type_bits = sizeof(UInt) << 3;
    static_assert(type_bits == 16 or type_bits == 32 or type_bits == 64);

    // TODO check T is uint16_t or uint32_t

    static_assert(exp_bits < type_bits);
    const unsigned mant_bits = type_bits - exp_bits;

    const UInt exp_mask = UInt((UInt(-1)) << mant_bits);
    const UInt mant_mask = UInt(~exp_mask);

    const UInt exp_hi_bit = UInt(UInt(1) << (exp_bits - 1));
    const UInt mant_hi_bit = UInt(UInt(1) << (mant_bits - 1));

    const SInt min_mant = SInt(-(UInt(1) << (mant_bits - 1)));
    const SInt max_mant = SInt((UInt(1) << (mant_bits - 1)) - 1);

    unsigned rawexp;
    UInt rawmant;
    if constexpr (type_bits == 16) {
        data = u16_from_le(data);

        rawexp = assert_read_bits_from_u16(UInt, data, exp_mask);
        rawmant = assert_read_bits_from_u16(UInt, data, mant_mask);
    } else if (type_bits == 32) {
        data = u32_from_le(data);

        rawexp = assert_read_bits_from_u32(UInt, data, exp_mask);
        rawmant = assert_read_bits_from_u32(UInt, data, mant_mask);
    } else if (type_bits == 64) {
        data = u64_from_le(data);

        rawexp = unsigned(assert_read_bits_from_u64(UInt, data, exp_mask));
        rawmant = assert_read_bits_from_u64(UInt, data, mant_mask);
    } else {
        return 0;  // we should never reach here
    }

    // sign extension
    if (rawexp & exp_hi_bit) {
        rawexp = unsigned((unsigned(-1) << exp_bits) | rawexp);
    }

    if (rawmant & mant_hi_bit) {
        rawmant = UInt((UInt(-1) << mant_bits) | rawmant);
    }

    int exp = xoz::internals::signed_cast_from_2complement<unsigned>(rawexp);
    SInt scaledmant_int = xoz::internals::signed_cast_from_2complement<UInt>(rawmant);

    double mant = rescale_int_to_double<SInt>(scaledmant_int, min_mant, max_mant);

    return std::ldexp(mant, exp);
}

    template<typename UInt, unsigned exp_bits>
    double double_to_le_and_back(const double d) {
#if 0
        const UInt i = /*xoz::internals::*/impl_double_to_le<UInt, exp_bits>(d);
        return double(i);// /*xoz::internals::*/impl_double_from_le<UInt, exp_bits>(i);
#endif
        const UInt i = impl_double_to_le<UInt, exp_bits>(d);
        return impl_double_from_le<UInt, exp_bits>(i);
    }


    TEST(DoubleTest, DoubleToInt64) {
        constexpr unsigned exp_bits = 11;
        using UInt = uint64_t;

        // eps == 2.0 / mantisa size
        constexpr double eps = 2.0 / (UInt(1) << ((sizeof(UInt) << 3) - exp_bits));

        const double vals[] = {
            0.5,
            0.2,
            0.1,
            0.000000000001,
            0.7,
            0.999999999999,
            1.0,
            999999999999.0,
        };

        EXPECT_DOUBLE_EQ((double_to_le_and_back<UInt, exp_bits>(0.0)), (double)0.0);
        for (unsigned i = 0; i < sizeof(vals)/sizeof(vals[0]); ++i) {
            const double d = vals[i];
            EXPECT_NEAR((double_to_le_and_back<UInt, exp_bits>(d)) / d, (double)1.0, eps);
            EXPECT_NEAR((double_to_le_and_back<UInt, exp_bits>(-d)) / -d, (double)1.0, eps);
        }
    }

    TEST(DoubleTest, DoubleToInt32) {
        constexpr unsigned exp_bits = 8;
        using UInt = uint32_t;

        // eps == 2.0 / mantisa size
        constexpr double eps = 2.0 / (1 << ((sizeof(UInt) << 3) - exp_bits));

        const double vals[] = {
            0.5,
            0.2,
            0.1,
            0.000000000001,
            0.7,
            0.999999999999,
            1.0,
            999999999999.0,
        };

        EXPECT_DOUBLE_EQ((double_to_le_and_back<UInt, exp_bits>(0.0)), (double)0.0);
        for (unsigned i = 0; i < sizeof(vals)/sizeof(vals[0]); ++i) {
            const double d = vals[i];
            EXPECT_NEAR((double_to_le_and_back<UInt, exp_bits>(d)) / d, (double)1.0, eps);
            EXPECT_NEAR((double_to_le_and_back<UInt, exp_bits>(-d)) / -d, (double)1.0, eps);
        }
    }

    TEST(DoubleTest, DoubleToInt16) {
        constexpr unsigned exp_bits = 5;
        using UInt = uint16_t;

        // eps == 2.0 / mantisa size
        constexpr double eps = 2.0 / (1 << ((sizeof(UInt) << 3) - exp_bits));

        const double vals[] = {
            0.5,
            0.2,
            0.1,
            0.7,
            1.0
        };

        EXPECT_DOUBLE_EQ((double_to_le_and_back<UInt, exp_bits>(0.0)), (double)0.0);
        for (unsigned i = 0; i < sizeof(vals)/sizeof(vals[0]); ++i) {
            const double d = vals[i];
            EXPECT_NEAR((double_to_le_and_back<UInt, exp_bits>(d)) / d, (double)1.0, eps);
            EXPECT_NEAR((double_to_le_and_back<UInt, exp_bits>(-d)) / -d, (double)1.0, eps);
        }
    }

#if 0
    TEST(DoubleTest, SignificantToInt) {
        const int16_t lo = -32768, hi = 32767;
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(0, lo, hi)), (int16_t)0);

        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(0.5, lo, hi)), (int16_t)5000);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(0.5712, lo, hi)), (int16_t)5712);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(-0.5712, lo, hi)), (int16_t)-5712);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(-0.3712, lo, hi)), (int16_t)-3712);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(-0.3212, lo, hi)), (int16_t)-32120);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(-0.32768, lo, hi)), (int16_t)-32768);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(0.32768, lo, hi)), (int16_t)3277);
        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(0.32767, lo, hi)), (int16_t)32767);

        EXPECT_EQ(int16_t(xoz::internals::significant_to_int<int>(-0.32766, lo, hi)), (int16_t)-32766);
    }

    TEST(DoubleTest, SignificantFromInt) {
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(0), (double)0);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(5000), (double)0.5);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(5712), (double)0.5712);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(-5712), (double)-0.5712);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(-3712), (double)-0.3712);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(-3212), (double)-0.32120);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(-32768), (double)-0.32768);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(3276), (double)0.3276);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(32767), (double)0.32767);

        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(-32766), (double)-0.32766);
        EXPECT_DOUBLE_EQ(xoz::internals::significant_from_int<int>(-3276), (double)-0.3276);
    }

    TEST(DoubleTest, SignificantAll) {
        const int16_t lo = -32768, hi = 32767;
        for (int32_t si = lo; si <= hi; ++si) {
            double di = xoz::internals::significant_from_int<int>(uint16_t(si));
            int16_t sj = int16_t(xoz::internals::significant_to_int<int>(di, lo, hi));

            int diff = si - sj;
            if (diff < -1) {
                ::testing_xoz::zbreak();
            }

            EXPECT_LE(diff, 1);
            //EXPECT_GE(diff, -1);
        }
    }

    TEST(DoubleTest, AllGoodValues) {
        for (uint16_t i = 0; i < uint16_t(-1); ++i) {
         //   EXPECT_EQ(double_to_5u16_le(double_from_5u16_le(i)), (uint16_t)i);

            /*
            double a = double_from_5u16_le(i);
            uint16_t j = double_to_5u16_le(a);
            double b = double_from_5u16_le(j);
            */
        }
    }
#endif
#if 0
    TEST(InetChecksumTest, InvalidChecksumValue) {
        EXPECT_THAT(
            [&]() { is_inet_checksum_good(0xffff + 1); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("Checksum value is invalid, its 2 most significant bytes are non-zero.")
                    )
                )
        );

        EXPECT_THAT(
            [&]() { is_inet_checksum_good(0x80000000); },
            ThrowsMessage<std::runtime_error>(
                AllOf(
                    HasSubstr("Checksum value is invalid, its 2 most significant bytes are non-zero.")
                    )
                )
        );
    }
#endif
}
