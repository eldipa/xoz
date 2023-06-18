#pragma once

#include <string>
#include <sstream>

namespace testing_xoz {
    namespace helpers {
        std::string hexdump(const std::stringstream& fp, unsigned at = 0, unsigned len = unsigned(-1));
    }

    void zbreak();
}
