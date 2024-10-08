#include "xoz/io/iosegment.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/mem/asserts.h"
#include "xoz/mem/casts.h"
#include "xoz/segm/segment.h"

namespace {
using namespace xoz;  // NOLINT
std::vector<uint32_t> create_ext_index(const Segment& sg, [[maybe_unused]] const uint32_t sg_no_inline_sz,
                                       const uint8_t blk_sz_order) {
    uint32_t cnt = sg.ext_cnt();

    std::vector<uint32_t> begin_positions;
    begin_positions.reserve(cnt);

    uint32_t pos = 0;
    for (const Extent& ext: sg.exts()) {
        begin_positions.push_back(pos);
        pos += ext.calc_data_space_size(blk_sz_order);
    }

    assert(pos == sg_no_inline_sz);

    return begin_positions;
}
}  // namespace


namespace xoz {
IOSegment::IOSegment(BlockArray& blkarr, Segment& sg):
        IOBase(sg.calc_data_space_size()),
        blkarr(blkarr),
        sg(sg),
        sg_no_inline_sz(remain_rd() - sg.inline_data_sz()),
        begin_positions(create_ext_index(sg, sg_no_inline_sz, blkarr.blk_sz_order())) {}

uint32_t IOSegment::rw_operation(const bool is_read_op, char* data, const uint32_t data_sz) {
    uint32_t remain_sz = data_sz;
    char* dataptr = data;

    uint32_t rwptr = is_read_op ? rd : wr;
    uint32_t rw_avail_sz = is_read_op ? remain_rd() : remain_wr();

    chk_within_limits(is_read_op);
    uint32_t rw_total_sz = 0;
    while (remain_sz and rw_avail_sz) {
        const struct ext_ptr_t ptr = abs_pos_to_ext(rwptr);
        if (ptr.end) {
            break;
        }

        const uint32_t batch_sz = std::min(std::min(ptr.remain, remain_sz), rw_avail_sz);

        uint32_t n = 0;
        if (is_read_op) {
            n = blkarr.read_extent(ptr.ext, dataptr, batch_sz, ptr.offset);
        } else {
            n = blkarr.write_extent(ptr.ext, dataptr, batch_sz, ptr.offset);
        }

        remain_sz -= n;
        rw_avail_sz -= n;

        rw_total_sz += n;
        dataptr += n;
        rwptr += n;
    }
    chk_within_limits(is_read_op);

    // Note: sg_no_inline_sz is the size of the complete segment without limits (rd_end, wr_min, ...)
    // but excluding the inline space of the segment.
    // In short, we are entering the if-block only if the pointer rwptr is in the range
    // that the correspond to the inline space.
    if (remain_sz and rw_avail_sz and sg_no_inline_sz <= rwptr) {
        const uint8_t remain_inline_sz = assert_u8(rw_avail_sz);
        assert(remain_inline_sz <= sg.inline_data_sz());

        const uint8_t batch_sz = assert_u8(std::min(assert_u32(remain_inline_sz), remain_sz));
        assert(batch_sz <= sg.inline_data_sz());

        const uint32_t offset = rwptr - sg_no_inline_sz;
        assert(offset < sg.inline_data_sz());

        if (is_read_op) {
            memcpy(dataptr, sg.inline_data().data() + offset, batch_sz);
        } else {
            memcpy(sg.inline_data().data() + offset, dataptr, batch_sz);
        }

        uint32_t n = batch_sz;

        remain_sz -= n;
        rw_avail_sz -= n;

        rw_total_sz += n;
        dataptr += n;
        rwptr += n;
    }

    if (is_read_op) {
        rd = rwptr;
    } else {
        wr = rwptr;
    }

    chk_within_limits(is_read_op);
    return rw_total_sz;
}


const struct IOSegment::ext_ptr_t IOSegment::abs_pos_to_ext(const uint32_t pos) const {
    struct ext_ptr_t ptr = {.ext = Extent(0, 0, false), .offset = 0, .remain = 0, .end = true};

    if (begin_positions.size() == 0 or pos >= sg_no_inline_sz) {
        return ptr;
    }

    uint32_t ix = 0;
    for (const uint32_t p: begin_positions) {
        if (p > pos) {
            break;
        }

        ++ix;
    }

    --ix;

    ptr.ext = sg.exts()[ix];
    ptr.offset = pos - begin_positions[ix];
    ptr.remain = ptr.ext.calc_data_space_size(blkarr.blk_sz_order()) - ptr.offset;
    ptr.end = false;

    return ptr;
}

IOSegment IOSegment::dup() const {
    return *this;  // call copy constructor
}

void IOSegment::fill_c(BlockArray& blkarr, Segment& sg, const char c, const bool include_inline) {
    auto io = IOSegment(blkarr, sg);
    uint32_t sz = io.remain_wr();
    if (not include_inline) {
        sz -= sg.inline_data_sz();
        assert(sz <= io.remain_wr());
    }
    io.fill(c, sz);
}
}  // namespace xoz
