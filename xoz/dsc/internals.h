#pragma once

#define MASK_IS_OBJ_FLAG (uint16_t)0x8000
#define MASK_HAS_ID_FLAG (uint16_t)0x0200

#define MASK_LO_DSIZE (uint16_t)0x7c00
#define MASK_TYPE (uint16_t)0x01ff

#define MASK_HI_DSIZE (uint32_t)0x80000000
#define MASK_OBJ_ID (uint32_t)0x7fffffff

#define MASK_LARGE_FLAG (uint16_t)0x8000

#define MASK_OBJ_LO_SIZE (uint16_t)0x7fff
#define MASK_OBJ_HI_SIZE (uint16_t)0xffff
