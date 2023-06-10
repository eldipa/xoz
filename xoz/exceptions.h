#pragma once

#include <string>
#include <sstream>
#include <stdexcept>

class Repository;

struct F {
    std::stringstream ss;

    template<typename T>
    F& operator<< (const T& val) {
        ss << val;
        return *this;
    }

    std::string str() {
        return ss.str();
    }
};

class OpenXOZError : public std::exception {
    private:
        std::string msg;

    public:
        OpenXOZError(const char* fpath, const std::string& msg);
        OpenXOZError(const char* fpath, const F& msg);

        virtual const char* what() const noexcept override;
};

class InconsistentXOZ : public std::exception {
    private:
        std::string msg;

    public:
        InconsistentXOZ(const Repository& repo, const std::string& msg);
        InconsistentXOZ(const Repository& repo, const F& msg);

        virtual const char* what() const noexcept override;
};

// Error detected before writing/modifying a xoz file that if we allow
// the change would end up in an inconsistent file
class WouldEndUpInconsistentXOZ : public std::exception {
    private:
        std::string msg;

    public:
        WouldEndUpInconsistentXOZ(const std::string& msg);
        WouldEndUpInconsistentXOZ(const F& msg);

        virtual const char* what() const noexcept override;
};

