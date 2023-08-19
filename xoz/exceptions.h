#pragma once

#include <sstream>
#include <stdexcept>
#include <string>

class Repository;
class Extent;

struct F {
    std::stringstream ss;

    template <typename T>
    F& operator<<(const T& val) {
        ss << val;
        return *this;
    }

    std::string str() { return ss.str(); }
};

class OpenXOZError: public std::exception {
private:
    std::string msg;

public:
    OpenXOZError(const char* fpath, const std::string& msg);
    OpenXOZError(const char* fpath, const F& msg);

    const char* what() const noexcept override;
};

// Error when reading a xoz file and we find inconsistencies.
class InconsistentXOZ: public std::exception {
private:
    std::string msg;

public:
    InconsistentXOZ(const Repository& repo, const std::string& msg);
    InconsistentXOZ(const Repository& repo, const F& msg);
    explicit InconsistentXOZ(const std::string& msg);
    explicit InconsistentXOZ(const F& msg);

    const char* what() const noexcept override;
};

// Error detected before writing/modifying a xoz file that if we allow
// the change would end up in an inconsistent file
class WouldEndUpInconsistentXOZ: public std::exception {
private:
    std::string msg;

public:
    explicit WouldEndUpInconsistentXOZ(const std::string& msg);
    explicit WouldEndUpInconsistentXOZ(const F& msg);

    const char* what() const noexcept override;
};
class NullBlockAccess: public std::exception {
private:
    std::string msg;

public:
    explicit NullBlockAccess(const std::string& msg);
    explicit NullBlockAccess(const F& msg);

    const char* what() const noexcept override;
};

// An extent (blk_nr + blk_cnt) that goes (partially
// or totally) beyond the bounds of the repository and
// it is *clear* that there is a bug or a corruption in the xoz
// and not a user mistake
class ExtentOutOfBounds: public std::exception {
private:
    std::string msg;

public:
    ExtentOutOfBounds(const Repository& repo, const Extent& ext, const std::string& msg);
    ExtentOutOfBounds(const Repository& repo, const Extent& ext, const F& msg);

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

class NotEnoughRoom: public std::exception {
private:
    std::string msg;

public:
    NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const std::string& msg);
    NotEnoughRoom(uint64_t requested_sz, uint64_t available_sz, const F& msg);

    const char* what() const noexcept override;
};
