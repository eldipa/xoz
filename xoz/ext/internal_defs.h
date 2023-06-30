#pragma once

#define READ_HdrEXT_SUBALLOC_FLAG(hdr_ext)     (bool)((hdr_ext) & 0x8000)
#define WRITE_HdrEXT_SUBALLOC_FLAG(hdr_ext)    (uint16_t)((hdr_ext) | 0x8000)

#define READ_HdrEXT_INLINE_FLAG(hdr_ext)       (bool)((hdr_ext) & 0x4000)
#define WRITE_HdrEXT_INLINE_FLAG(hdr_ext)      (uint16_t)((hdr_ext) | 0x4000)

#define READ_HdrEXT_INLINE_SZ(hdr_ext)         (uint8_t)(((hdr_ext) & 0x3f00) >> 8)
#define WRITE_HdrEXT_INLINE_SZ(hdr_ext, sz)    (uint16_t)((hdr_ext) | (((sz) & 0x3f) << 8))

#define READ_HdrEXT_INLINE_LAST(hdr_ext)         (uint8_t)((hdr_ext) & 0x00ff)
#define WRITE_HdrEXT_INLINE_LAST(hdr_ext, first) (uint16_t)((hdr_ext) | ((first) & 0xff))

#define READ_HdrEXT_MORE_FLAG(hdr_ext)      (bool)((hdr_ext) & 0x0400)
#define WRITE_HdrEXT_MORE_FLAG(hdr_ext)     (uint16_t)((hdr_ext) | 0x0400)

#define READ_HdrEXT_SMALLCNT(hdr_ext)             (uint8_t)(((hdr_ext) & 0x7800) >> 11)
#define WRITE_HdrEXT_SMALLCNT(hdr_ext, smallcnt)  (uint16_t)((hdr_ext) | ((smallcnt) << 11))
#define EXT_SMALLCNT_MAX                        (uint16_t)(0x000f)

#define READ_HdrEXT_HI_BLK_NR(hdr_ext)              ((hdr_ext) & 0x03ff)
#define WRITE_HdrEXT_HI_BLK_NR(hdr_ext, hi_blk_nr)  (uint16_t)((hdr_ext) | (hi_blk_nr))

#define EXT_INLINE_SZ_MAX_u16                (uint16_t)(63)

