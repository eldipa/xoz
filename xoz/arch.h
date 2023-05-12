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

#define __u32_count_leading_zeros(X) __builtin_clz((X))

inline int u16_log2_floor(uint16_t X) { return (32 - __u32_count_leading_zeros((X)) - 1); }
inline int u32_log2_floor(uint32_t X) { return (32 - __u32_count_leading_zeros((X)) - 1); }

