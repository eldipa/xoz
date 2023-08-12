#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "xoz/trace.h"

#include <fstream>

#define SCRATCH_HOME "./scratch/mem/"

#define NOT_MOUNTED_FILE_TOKEN "not-mounted"

class EnsureScratchMemIsMounted: public ::testing::Environment {
    public:
        void SetUp() override {
            std::fstream test(SCRATCH_HOME NOT_MOUNTED_FILE_TOKEN, std::fstream::in);
            if (!test) {
                // Token does not exist: assume that the scratch/mem/ is
                // mounted so "everything is ok"
            } else{
                // Token exists: assume that the scratch/mem/ is *not*
                // mounted so make the tests execution abort.
                FAIL()
                    << "Token file '"
                    << NOT_MOUNTED_FILE_TOKEN
                    << "' found in folder '"
                    << SCRATCH_HOME
                    << "'. Assumed that the tmpfs (memory only) file system is *not* mounted. "
                    << "Abort the tests execution.";
            }
        }

        void TearDown() override {}
        ~EnsureScratchMemIsMounted() override {}
};

int main(int argc, char** argv) {
    set_trace_mask_from_env();
    testing::InitGoogleTest(&argc, argv);
    testing::AddGlobalTestEnvironment(new EnsureScratchMemIsMounted);
    return RUN_ALL_TESTS();
}
