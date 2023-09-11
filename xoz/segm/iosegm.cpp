#include "xoz/segm/iosegm.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "xoz/chk.h"
#include "xoz/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/repo/repo.h"
#include "xoz/segm/segment.h"

namespace {
std::vector<uint32_t> create_ext_index(const Segment& sg, const uint32_t sg_no_inline_sz, const uint8_t blk_sz_order) {
    uint32_t cnt = sg.ext_cnt();

    std::vector<uint32_t> begin_positions;
    begin_positions.reserve(cnt);

    uint32_t pos = 0;
    for (const Extent& ext: sg.exts()) {
        begin_positions.push_back(pos);
        pos += ext.calc_usable_space_size(blk_sz_order);
    }

    assert(pos == sg_no_inline_sz);

    return begin_positions;
}
}  // namespace


IOSegment::IOSegment(Repository& repo, const Segment& sg):
        repo(repo),
        sg(sg),
        sg_sz(sg.calc_usable_space_size(repo.blk_sz_order())),
        sg_no_inline_sz(sg_sz - sg.inline_data_sz()),
        begin_positions(create_ext_index(sg, sg_no_inline_sz, repo.blk_sz_order())),
        rd(0),
        wr(0) {}

void IOSegment::rw_operation_exact_sz(const bool is_read_op, char* data, const uint32_t exact_sz) {
    const uint32_t remain_sz = is_read_op ? remain_rd() : remain_wr();
    if (remain_sz < exact_sz) {
        throw NotEnoughRoom(exact_sz, remain_sz,
                            F() << (is_read_op ? "Read " : "Write ") << "exact-byte-count operation at position "
                                << (is_read_op ? rd : wr) << " failed; detected before the "
                                << (is_read_op ? "read." : "write."));
    }

    const uint32_t rw_total_sz = rw_operation(is_read_op, data, exact_sz);
    if (rw_total_sz != exact_sz) {
        throw UnexpectedShorten(exact_sz, remain_sz, rw_total_sz,
                                F() << (is_read_op ? "Read " : "Write ")
                                    << "exact-byte-count operation failed due a short "
                                    << (is_read_op ? "read " : "write ") << "(pointer left at position "
                                    << (is_read_op ? rd : wr) << " ).");
    }
}

uint32_t IOSegment::rw_operation(const bool is_read_op, char* data, const uint32_t max_data_sz) {
    uint32_t remain_sz = max_data_sz;
    char* dataptr = data;

    uint32_t rwptr = is_read_op ? rd : wr;

    uint32_t rw_total_sz = 0;
    while (remain_sz) {
        const struct ext_ptr_t ptr = abs_pos_to_ext(rwptr);
        if (ptr.ext.is_null()) {
            break;
        }

        const uint32_t batch_sz = std::min(ptr.remain, remain_sz);

        uint32_t n = 0;
        if (is_read_op) {
            n = repo.read_extent(ptr.ext, dataptr, batch_sz, ptr.offset);
        } else {
            n = repo.write_extent(ptr.ext, dataptr, batch_sz, ptr.offset);
        }

        remain_sz -= n;

        rw_total_sz += n;
        dataptr += n;
        rwptr += n;
    }

    if (remain_sz and sg_no_inline_sz <= rwptr and rwptr < sg_sz) {
        const uint8_t remain_inline_sz = assert_u8(sg_sz - rwptr);
        assert(remain_inline_sz <= sg.inline_data_sz());

        const uint8_t batch_sz = assert_u8(std::min((uint32_t)remain_inline_sz, remain_sz));
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

        rw_total_sz += n;
        dataptr += n;
        rwptr += n;
    }

    if (is_read_op) {
        rd = rwptr;
    } else {
        wr = rwptr;
    }

    return rw_total_sz;
}


const struct IOSegment::ext_ptr_t IOSegment::abs_pos_to_ext(const uint32_t pos) const {
    struct ext_ptr_t ptr = {.ext = Extent::NullExtent(), .offset = 0, .remain = 0};

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
    ptr.remain = ptr.ext.calc_usable_space_size(repo.blk_sz_order()) - ptr.offset;

    return ptr;
}
