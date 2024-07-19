#pragma once

#include <iomanip>

namespace xoz::log {
extern int __XOZ_TRACE_MASK;

#define TRACE_ON(mask)                                                 \
    do {                                                               \
        if ((mask)&xoz::log::__XOZ_TRACE_MASK) {                       \
            std::ios_base::fmtflags _io_xoz_flags = std::cerr.flags(); \
        std::cerr
#define TRACE_FLUSH                 \
    std::flush;                     \
    std::cerr.flags(_io_xoz_flags); \
    }                               \
    }                               \
    while (0)
#define TRACE_ENDL                  \
    std::endl;                      \
    std::cerr.flags(_io_xoz_flags); \
    }                               \
    }                               \
    while (0)

void set_trace_mask_from_env();
}
