#pragma once

#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>

#include "xoz/err/msg.h"

namespace xoz::log {

/*
 * hex() is a ostream manipulator that receives an unsigned integers and formats
 * it in hexadecimal.
 * This works also for xoz' F() objects.
 *
 * Under the hood it creates an internal object of type _hex_type<UInt> that
 * forces the compiler to call an overload version of ostream<<  that makes the magic.
 * */

template <typename UInt>
struct _hex_type {
    UInt num;
};

template <typename UInt>
[[nodiscard]] inline typename std::enable_if_t<std::is_integral_v<UInt> and std::is_unsigned_v<UInt>, std::ostream&>
        operator<<(std::ostream& out, const _hex_type<UInt> t) {
    constexpr int w = sizeof(UInt) << 1;

    std::ios_base::fmtflags ioflags = out.flags();
    out << "0x" << std::setfill('0') << std::setw(w) << std::hex << t.num;
    out.flags(ioflags);
    return out;
}

template <typename UInt>
[[nodiscard]] inline typename std::enable_if_t<std::is_integral_v<UInt> and std::is_unsigned_v<UInt>, F&> operator<<(
        F& out, const _hex_type<UInt> t) {
    constexpr int w = sizeof(UInt) << 1;

    std::ios_base::fmtflags ioflags = out.ss.flags();
    out << "0x" << std::setfill('0') << std::setw(w) << std::hex << t.num;
    out.ss.flags(ioflags);
    return out;
}


template <typename UInt>
[[nodiscard]] constexpr inline
        typename std::enable_if_t<std::is_integral_v<UInt> and std::is_unsigned_v<UInt>, _hex_type<UInt>>
        hex(UInt num) {
    return _hex_type<UInt>{num};
}


}  // namespace xoz::log
