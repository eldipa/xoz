#pragma once

// TODO endianness:
//  - how to handle little-endian compiled running in a big-endian environment?

#define  u8_to_le(X) (X)
#define u16_to_le(X) (X)
#define u32_to_le(X) (X)
#define u64_to_le(X) (X)

#define  u8_from_le(X) (X)
#define u16_from_le(X) (X)
#define u32_from_le(X) (X)
#define u64_from_le(X) (X)

#define __count_leading_zeros(X) __builtin_clz((X))

#define  u16_log2_floor(X) (16 - __count_leading_zeros((X)) - 1)
#define  u32_log2_floor(X) (32 - __count_leading_zeros((X)) - 1)
