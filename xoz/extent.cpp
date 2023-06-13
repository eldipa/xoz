#include "extent.h"
#include "arch.h"
#include "exceptions.h"

#include <bit>
#include <iostream>
#include <cassert>

#define READ_HiEXT_SUBALLOC_FLAG(hi_ext)     (bool)((hi_ext) & 0x8000)
#define WRITE_HiEXT_SUBALLOC_FLAG(hi_ext)    (uint16_t)((hi_ext) | 0x8000)

#define READ_HiEXT_INLINE_FLAG(hi_ext)       (bool)((hi_ext) & 0x4000)
#define WRITE_HiEXT_INLINE_FLAG(hi_ext)      (uint16_t)((hi_ext) | 0x4000)

#define READ_HiEXT_INLINE_SZ(hi_ext)         (((hi_ext) & 0x00ff) << 1)
#define WRITE_HiEXT_INLINE_SZ(hi_ext, sz)    (uint16_t)((hi_ext) | ((sz) >> 1))

#define READ_HiEXT_INLINE_FLAGS(hi_ext)         (uint8_t)(((hi_ext) & 0x3f00) >> 8)
#define WRITE_HiEXT_INLINE_FLAGS(hi_ext, flags) (uint16_t)((hi_ext) | ((flags) << 8))

#define READ_HiEXT_MORE_FLAG(hi_ext)      (bool)((hi_ext) & 0x0400)
#define WRITE_HiEXT_MORE_FLAG(hi_ext)     (uint16_t)((hi_ext) | 0x0400)

#define READ_HiEXT_SMALLCNT(hi_ext)             (uint8_t)(((hi_ext) & 0x7800) >> 11)
#define WRITE_HiEXT_SMALLCNT(hi_ext, smallcnt)  (uint16_t)((hi_ext) | ((smallcnt) << 11))
#define EXT_SMALLCNT_MAX                        (uint16_t)(0x000f)

#define READ_HiEXT_HI_BLK_NR(hi_ext)              ((hi_ext) & 0x03ff)
#define WRITE_HiEXT_HI_BLK_NR(hi_ext, hi_blk_nr)  (uint16_t)((hi_ext) | (hi_blk_nr))

#define EXT_INLINE_SZ_MAX_u16                (uint16_t)(0xff)

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

// An ExtentGroup is "valid" empty if and only if it has no extent
// and it as an inline of 0 bytes.
// Otherwise, it must have or at least 1 extent or inline data.
void fail_if_invalid_empty(const ExtentGroup& exts) {
    if (exts.arr.size() == 0 and not exts.inline_present)
        throw WouldEndUpInconsistentXOZ("ExtentGroup is literally empty: no extents and no inline data. This is not allowed, an valid empty ExtentGroup can be made by a zero inline data.");
}

void fail_if_bad_inline_sz(const ExtentGroup& exts) {
    size_t inline_sz = exts.raw.size();
    if (inline_sz % 2 != 0) {
        throw WouldEndUpInconsistentXOZ(F()
                << "Inline data size must be a multiple of 2 but it has "
                << inline_sz
                << " bytes."
                );
    }

    if ((inline_sz >> 1) > EXT_INLINE_SZ_MAX_u16) {
        throw WouldEndUpInconsistentXOZ(F()
                << "Inline data too large: it has "
                << inline_sz
                << " bytes but only up to "
                << (EXT_INLINE_SZ_MAX_u16 << 1)
                << " bytes are allowed."
                );
    }
}

ExtentGroup load_ext_arr(std::istream& fp, uint64_t endpos) {
    assert(std::streampos(endpos) >= fp.tellg());
    bool is_more = true;

    ExtentGroup exts;

    while (is_more) {
        is_more = false;

        uint16_t hi_ext;
        CHK_READ_ROOM(fp, endpos, sizeof(hi_ext));

        fp.read((char*)&hi_ext, sizeof(hi_ext));
        hi_ext = u16_from_le(hi_ext);

        bool is_suballoc = READ_HiEXT_SUBALLOC_FLAG(hi_ext);
        bool is_inline = READ_HiEXT_INLINE_FLAG(hi_ext);

        if (is_suballoc and is_inline) {
            exts.inline_present = true;

            uint16_t inline_sz = READ_HiEXT_INLINE_SZ(hi_ext);
            exts.inline_flags = READ_HiEXT_INLINE_FLAGS(hi_ext);

            CHK_READ_ROOM(fp, endpos, inline_sz);
            exts.raw.resize(inline_sz);

            fp.read((char*)exts.raw.data(), inline_sz);
        } else {
            is_more = READ_HiEXT_MORE_FLAG(hi_ext);

            uint8_t smallcnt = READ_HiEXT_SMALLCNT(hi_ext);
            uint16_t hi_blk_nr = READ_HiEXT_HI_BLK_NR(hi_ext);

            uint16_t lo_blk_nr;
            CHK_READ_ROOM(fp, endpos, sizeof(lo_blk_nr));

            fp.read((char*)&lo_blk_nr, sizeof(lo_blk_nr));
            lo_blk_nr = u16_from_le(lo_blk_nr);

            uint16_t blk_cnt = 0;
            if (not is_suballoc and smallcnt) {
                blk_cnt = smallcnt;

            } else {
                assert (smallcnt == 0);

                CHK_READ_ROOM(fp, endpos, sizeof(blk_cnt));
                fp.read((char*)&blk_cnt, sizeof(blk_cnt));
                blk_cnt = u16_from_le(blk_cnt);
            }

            exts.arr.emplace_back(
                hi_blk_nr,
                lo_blk_nr,
                blk_cnt,
                is_suballoc
            );
        }
    }

    fail_if_invalid_empty(exts);
    return exts;
}

uint32_t calc_size_in_disk(const ExtentGroup& exts) {
    fail_if_invalid_empty(exts);
    uint32_t sz = 0;
    for (const auto& ext : exts.arr) {
        // Ext header, always present
        sz += sizeof(uint16_t);

        // Ext low blk nr bits, always present
        // (ext is not an inline)
        sz += sizeof(uint16_t);

        // blk_cnt is present only if
        //   - OR is_suballoc (blk_cnt is a bitmap)
        //   - OR ext.blk_nr is greater than EXT_SMALLCNT_MAX
        //     (it cannot be representable by 4 bits)
        if (ext.is_suballoc() or ext.blk_cnt() > EXT_SMALLCNT_MAX or ext.blk_cnt() == 0) {
            sz += sizeof(uint16_t);
        }
    }

    if (exts.inline_present) {
        // Ext header, always present
        sz += sizeof(uint16_t);

        fail_if_bad_inline_sz(exts);

        // No blk_nr or blk_cnt are present in an inline
        // After the header the uint8_t raw follows
        //
        // Note: the cast from size_t to uint16_t should be
        // safe because if the size of the raw cannot be represented
        // by uint16_t, fail_if_bad_inline_sz() should had failed
        // before
        uint16_t inline_sz = uint16_t(exts.raw.size());
        sz += inline_sz;
    }

    return sz;
}

uint32_t calc_allocated_size(const ExtentGroup& exts, uint8_t blk_sz_order) {
    fail_if_invalid_empty(exts);
    uint32_t sz = 0;
    for (const auto& ext : exts.arr) {
        if (ext.is_suballoc()) {
            sz += std::popcount(ext.blk_cnt()) << (blk_sz_order - 4);
        } else {
            sz += ext.blk_cnt() << blk_sz_order;
        }
    }

    if (exts.inline_present) {
        fail_if_bad_inline_sz(exts);

        // Note: the cast from size_t to uint16_t should be
        // safe because if the size of the raw cannot be represented
        // by uint16_t, fail_if_bad_inline_sz() should had failed
        // before
        uint16_t inline_sz = uint16_t(exts.raw.size());
        sz += inline_sz;
    }

    return sz;
}


void write_ext_arr(std::ostream& fp, uint64_t endpos, const ExtentGroup& exts) {
    assert(std::streampos(endpos) >= fp.tellp());
    fail_if_invalid_empty(exts);

    // All the extent except the last one will have the 'more' bit set
    // We track how many extents remain in the list to know when
    // and when not we have to set the 'more' bit
    size_t remain = exts.arr.size();

    // If an inline follows the last extent, make it appear
    // and another remain item.
    if (exts.inline_present) {
        ++remain;
    }

    for (const auto& ext : exts.arr) {
        assert (remain > 0);

        // The first (highest) 2 bytes
        uint16_t hi_ext = 0;

        bool is_more = (remain > 1);
        --remain;

        // Save the 'more' bit
        if (is_more)
            hi_ext = WRITE_HiEXT_MORE_FLAG(hi_ext);

        // ext.blk_nr encodes in its highest bits meta-information
        // in this case, if the block is for sub-block allaction
        bool is_suballoc = ext.is_suballoc();
        if (is_suballoc)
            hi_ext = WRITE_HiEXT_SUBALLOC_FLAG(hi_ext);

        uint8_t smallcnt = 0;
        if (not is_suballoc and ext.blk_cnt() <= EXT_SMALLCNT_MAX and ext.blk_cnt() > 0) {
            smallcnt = uint8_t(ext.blk_cnt());
        }

        // This may set the smallcnt *iff* not suballoc and the
        // count can be represented in the smallcnt bitfield
        // otherwise this will set zeros in there (no-op)
        hi_ext = WRITE_HiEXT_SMALLCNT(hi_ext, smallcnt);

        // Split the block number in two parts
        uint16_t hi_blk_nr = ext.hi_blk_nr();
        uint16_t lo_blk_nr = ext.lo_blk_nr();

        // Save the highest bits
        hi_ext = WRITE_HiEXT_HI_BLK_NR(hi_ext, hi_blk_nr);

        // Now hi_ext and lo_blk_nr are complete: write both to disk
        CHK_WRITE_ROOM(fp, endpos, sizeof(hi_ext) + sizeof(lo_blk_nr));

        hi_ext = u16_to_le(hi_ext);
        fp.write((char*)&hi_ext, sizeof(hi_ext));

        lo_blk_nr = u16_to_le(lo_blk_nr);
        fp.write((char*)&lo_blk_nr, sizeof(lo_blk_nr));

        assert (not (is_suballoc and smallcnt));
        if (is_suballoc or smallcnt == 0) {
            // write blk_cnt/bitmap
            uint16_t blk_cnt = u16_to_le(ext.blk_cnt());
            CHK_WRITE_ROOM(fp, endpos, sizeof(blk_cnt));
            fp.write((char*)&blk_cnt, sizeof(blk_cnt));
        }
    }

    if (exts.inline_present) {
        assert (remain == 1);
        --remain;

        // TODO if we fail here we'll left the file corrupted:
        // the last extent has 'more' set but garbage follows.
        // We should write an empty inline-data extent at least.
        fail_if_bad_inline_sz(exts);

        uint16_t inline_sz = uint16_t(exts.raw.size());

        // The first (highest) 2 bytes
        uint16_t hi_ext = 0;
        hi_ext = WRITE_HiEXT_SUBALLOC_FLAG(hi_ext);
        hi_ext = WRITE_HiEXT_INLINE_FLAG(hi_ext);
        hi_ext = WRITE_HiEXT_INLINE_FLAGS(hi_ext, exts.inline_flags);
        hi_ext = WRITE_HiEXT_INLINE_SZ(hi_ext, inline_sz);


        // TODO bits: backward/forward compat
        //

        // Now hi_ext is complete: write it to disk
        CHK_WRITE_ROOM(fp, endpos, sizeof(hi_ext) + inline_sz);
        hi_ext = u16_to_le(hi_ext);
        fp.write((char*)&hi_ext, sizeof(hi_ext));

        // After the uint8_t raw follows
        fp.write((char*)exts.raw.data(), inline_sz);
    }

    assert (remain == 0);
}
