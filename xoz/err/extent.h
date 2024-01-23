#pragma once

#include <stdexcept>
#include <string>

#include "xoz/err/msg.h"

class BlockArray;
class Extent;

// An extent (blk_nr + blk_cnt) that goes (partially
// or totally) beyond the bounds of the repository and
// it is *clear* that there is a bug or a corruption in the xoz
// and not a user mistake
class ExtentOutOfBounds: public std::exception {
private:
    std::string msg;

public:
    ExtentOutOfBounds(const BlockArray& blkarr, const Extent& ext, const std::string& msg);
    ExtentOutOfBounds(const BlockArray& blkarr, const Extent& ext, const F& msg);

    const char* what() const noexcept override;
};

class ExtentOverlapError: public std::exception {
private:
    std::string msg;

public:
    ExtentOverlapError(const Extent& ref, const Extent& ext, const std::string& msg);
    ExtentOverlapError(const Extent& ref, const Extent& ext, const F& msg);

    ExtentOverlapError(const std::string& ref_name, const Extent& ref, const std::string& ext_name, const Extent& ext,
                       const std::string& msg);
    ExtentOverlapError(const std::string& ref_name, const Extent& ref, const std::string& ext_name, const Extent& ext,
                       const F& msg);

    const char* what() const noexcept override;
};
