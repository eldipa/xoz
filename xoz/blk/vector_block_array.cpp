#include "xoz/blk/vector_block_array.h"

#include "xoz/blk/block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/io/iosegment.h"
#include "xoz/segm/segment.h"

std::tuple<uint32_t, uint16_t> VectorBlockArray::impl_grow_by_blocks(uint16_t ar_blk_cnt) {
    // How many bytes are those?
    uint32_t grow_sz = ar_blk_cnt << blk_sz_order();
    buf.resize(buf.size() + grow_sz);

    io.reset(new IOSpan(buf));
    return {past_end_blk_nr(), ar_blk_cnt};
}

uint32_t VectorBlockArray::impl_shrink_by_blocks(uint32_t ar_blk_cnt) {
    // How many bytes are those?
    uint32_t shrink_sz = (ar_blk_cnt << blk_sz_order());
    assert(shrink_sz <= buf.size());
    buf.resize(buf.size() - shrink_sz);

    io.reset(new IOSpan(buf));
    return ar_blk_cnt;
}

void VectorBlockArray::impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    io->seek_rd(blk2bytes(blk_nr) + offset);
    io->readall(buf, exact_sz);
}

void VectorBlockArray::impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    io->seek_wr(blk2bytes(blk_nr) + offset);
    io->writeall(buf, exact_sz);
}

uint32_t VectorBlockArray::impl_release_blocks() { return 0; }

VectorBlockArray::VectorBlockArray(uint32_t blk_sz): BlockArray() {
    if (blk_sz == 0) {
        throw "";
    }

    io.reset(new IOSpan(buf));

    if (io->remain_rd() % blk_sz != 0) {
        throw "";
    }

    initialize_block_array(blk_sz, 0, io->remain_rd() / blk_sz);
}

VectorBlockArray::~VectorBlockArray() {}
