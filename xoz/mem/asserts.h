#pragma once

#include <bit>
#include <cassert>
#include <cstdint>

#ifndef NDEBUG
#include <cstdio>
#endif

namespace xoz {

#ifdef NDEBUG
#define xoz_internals__assert_annotated(msg, cond, file, line, func) ((void)0)
#define xoz_assert(msg, cond) ((void)0)
#else
namespace internals {
void print_error(const char* msg, const char* cond_str, const char* file, unsigned int line, const char* func_str);
void abort_execution();
}  // namespace internals

#define xoz_internals__assert_annotated(msg, cond, file, line, func)      \
    do {                                                                  \
        if (!(cond)) {                                                    \
            internals::print_error((msg), #cond, (file), (line), (func)); \
            internals::abort_execution();                                 \
        }                                                                 \
    } while (0)

/*
 * This macro checks the condition and it yields false, it will abort the
 * program printing to stderr the given message next to the file, line and
 * function where the xoz_assert was called.
 *
 * This macro is a no-op if NDEBUG is defined. Essentially, it works
 * as the good old C assert() macro.
 * */
#define xoz_assert(msg, cond) xoz_internals__assert_annotated(msg, cond, __FILE__, __LINE__, __func__)
#endif


}  // namespace xoz
