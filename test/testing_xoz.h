#pragma once

#include <string>
#include <sstream>
#include <vector>

namespace testing_xoz {
    namespace helpers {
        std::string hexdump(const std::stringstream& fp, unsigned at = 0, unsigned len = unsigned(-1));

        template<class T>
        std::vector<T> subvec(const std::vector<T>& vec, signed begin, signed end =0) {
            if (end == 0) {
                end = (signed)vec.size();
                if (end < 0) {
                    throw "";
                }
            }

            if (end < 0) {
                end = (signed)vec.size() + end;
            }

            if (begin < 0) {
                begin = (signed)vec.size() + begin;
            }

            return std::vector<T>(&vec.at(begin), &vec.at(end));
        }
    }

    void zbreak();
}
