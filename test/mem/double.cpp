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

    template<typename UInt, unsigned exp_bits>
    double double_to_le_and_back(const double d) {
        const UInt i = xoz::internals::impl_double_to_le<UInt, exp_bits>(d);
        return xoz::internals::impl_double_from_le<UInt, exp_bits>(i);
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
}
