#pragma once

#define MASK_OWN_EDATA_FLAG (uint16_t)0x8000
#define MASK_HAS_ID_FLAG (uint16_t)0x0200

#define MASK_LO_DSIZE (uint16_t)0x7c00
#define MASK_TYPE (uint16_t)0x01ff

#define MASK_HI_DSIZE (uint32_t)0x80000000
#define MASK_ID (uint32_t)0x7fffffff

#define MASK_LARGE_FLAG (uint16_t)0x8000

#define MASK_LO_ESIZE (uint16_t)0x7fff
#define MASK_HI_ESIZE (uint16_t)0xffff

#define ALTERNATIVE_TYPE_VAL (uint16_t)0x1ff
