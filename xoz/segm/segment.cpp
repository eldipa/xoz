#include "xoz/segm/segment.h"

#include <numeric>
#include <ostream>

#include "xoz/arch.h"
#include "xoz/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/ext/internal_defs.h"

void PrintTo(const Segment& segm, std::ostream* out) {
    for (auto const& ext: segm.exts()) {
        PrintTo(ext, out);
        (*out) << " ";
    }
}

std::ostream& operator<<(std::ostream& out, const Segment& segm) {
    PrintTo(segm, &out);
    return out;
}

uint32_t Segment::calc_footprint_disk_size() const {
    const Segment& segm = *this;

    Extent prev(0, 0, false);

    uint32_t sz = 0;
    for (const auto& ext: segm.arr) {
        // Ext header, always present
        sz += sizeof(uint16_t);

        Extent::blk_distance_t dist = Extent::distance_in_blks(prev, ext);
        if (not dist.is_near) {
            // Ext low blk nr bits, always present
            // (ext is not an inline and it is not near)
            sz += sizeof(uint16_t);
        }

        // blk_cnt is present only if
        //   - OR is_suballoc (blk_cnt is a bitmap)
        //   - OR ext.blk_nr is greater than EXT_SMALLCNT_MAX
        //     (it cannot be representable by 4 bits)
        if (ext.is_suballoc() or ext.blk_cnt() > EXT_SMALLCNT_MAX or ext.blk_cnt() == 0) {
            sz += sizeof(uint16_t);
        }

        prev = ext;
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


uint32_t Segment::calc_usable_space_size(uint8_t blk_sz_order) const {
    const Segment& segm = *this;

    uint32_t sz = std::accumulate(
            segm.arr.cbegin(), segm.arr.cend(), 0,
            [&blk_sz_order](uint32_t sz, const Extent& ext) { return sz + ext.calc_usable_space_size(blk_sz_order); });

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

uint32_t Segment::estimate_on_avg_internal_frag_sz(uint8_t blk_sz_order) const {
    if (subblk_cnt() > 0) {
        return 1 << (blk_sz_order - Extent::SUBBLK_SIZE_ORDER - 1);
    } else if (full_blk_cnt() > 0) {
        return 1 << (blk_sz_order - 1);
    } else {
        return 0;
    }
}
