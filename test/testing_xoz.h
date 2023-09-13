#pragma once

#include <string>
#include <sstream>
#include <vector>
#include <functional>
#include <memory>

namespace testing_xoz {
    namespace helpers {
        std::string hexdump(const std::stringstream& fp, unsigned at = 0, unsigned len = unsigned(-1));
        std::string hexdump(const std::vector<char>& buf, unsigned at = 0, unsigned len = unsigned(-1));
        bool are_all_zeros(const std::vector<char>& buf, unsigned at = 0, unsigned len = unsigned(-1));
        bool are_all_zeros(const std::stringstream& fp, unsigned at = 0, unsigned len = unsigned(-1));

        template<class T>
        std::vector<T> subvec(const std::vector<T>& vec, signed begin, signed end = 0) {
            auto itbegin = vec.begin();
            auto itend = vec.end();
            if (end < 0) {
                itend += end;
            } else if (end > 0) {
                itend = itbegin + end;
            } else {
                // end == 0, itend == vec.end()
            }

            if (begin < 0) {
                itbegin = itend + begin;
            } else if (begin > 0) {
                itbegin += begin;
            } else {
                // begin == 0, itbegin == vec.begin()
            }

            return std::vector<T>(itbegin, itend);
        }

        const std::stringstream file2mem(const char* path);

        // Credit:
        // https://github.com/google/googletest/issues/4073#issuecomment-1384645201
        template<class Function>
        std::function<void()> ensure_called_once(Function function)
        {
            auto shared_exception_ptr = std::make_shared<std::exception_ptr>();
            auto was_called = std::make_shared<bool>(false);
            return [shared_exception_ptr, was_called, function]() {
                if (*shared_exception_ptr) {
                    std::rethrow_exception(*shared_exception_ptr);
                }
                if (*was_called) {
                    return;
                }
                *was_called = true;
                try {
                    std::invoke(function);
                } catch (...) {
                    *shared_exception_ptr = std::current_exception();
                    std::rethrow_exception(*shared_exception_ptr);
                }
            };
        }
    }

    void zbreak();
}
