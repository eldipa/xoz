#include "xoz/ext/extent.h"
#include "xoz/ext/segment.h"

#include "xoz/ext/internal_defs.h"
#include "xoz/arch.h"

uint32_t Segment::calc_footprint_disk_size() const {
    const Segment& segm = *this;

    segm.fail_if_invalid_empty();
    uint32_t sz = 0;
    for (const auto& ext : segm.arr) {
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

    if (segm.inline_present) {
        // Ext header, always present
        sz += sizeof(uint16_t);

        segm.fail_if_bad_inline_sz();

        // No blk_nr or blk_cnt are present in an inline
        // After the header the uint8_t raw follows
        //
        // Note: the cast from size_t to uint16_t should be
        // safe because if the size of the raw cannot be represented
        // by uint16_t, fail_if_bad_inline_sz() should had failed
        // before
        uint16_t inline_sz = uint16_t(segm.raw.size());

        // If size is odd, raw's last byte was saved in the ext header
        // so the remaining data is size-1
        if (inline_sz % 2 == 1) {
            inline_sz -= 1;
        }

        sz += inline_sz;
    }

    return sz;
}

uint32_t Extent::calc_usable_space_size(uint8_t blk_sz_order) const {
    const Extent& ext = *this;
    if (ext.is_unallocated()) {
        return 0;
    }

    if (ext.is_suballoc()) {
        return std::popcount(ext.blk_cnt()) << (blk_sz_order - 4);
    } else {
        return ext.blk_cnt() << blk_sz_order;
    }
}

uint32_t Segment::calc_usable_space_size(uint8_t blk_sz_order) const {
    const Segment& segm = *this;

    segm.fail_if_invalid_empty();
    uint32_t sz = 0;
    for (const auto& ext : segm.arr) {
        sz += ext.calc_usable_space_size(blk_sz_order);
    }

    if (segm.inline_present) {
        segm.fail_if_bad_inline_sz();

        // Note: the cast from size_t to uint16_t should be
        // safe because if the size of the raw cannot be represented
        // by uint16_t, fail_if_bad_inline_sz() should had failed
        // before
        uint16_t inline_sz = uint16_t(segm.raw.size());

        // Note: calc_usable_space_size means how many bytes are allocated
        // for user data so we register all the inline data as such
        // (not matter if the size is an even or an odd number)
        sz += inline_sz;
    }

    return sz;
}


