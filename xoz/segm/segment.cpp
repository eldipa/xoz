#include "xoz/segm/segment.h"

#include <bit>
#include <cassert>
#include <cstring>
#include <iostream>
#include <numeric>
#include <ostream>
#include <utility>

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

void Segment::fail_if_bad_inline_sz() const {
    const Segment& segm = *this;
    size_t inline_sz = segm.raw.size();

    if (inline_sz > EXT_INLINE_SZ_MAX_u16) {
        throw WouldEndUpInconsistentXOZ(F() << "Inline data too large: it has " << inline_sz << " bytes but only up to "
                                            << EXT_INLINE_SZ_MAX_u16 << " bytes are allowed.");
    }
}


constexpr void assert_write_room_and_consume(uint64_t requested_sz, uint64_t* available_sz) {
    // hard failure as failing this is considered a bug
    assert(requested_sz <= *available_sz);
    *available_sz -= requested_sz;
}

constexpr void fail_remain_exhausted_during_partial_read(uint64_t requested_sz, uint64_t* available_sz,
                                                         uint64_t segm_sz, const char* reason) {
    // This is an error in the data during the read:
    //  - may be the caller gave us the incorrect size to read
    //  - may be the XOZ file is corrupted with an invalid size
    if (requested_sz > *available_sz) {
        throw NotEnoughRoom(requested_sz, *available_sz,
                            F() << "The read operation set an initial size of " << segm_sz
                                << " bytes but they were consumed leaving only " << *available_sz
                                << " bytes available. This is not enough to proceed "
                                << "reading (segment reading is incomplete: " << reason << ").");
    }
    *available_sz -= requested_sz;
}

void Segment::read_struct_from(const std::span<const char> dataview, const uint64_t segm_sz) {
    // Check that the segment size to read (aka remain_sz)
    // is multiple of the segment size
    // NOTE: in a future version we may accept segm_sz == (uint64_t)(-1)
    // to signal "read until the end-of-segment marker"
    uint64_t remain_sz = segm_sz;
    if (remain_sz % 2 != 0) {
        throw std::runtime_error((F() << "the size to read " << segm_sz << " must be a multiple of 2.").str());
    }

    // Check that the segment size to read (aka remain_sz)
    // is smaller than the available size in the file.
    auto available_sz = dataview.size();
    if (segm_sz > available_sz) {
        throw NotEnoughRoom(segm_sz, available_sz, F() << "Read segment structure from buffer failed.");
    }

    const char* dataptr = dataview.data();

    Extent prev(0, 0, false);
    Segment segm;

    while (remain_sz >= 2) {
        assert(remain_sz % 2 == 0);

        uint16_t hdr_ext;
        fail_remain_exhausted_during_partial_read(sizeof(hdr_ext), &remain_sz, segm_sz,
                                                  "stop before reading extent header");

        hdr_ext = read_u16_from_le(&dataptr);

        bool is_suballoc = READ_HdrEXT_SUBALLOC_FLAG(hdr_ext);
        bool is_inline = READ_HdrEXT_INLINE_FLAG(hdr_ext);
        bool is_near = READ_HdrEXT_NEAR_FLAG(hdr_ext);

        if (is_suballoc and is_inline) {
            segm.inline_present = true;

            uint16_t inline_sz = READ_HdrEXT_INLINE_SZ(hdr_ext);
            uint8_t last = READ_HdrEXT_INLINE_LAST(hdr_ext);

            segm.raw.resize(inline_sz);

            // If the size is odd, reduce it by one as the last
            // byte was already loaded from hdr_ext
            if (inline_sz % 2 == 1) {
                segm.raw[inline_sz - 1] = last;
                inline_sz -= 1;
            }

            if (inline_sz > 0) {
                fail_remain_exhausted_during_partial_read(inline_sz, &remain_sz, segm_sz,
                                                          "inline data is partially read");
                memcpy((char*)segm.raw.data(), dataptr, inline_sz);
                dataptr += inline_sz;
            }

            // inline data *is* the last element of a segment
            // regardless of the caller's provided segm_sz
            break;

        } else {
            // We cannot keep reading another extent *after* reading inline
            // data, it is not allowed by RFC-v3
            assert(not segm.inline_present);

            uint8_t smallcnt = READ_HdrEXT_SMALLCNT(hdr_ext);
            uint32_t blk_nr = 0;

            // If not a near extent, we need to read the full block number
            if (not is_near) {
                uint16_t hi_blk_nr = READ_HdrEXT_HI_BLK_NR(hdr_ext);
                uint16_t lo_blk_nr;

                fail_remain_exhausted_during_partial_read(sizeof(lo_blk_nr), &remain_sz, segm_sz,
                                                          "cannot read LSB block number");

                lo_blk_nr = read_u16_from_le(&dataptr);

                blk_nr = ((uint32_t(hi_blk_nr & 0x03ff) << 16) | lo_blk_nr);

                if (blk_nr == 0) {
                    throw InconsistentXOZ(F()
                                          << "Extent with block number 0 is unexpected "
                                          << "from composing hi_blk_nr:" << (hi_blk_nr & 0x03ff)
                                          << " (10 highest bits) and lo_blk_nr:" << lo_blk_nr << " (16 lowest bits).");
                }
            }

            uint16_t blk_cnt = 0;
            if (not is_suballoc and smallcnt) {
                blk_cnt = smallcnt;

            } else {
                if (smallcnt != 0) {
                    throw InconsistentXOZ("Extent with non-zero smallcnt block. Is inline flag missing?");
                }

                fail_remain_exhausted_during_partial_read(sizeof(blk_cnt), &remain_sz, segm_sz,
                                                          "cannot read block count");
                blk_cnt = read_u16_from_le(&dataptr);
            }

            // If it is a near extent, we know now its block count so we can
            // compute the jump/gap
            if (is_near) {
                assert(blk_nr == 0);
                bool is_backward_dir = READ_HdrEXT_BACKWARD_DIR(hdr_ext);
                uint16_t jmp_offset = READ_HdrEXT_JMP_OFFSET(hdr_ext);

                // Reference at prev extent's block number
                uint32_t ref_nr = blk_nr = prev.blk_nr();
                uint32_t prev_blk_cnt = (prev.is_suballoc() ? 1 : prev.blk_cnt());

                bool blk_nr_wraparound = false;

                if (is_backward_dir) {
                    blk_nr -= jmp_offset;
                    blk_nr -= blk_cnt;

                    if (ref_nr < blk_nr) {
                        blk_nr_wraparound = true;
                    }
                } else {
                    blk_nr += jmp_offset;
                    blk_nr += prev_blk_cnt;

                    if (ref_nr > blk_nr) {
                        blk_nr_wraparound = true;
                    }
                }

                if (blk_nr_wraparound) {
                    throw InconsistentXOZ(F() << "Near extent block number wraparound: "
                                              << "current extent offset " << jmp_offset << " and blk cnt " << blk_cnt
                                              << " in the " << (is_backward_dir ? "backward" : "forward")
                                              << " direction and previous extent at blk nr " << prev.blk_nr()
                                              << " and blk cnt " << prev_blk_cnt << ".");
                }

                if (blk_nr == 0) {
                    throw InconsistentXOZ(F() << "Extent with block number 0 is unexpected "
                                              << "for " << blk_cnt << " blocks length extent from relative offset "
                                              << jmp_offset << " in the " << (is_backward_dir ? "backward" : "forward")
                                              << " direction with respect previous blk nr " << prev.blk_nr() << " ("
                                              << prev_blk_cnt << " blocks length).");
                }
            }

            assert(blk_nr != 0);
            segm.arr.emplace_back(blk_nr, blk_cnt, is_suballoc);

            prev = segm.arr.back();
        }
    }

    // Override this segment with the loaded one
    this->arr = std::move(segm.arr);
    this->raw = std::move(segm.raw);
    this->inline_present = segm.inline_present;

    // Or consumed everything *or* we stop earlier because
    // we found an inline data
    assert(remain_sz == 0 or segm.inline_present);
}

void Segment::write_struct_into(const std::span<char> dataview) const {
    const Segment& segm = *this;
    Extent prev(0, 0, false);

    // Track how many bytes we written so far
    uint64_t remain_sz = segm.calc_footprint_disk_size();
    auto available_sz = dataview.size();
    if (remain_sz > available_sz) {
        throw NotEnoughRoom(remain_sz, available_sz, F() << "Write segment structure into buffer failed.");
    }

    char* dataptr = dataview.data();

    // We track how many extents remain_cnt in the list
    size_t remain_cnt = segm.arr.size();

    // If an inline follows the last extent, make it appear
    // and another remain_cnt item.
    if (segm.inline_present) {
        ++remain_cnt;
    }

    for (const auto& ext: segm.arr) {
        assert(remain_cnt > 0);
        assert(remain_sz >= 2);

        // The first (highest) 2 bytes
        uint16_t hdr_ext = 0;
        --remain_cnt;

        // ext.blk_nr encodes in its highest bits meta-information
        // in this case, if the block is for sub-block allaction
        bool is_suballoc = ext.is_suballoc();
        if (is_suballoc)
            hdr_ext = WRITE_HdrEXT_SUBALLOC_FLAG(hdr_ext);

        uint8_t smallcnt = 0;
        if (not is_suballoc and ext.blk_cnt() <= EXT_SMALLCNT_MAX and ext.blk_cnt() > 0) {
            smallcnt = uint8_t(ext.blk_cnt());
        }

        // This may set the smallcnt *iff* not suballoc and the
        // count can be represented in the smallcnt bitfield
        // otherwise this will set zeros in there (no-op)
        hdr_ext = WRITE_HdrEXT_SMALLCNT(hdr_ext, smallcnt);

        // Calculate the distance from the previous extent the current
        // so we can know if it is a near extent or not
        Extent::blk_distance_t dist = Extent::distance_in_blks(prev, ext);

        if (dist.is_near) {
            hdr_ext = WRITE_HdrEXT_NEAR_FLAG(hdr_ext);
            hdr_ext = WRITE_HdrEXT_JMP_OFFSET(hdr_ext, dist.blk_cnt);
            if (dist.is_backwards) {
                hdr_ext = WRITE_HdrEXT_BACKWARD_DIR(hdr_ext);
            }

            // Now hdr_ext is complete: write it to disk
            assert_write_room_and_consume(sizeof(hdr_ext), &remain_sz);
            write_u16_to_le(&dataptr, hdr_ext);

        } else {
            // Split the block number in two parts
            uint16_t hi_blk_nr = (ext.blk_nr() >> 16) & 0x3ff;  // 10 bits
            uint16_t lo_blk_nr = ext.blk_nr() & 0xffff;         // 16 bits

            // Save the highest bits in the header
            hdr_ext = WRITE_HdrEXT_HI_BLK_NR(hdr_ext, hi_blk_nr);

            // Now hdr_ext and lo_blk_nr are complete: write both to disk
            assert_write_room_and_consume(sizeof(hdr_ext) + sizeof(lo_blk_nr), &remain_sz);

            write_u16_to_le(&dataptr, hdr_ext);
            write_u16_to_le(&dataptr, lo_blk_nr);
        }

        assert(not(is_suballoc and smallcnt));
        if (is_suballoc or smallcnt == 0) {
            // write blk_cnt/bitmap
            uint16_t blk_cnt_bitmap = is_suballoc ? u16_to_le(ext.blk_bitmap()) : u16_to_le(ext.blk_cnt());
            assert_write_room_and_consume(sizeof(blk_cnt_bitmap), &remain_sz);
            write_u16_to_le(&dataptr, blk_cnt_bitmap);
        }

        prev = ext;
    }

    if (segm.inline_present) {
        assert(remain_cnt == 1);
        --remain_cnt;

        // TODO if we fail here we'll left the file corrupted:
        // the last extent has 'more' set but garbage follows.
        // We should write an empty inline-data extent at least.
        segm.fail_if_bad_inline_sz();

        uint16_t inline_sz = uint16_t(segm.raw.size());

        // The first (highest) 2 bytes
        uint16_t hdr_ext = 0;
        hdr_ext = WRITE_HdrEXT_SUBALLOC_FLAG(hdr_ext);
        hdr_ext = WRITE_HdrEXT_INLINE_FLAG(hdr_ext);
        hdr_ext = WRITE_HdrEXT_INLINE_SZ(hdr_ext, inline_sz);

        uint8_t last = 0x00;

        // If the size is odd, store the last byte in `last`
        // and subtract 1 to the size
        if (inline_sz % 2 == 1) {
            last = segm.raw[inline_sz - 1];
            inline_sz -= 1;
        }

        // the last byte of raw or 0x00 as padding
        hdr_ext = WRITE_HdrEXT_INLINE_LAST(hdr_ext, last);

        // Now hdr_ext is complete: write it to disk
        assert_write_room_and_consume(sizeof(hdr_ext) + inline_sz, &remain_sz);
        write_u16_to_le(&dataptr, hdr_ext);

        // After the uint8_t raw follows, if any
        if (inline_sz > 0) {
            memcpy(dataptr, segm.raw.data(), inline_sz);
            dataptr += inline_sz;
        }
    }

    // It must be hold remain_cnt == 0 because we counted at the begin
    // of the Segment::write how many extents+inline there were so
    // if everything worked as planned, we should have 0 elements remaining
    assert(remain_cnt == 0);

    // The same goes for the remaining size: we calculated the footprint
    // of the segment and we expect to write all of it
    assert(remain_sz == 0);
}
