#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/file/file.h"

using xoz::Descriptor;
using xoz::DescriptorSet;
using xoz::File;
using xoz::DescriptorMapping;

void stats(File& xfile) {
    // Writing a xfile to stdout will pretty print the statistics of the xoz file
    // Check the documentation in the source code of File, BlockArray and SegmentAllocator.
    std::cout << xfile << "\n";

    auto root = xfile.root();
    DescriptorSet::top_down_for_each_set(*root, [](const DescriptorSet* dset, size_t level) {
        for (size_t i = 0; i < level; i++) {
            std::cout << "  ";
        }

        std::cout << "+ (" << dset->count() << " descriptors; " << dset->count_subset() << " subsets)\n";
    });
}

void print_usage() {
    std::cerr << "Missing/Bad arguments\n";
    std::cerr << "Usage:\n";
    std::cerr << "  show stats:     xozstats <file.xoz>\n";
}

int main(int argc, char* argv[]) {
    int ret = -1;

    if (argc < 2) {
        print_usage();
        return ret;
    }

    DescriptorMapping dmap({});

    // Open a physical file and read/load the xoz file.
    //
    // If the file does not exist, it cannot be opened for read+write
    // or it contains an invalid xoz file, fail.
    File xfile(dmap, argv[1]);
    auto dset = xfile.root();

    ret = 0;
    try {
        stats(xfile);
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        ret = -3;
    }

    try {
        xfile.close();
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        ret = -4;
        xfile.panic_close();
    }
    return ret;
}
