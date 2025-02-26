#include "xoz/mem/double.h"

#include <cmath>

#include "xoz/mem/endianness.h"
#include "xoz/mem/integer_ops.h"

namespace xoz {
uint16_t half_float_to_le(double num) { return internals::impl_double_to_le<uint16_t, 5>(num); }

double half_float_from_le(uint16_t num) { return internals::impl_double_from_le<uint16_t, 5>(num); }

uint32_t single_float_to_le(double num) { return internals::impl_double_to_le<uint32_t, 8>(num); }

double single_float_from_le(uint32_t num) { return internals::impl_double_from_le<uint32_t, 8>(num); }

/*
uint64_t double_float_to_le(double num) {
    return internals::impl_double_to_le<uint64_t, 11>(num);
}

double double_float_from_le(uint64_t num) {
    return internals::impl_double_from_le<uint64_t, 11>(num);
}
*/

}  // namespace xoz
