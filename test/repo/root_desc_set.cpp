#include "xoz/repo/repository.h"
#include "xoz/ext/extent.h"
#include "xoz/err/exceptions.h"
#include "xoz/dsc/default.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "test/testing_xoz.h"

#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::subvec;
using ::testing_xoz::helpers::file2mem;


#define XOZ_EXPECT_FILE_SERIALIZATION(path, at, len, data) do {           \
    EXPECT_EQ(hexdump(file2mem(path), (at), (len)), (data));              \
} while (0)

#define SCRATCH_HOME "./scratch/mem/"
#define DELETE(X) system("rm -f '" SCRATCH_HOME X "'")

namespace {
    TEST(RootDescSetTest, EmptySet) {
        const GlobalParameters gp = {
            .blk_sz = 64, // 64/16 = 4 bytes per subblock
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        DELETE("RootDescSetTestEmptySet.xoz");

        const char* fpath = SCRATCH_HOME "RootDescSetTestEmptySet.xoz";

        // First round: test that we can create an empty repository
        // with an empty set and we can save it.
        {
            Repository repo = Repository::create(fpath, true, 0, gp);

            // Get the root descriptor set
            auto dset = repo.root();

            EXPECT_EQ(dset->count(), (uint32_t)0);
            EXPECT_EQ(dset->does_require_write(), (bool)false);

            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "4000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0100 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "00c0 "                 // root segment
                    "0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }


        // Load the same repo from the disc, test that we get the same
        // information
        {
            Repository repo(fpath);

            // Get the root descriptor set
            auto dset = repo.root();

            EXPECT_EQ(dset->count(), (uint32_t)0);
            EXPECT_EQ(dset->does_require_write(), (bool)false);



            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "4000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0100 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "00c0 "                 // root segment
                    "0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }
    }


    // TODO add more tests for when the set's segment grows and it cannot be put in the header
    // TODO in a segment, if 2 consecutive extents can be merged, merge it:
    //  - given A and B, there is a distance of 0 between the extents and A.blknr < B.blknr
    //  - o both are suballoc and same block

    TEST(RootDescSetTest, SmallSet) {
        const GlobalParameters gp = {
            .blk_sz = 64, // 64/16 = 4 bytes per subblock
            .blk_sz_order = 6,
            .blk_init_cnt = 1
        };

        struct Descriptor::header_t hdr = {
            .own_edata = false,
            .type = 0xfa,

            .id = 0x80000001,

            .dsize = 0,
            .esize = 0,
            .segm = Segment::create_empty_zero_inline()
        };

        DELETE("RootDescSetTestSmallSet.xoz");

        const char* fpath = SCRATCH_HOME "RootDescSetTestSmallSet.xoz";
        uint32_t id1 = 0, id2 = 0;

        std::map<uint16_t, descriptor_create_fn> descriptors_map;
        deinitialize_descriptor_mapping();
        initialize_descriptor_mapping(descriptors_map);

        // First round: test that we can create an empty repository
        // with an empty set and we can save it.
        {
            Repository repo = Repository::create(fpath, true, 0, gp);

            // Get the root descriptor set
            auto dset = repo.root();

            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo);
            id1 = dset->add(std::move(dscptr), true);

            EXPECT_EQ(dset->count(), (uint32_t)1);
            EXPECT_EQ(dset->does_require_write(), (bool)true);

            repo.close(); // this implies dset->write_set()
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "8000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0200 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0184 00c0 00c0 "                 // root segment
                    "0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "fa02 0100 0000 "
                                   "0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );

        }


        // Load the same repo from the disk, test that we get the same
        // information
        {
            Repository repo(fpath);

            // Get the root descriptor set
            auto dset = repo.root();

            EXPECT_EQ(dset->count(), (uint32_t)1);
            EXPECT_EQ(dset->does_require_write(), (bool)false);

            // Test that the descriptor still lives in the set
            dset->get<DefaultDescriptor>(id1);

            std::cout << dset->segment() << std::endl;

            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "8000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0200 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0184 00c0 00c0 "                 // root segment
                    "0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "fa02 0100 0000 "
                                   "0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }


        // Load the same repo, add a new descriptor to the set making it a little larger
        // (but still quite small)
        {
            Repository repo(fpath);

            // Get the root descriptor set
            auto dset = repo.root();

            auto dscptr = std::make_unique<DefaultDescriptor>(hdr, repo);
            id2 = dset->add(std::move(dscptr), true);

            dset->write_set();
            std::cout << dset->segment() << std::endl;
            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "8000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0200 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0184 00c0 0080 0100 0020 00c0 "                 // root segment
                    "0000 0000 "
                    //---------------------------- 64 bytes block
                    "fa02 0100 0000 "
                                   "fa02 0200 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }

        {
            Repository repo(fpath);

            // Get the root descriptor set
            auto dset = repo.root();

            auto dsc = dset->get<DefaultDescriptor>(id2);

            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "8000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0200 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0184 00c0 0080 0100 0020 00c0 "                 // root segment
                    "0000 0000 "
                    //---------------------------- 64 bytes block
                    "fa02 0100 0000 "
                                   "fa02 0200 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }

        // Load the same repo, but remove all its descriptors (leave an empty set)
        {
            Repository repo(fpath);

            // Get the root descriptor set
            auto dset = repo.root();

            dset->erase(id1);
            dset->erase(id2);

            // These two are required to make the segment for the root dset smaller
            // (back to "00c0")
            dset->write_set();
            dset->release_free_space();

            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "8000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0200 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "00c0 "                 // root segment
                    "0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    "0000 0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }

        {
            Repository repo(fpath);

            // Get the root descriptor set
            auto dset = repo.root();

            // This is necessary to shrink the file
            repo.allocator().release();

            repo.close();
            XOZ_EXPECT_FILE_SERIALIZATION(fpath, 0, -1,
                    // header
                    "584f 5a00 "            // magic XOZ\0
                    "4000 0000 0000 0000 "  // repo_sz
                    "0400 0000 0000 0000 "  // trailer_sz
                    "0100 0000 "            // blk_total_cnt
                    "0100 0000 "            // blk_init_cnt
                    "06"                    // blk_sz_order
                    "00 0000 0000 0000 0000 0000 0000 0000 0000 0000 "
                    "00c0 "                 // root segment
                    "0000 0000 0000 0000 0000 0000 0000 "
                    //---------------------------- 64 bytes block
                    "454f 4600"             // trailer
                    );
        }


    }
}
