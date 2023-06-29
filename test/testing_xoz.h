#pragma once

#include <string>
#include <sstream>
#include <vector>

namespace testing_xoz {
    namespace helpers {
        std::string hexdump(const std::stringstream& fp, unsigned at = 0, unsigned len = unsigned(-1));
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
    }

    void zbreak();
}
