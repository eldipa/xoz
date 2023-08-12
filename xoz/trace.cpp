#include "xoz/trace.h"

#include <cstdlib>

// Do not modify this unless via calling set_trace_mask_from_env()
int __XOZ_TRACE_MASK = 0;


// You must call this as soon as main() starts and call it
// only once
void set_trace_mask_from_env() {
    const char* valstr = std::getenv("XOZ_TRACE");
    if (valstr) {
        __XOZ_TRACE_MASK = std::atoi(valstr);
    }
}
