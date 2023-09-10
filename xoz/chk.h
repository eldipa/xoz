#pragma once

#include <cassert>

constexpr uint8_t assert_u8(uint32_t n) {
    assert(n <= (uint8_t)-1);
    return (uint8_t)n;
}
