#include "xoz/mem/asserts.h"

#ifndef NDEBUG
#include <cstdlib>
#include <filesystem>

void xoz::internals::print_error(const char* msg, const char* cond_str, const char* file, unsigned int line,
                                 const char* func_str) {
    auto fname = std::filesystem::path(file).filename();
    fprintf(stderr, "%s at line %u - %s: (%s) failed\n-> %s\n", fname.c_str(), line, func_str, cond_str, msg);
}

void xoz::internals::abort_execution() { std::abort(); }
#endif
