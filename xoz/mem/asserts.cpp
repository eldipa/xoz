#include "xoz/mem/asserts.h"

#ifndef NDEBUG
#include <cstdlib>

void xoz::internals::print_error(const char* msg, const char* cond_str, const char* file, unsigned int line,
                                 const char* func_str) {
    fprintf(stderr, "%s at line %u - %s: %s (%s)\n", file, line, func_str, msg, cond_str);
}

void xoz::internals::abort_execution() { std::abort(); }
#endif
