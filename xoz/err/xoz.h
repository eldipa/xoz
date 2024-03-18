#pragma once

#include <stdexcept>
#include <string>

#include "xoz/err/msg.h"

class Repository;

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
