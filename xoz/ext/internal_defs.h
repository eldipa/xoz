#pragma once

#define READ_HiEXT_SUBALLOC_FLAG(hi_ext)     (bool)((hi_ext) & 0x8000)
#define WRITE_HiEXT_SUBALLOC_FLAG(hi_ext)    (uint16_t)((hi_ext) | 0x8000)

#define READ_HiEXT_INLINE_FLAG(hi_ext)       (bool)((hi_ext) & 0x4000)
#define WRITE_HiEXT_INLINE_FLAG(hi_ext)      (uint16_t)((hi_ext) | 0x4000)

#define READ_HiEXT_INLINE_SZ(hi_ext)         (uint8_t)(((hi_ext) & 0x3f00) >> 8)
#define WRITE_HiEXT_INLINE_SZ(hi_ext, sz)    (uint16_t)((hi_ext) | (((sz) & 0x3f) << 8))

#define READ_HiEXT_INLINE_LAST(hi_ext)         (uint8_t)((hi_ext) & 0x00ff)
#define WRITE_HiEXT_INLINE_LAST(hi_ext, first) (uint16_t)((hi_ext) | ((first) & 0xff))

#define READ_HiEXT_MORE_FLAG(hi_ext)      (bool)((hi_ext) & 0x0400)
#define WRITE_HiEXT_MORE_FLAG(hi_ext)     (uint16_t)((hi_ext) | 0x0400)

#define READ_HiEXT_SMALLCNT(hi_ext)             (uint8_t)(((hi_ext) & 0x7800) >> 11)
#define WRITE_HiEXT_SMALLCNT(hi_ext, smallcnt)  (uint16_t)((hi_ext) | ((smallcnt) << 11))
#define EXT_SMALLCNT_MAX                        (uint16_t)(0x000f)

#define READ_HiEXT_HI_BLK_NR(hi_ext)              ((hi_ext) & 0x03ff)
#define WRITE_HiEXT_HI_BLK_NR(hi_ext, hi_blk_nr)  (uint16_t)((hi_ext) | (hi_blk_nr))

#define EXT_INLINE_SZ_MAX_u16                (uint16_t)(63)

#define CHK_READ_ROOM(fp, endpos, sz)       \
do {                                        \
    if ((endpos) - (fp).tellg() < (sz)) {   \
        throw "1";                          \
    }                                       \
} while(0)

#define CHK_WRITE_ROOM(fp, endpos, sz)       \
do {                                        \
    if ((endpos) - (fp).tellp() < (sz)) {   \
        throw "1";                          \
    }                                       \
} while(0)

