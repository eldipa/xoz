#include "xoz/segm/segment.h"

#include <ostream>

void PrintTo(const Segment& segm, std::ostream* out) {
    for (auto const& ext: segm.exts()) {
        PrintTo(ext, out);
        (*out) << " ";
    }
}

std::ostream& operator<<(std::ostream& out, const Segment& segm) {
    PrintTo(segm, &out);
    return out;
}
