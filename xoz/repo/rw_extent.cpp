#include <algorithm>
#include <cstring>

#include "xoz/err/exceptions.h"
#include "xoz/repo/repository.h"

uint32_t Repository::chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start) {
    if (ext.blk_nr() == 0x0) {
        throw NullBlockAccess(F() << "The block 0x00 cannot be " << (is_read_op ? "read" : "written"));
    }

    assert(ext.blk_nr() != 0x0);
    return BlockArray::chk_extent_for_rw(is_read_op, ext, max_data_sz, start);
}

void Repository::impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    seek_read_blk(blk_nr, offset);
    fp.read(buf, exact_sz);
}

void Repository::impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    seek_write_blk(blk_nr, offset);
    fp.write(buf, exact_sz);
}
