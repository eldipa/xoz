#pragma once

#include <string>
#include <stdexcept>

class Repository;

class OpenXOZError : public std::exception {
    private:
        std::string msg;

    public:
        OpenXOZError(const char* fpath, const char* msg);

        virtual const char* what() const noexcept override;
};

class InconsistentXOZ : public std::exception {
    private:
        std::string msg;

    public:
        InconsistentXOZ(const Repository& repo, const char* msg);

        virtual const char* what() const noexcept override;
};
