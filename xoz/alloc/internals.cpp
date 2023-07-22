#include "xoz/alloc/internals.h"

void xoz::alloc::internals::fail_alloc_if_empty(const uint16_t cnt, const bool is_suballoc) {
    if (cnt == 0) {
        throw std::runtime_error((F() << "cannot alloc 0 " << (is_suballoc ? "subblocks" : "blocks")).str());
    }
}

void xoz::alloc::internals::fail_if_suballoc_or_zero_cnt(const Extent& ext) {
    if (ext.is_suballoc() or ext.blk_cnt() == 0) {
        throw std::runtime_error(
                (F() << "cannot dealloc " << ((ext.is_suballoc()) ? "suballoc extent" : "0 blocks")).str());
    }
}
