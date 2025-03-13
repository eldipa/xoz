#include "xoz/ext/extent.h"
#include "xoz/segm/segment.h"
#include "xoz/io/iospan.h"
#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/descriptor_set.h"
#include "test/plain.h"
#include "xoz/err/exceptions.h"
#include "xoz/file/runtime_context.h"
#include "xoz/blk/vector_block_array.h"
#include "xoz/mem/inet_checksum.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "test/testing_xoz.h"


#include <numeric>

using ::testing::HasSubstr;
using ::testing::ThrowsMessage;
using ::testing::AllOf;

using ::testing_xoz::PlainDescriptor;
using ::testing_xoz::PlainWithContentDescriptor;

using ::testing_xoz::helpers::hexdump;
using ::testing_xoz::helpers::are_all_zeros;
using ::testing_xoz::helpers::ensure_called_once;

using namespace ::xoz;
using namespace ::xoz::alloc::internals;

namespace {
const size_t FP_SZ = 224;
}

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, disk_sz, idata_sz, cdata_sz, obj_data_sz) do {      \
    EXPECT_EQ((dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ((dsc).calc_internal_data_space_size(), (unsigned)(idata_sz));                                  \
    EXPECT_EQ((dsc).calc_content_space_size(), (unsigned)(cdata_sz));      \
    EXPECT_EQ((dsc).get_hdr_csize(), (unsigned)(obj_data_sz));       \
} while (0)

// Check that the serialization of the obj in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION_v2(fp, dsc, data) do {                                 \
    EXPECT_EQ(hexdump((fp), 0, (dsc).calc_struct_footprint_size()), (data));         \
} while (0)

// Calc checksum over the fp (bytes) and expect to be the same as the descriptor's checksum
// Note: this requires a load_struct_from/write_struct_into call before to make
// the descriptor's checksum updated
#define XOZ_EXPECT_CHECKSUM(fp, dsc) do {    \
    EXPECT_EQ(inet_checksum((uint8_t*)(fp).data(), (dsc).calc_struct_footprint_size()), (dsc).checksum); \
} while (0)

#define XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc, rctx, cblkarr) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    uint32_t sz1 = (dsc).calc_struct_footprint_size();                   \
    auto d1 = hexdump((fp), 0, sz1);                                      \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), (rctx), (cblkarr));   \
    checksum2 = dsc2_ptr->checksum;                                      \
    dsc2_ptr->checksum = 0;                                              \
                                                                         \
    uint32_t sz2 = dsc2_ptr->calc_struct_footprint_size();               \
    EXPECT_EQ(sz1, sz2);                                                 \
                                                                         \
    dsc2_ptr->write_struct_into(IOSpan(buf2), (rctx));                   \
    checksum3 = dsc2_ptr->checksum;                                      \
    auto d2 = hexdump(buf2, 0, sz1);                                      \
                                                                         \
    EXPECT_EQ(d1, d2);                                                   \
    EXPECT_EQ(checksum2, checksum3);                                     \
} while (0)


namespace {

    class FooV1 : public Descriptor {
    public:
        FooV1(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr) : Descriptor(hdr, cblkarr), content_v1_size(0) {}

        static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                                  [[maybe_unused]] ::xoz::RuntimeContext& rctx) {
            return std::make_unique<FooV1>(hdr, cblkarr);
        }

    public:
        void set_content_v1(const std::vector<char>& data) {
            if (not does_present_csize_fit(data.size())) {
                throw "";
            }

            content_v1_size = assert_u32(data.size());
            resize_content(content_v1_size);

            auto io = get_content_io();
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v1() {
            std::vector<char> data;
            auto io = get_content_io();

            // For V1, the entire content *is* content_v1
            io.readall(data);

            return data;
        }

        void del_content_v1() {
            resize_content(0);
            content_v1_size = 0;
            notify_descriptor_changed();
        }

    protected:
        void read_struct_specifics_from(IOBase& io) override {
            content_v1_size = io.read_u32_from_le();
        }

        void write_struct_specifics_into(IOBase& io) override {
            io.write_u32_to_le(content_v1_size);
        }

        void update_sizes(uint64_t& isize, uint64_t& csize) override {
            isize = sizeof(content_v1_size);
            csize = content_v1_size;
        }

    private:
        uint32_t content_v1_size;
    };

    class FooV2 : public Descriptor {
    public:
        FooV2(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr) : Descriptor(hdr, cblkarr), content_v1_size(0), content_v2_size(0) {}

        static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                                  [[maybe_unused]] ::xoz::RuntimeContext& rctx) {
            return std::make_unique<FooV2>(hdr, cblkarr);
        }

    public:
        void set_content_v1(const std::vector<char>& data) {
            if (not does_present_csize_fit(data.size() + content_v2_size)) {
                throw "";
            }

            content_v1_size = assert_u32(data.size());
            resize_content(content_v1_size + content_v2_size);

            auto io = get_content_io();
            io.limit_wr(0, content_v1_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v1() {
            std::vector<char> data;
            auto io = get_content_io();
            io.limit_rd(0, content_v1_size);
            io.readall(data);

            return data;
        }

        void del_content_v1() {
            auto io = get_content_io();

            io.seek_wr(0);
            io.seek_rd(content_v1_size);
            io.copy_into_self(content_v2_size);

            resize_content(content_v2_size);
            content_v1_size = 0;

            notify_descriptor_changed();
        }

        void set_content_v2(const std::vector<char>& data) {
            if (not does_present_csize_fit(data.size() + content_v1_size)) {
                throw "";
            }

            content_v2_size = assert_u32(data.size());
            resize_content(content_v1_size + content_v2_size);

            auto io = get_content_io();
            io.limit_wr(content_v1_size, content_v2_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v2() {
            std::vector<char> data;
            auto io = get_content_io();
            io.limit_rd(content_v1_size, content_v2_size);
            io.readall(data);

            return data;
        }

        void del_content_v2() {
            auto io = get_content_io();

            resize_content(content_v1_size);
            content_v2_size = 0;

            notify_descriptor_changed();
        }

    protected:
        void read_struct_specifics_from(IOBase& io) override {
            content_v1_size = io.read_u32_from_le();
            if (io.remain_rd() > 0) {
                content_v2_size = io.read_u32_from_le();
            } else {
                // Backward compatible: V1 does not have content_v2_size field
                content_v2_size = 0;
            }
        }

        void write_struct_specifics_into(IOBase& io) override {
            io.write_u32_to_le(content_v1_size);
            io.write_u32_to_le(content_v2_size);
        }

        void update_sizes(uint64_t& isize, uint64_t& csize) override {
            isize = sizeof(content_v1_size) + sizeof(content_v2_size);
            csize = content_v1_size + content_v2_size;
        }

    private:
        uint32_t content_v1_size;
        uint32_t content_v2_size;
    };

    TEST(CompatibilityDescriptorTest, FwdBwdCompatibilityUnderNoChange) {
        RuntimeContext rctx_v1({
                {0xff, FooV1::create}
                });

        RuntimeContext rctx_v2({
                {0xff, FooV2::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        FooV1 dsc_v1 = FooV1(hdr, cblkarr);
        dsc_v1.set_content_v1({'A', 'B', 'C'});

        dsc_v1.full_sync(false);
        dsc_v1.write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, dsc_v1,
                "ff88 0000 0300 43c3 4142 0300 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc_v2 = tmp_v2->cast<FooV2>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v2->get_content_v1()),
                "4142 43"
                );

        dsc_v2->set_content_v2({'D', 'E'});

        dsc_v2->full_sync(false);
        dsc_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v2,
                "ff90 0000 0500 45c5 4142 4344 0300 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v2, rctx_v2, cblkarr);

        // From V2 to V1
        auto tmp2_v1 = Descriptor::load_struct_from(IOSpan(fp), rctx_v1, cblkarr);
        [[maybe_unused]] auto dsc2_v1 = tmp2_v1->cast<FooV1>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc2_v1->get_content_v1()),
                "4142 43"
                );

        // No modifications to V1

        dsc2_v1->full_sync(false);
        dsc2_v1->write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v1,
                "ff90 0000 0500 45c5 4142 4344 0300 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp2_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc2_v2 = tmp2_v2->cast<FooV2>();

        // Check data from V1 and V2 were preserved
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v1()),
                "4142 43"
                );
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v2()),
                "4445"
                );

        // No modifications to V2

        dsc2_v2->full_sync(false);
        dsc2_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v2,
                "ff90 0000 0500 45c5 4142 4344 0300 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v2, rctx_v2, cblkarr);
    }

    TEST(CompatibilityDescriptorTest, FwdBwdCompatibilityUnderShrinkInV1) {
        RuntimeContext rctx_v1({
                {0xff, FooV1::create}
                });

        RuntimeContext rctx_v2({
                {0xff, FooV2::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        FooV1 dsc_v1 = FooV1(hdr, cblkarr);
        dsc_v1.set_content_v1({'A', 'B', 'C'});

        dsc_v1.full_sync(false);
        dsc_v1.write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, dsc_v1,
                "ff88 0000 0300 43c3 4142 0300 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc_v2 = tmp_v2->cast<FooV2>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v2->get_content_v1()),
                "4142 43"
                );

        dsc_v2->set_content_v2({'D', 'E'});

        dsc_v2->full_sync(false);
        dsc_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v2,
                "ff90 0000 0500 45c5 4142 4344 0300 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v2, rctx_v2, cblkarr);

        // From V2 to V1
        auto tmp2_v1 = Descriptor::load_struct_from(IOSpan(fp), rctx_v1, cblkarr);
        [[maybe_unused]] auto dsc2_v1 = tmp2_v1->cast<FooV1>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc2_v1->get_content_v1()),
                "4142 43"
                );

        // Shrink V1 content
        dsc2_v1->set_content_v1({'F'});

        dsc2_v1->full_sync(false);
        dsc2_v1->write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v1,
                "ff90 0000 0300 45c3 4644 0100 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp2_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc2_v2 = tmp2_v2->cast<FooV2>();

        // Check data from V1 and V2 were preserved
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v1()),
                "46"
                );
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v2()),
                "4445"
                );

        // No modifications to V2

        dsc2_v2->full_sync(false);
        dsc2_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v2,
                "ff90 0000 0300 45c3 4644 0100 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v2, rctx_v2, cblkarr);
    }

    TEST(CompatibilityDescriptorTest, FwdBwdCompatibilityUnderExpandInV1) {
        RuntimeContext rctx_v1({
                {0xff, FooV1::create}
                });

        RuntimeContext rctx_v2({
                {0xff, FooV2::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        FooV1 dsc_v1 = FooV1(hdr, cblkarr);
        dsc_v1.set_content_v1({'A', 'B', 'C'});

        dsc_v1.full_sync(false);
        dsc_v1.write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, dsc_v1,
                "ff88 0000 0300 43c3 4142 0300 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc_v2 = tmp_v2->cast<FooV2>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v2->get_content_v1()),
                "4142 43"
                );

        dsc_v2->set_content_v2({'D', 'E'});

        dsc_v2->full_sync(false);
        dsc_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v2,
                "ff90 0000 0500 45c5 4142 4344 0300 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v2, rctx_v2, cblkarr);

        // From V2 to V1
        auto tmp2_v1 = Descriptor::load_struct_from(IOSpan(fp), rctx_v1, cblkarr);
        [[maybe_unused]] auto dsc2_v1 = tmp2_v1->cast<FooV1>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc2_v1->get_content_v1()),
                "4142 43"
                );

        // Expand V1 content
        dsc2_v1->set_content_v1({'F', 'G', 'H', 'I'});

        dsc2_v1->full_sync(false);
        dsc2_v1->write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v1,
                "ff90 0000 0600 00c6 4647 4849 4445 0400 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp2_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc2_v2 = tmp2_v2->cast<FooV2>();

        // Check data from V1 and V2 were preserved
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v1()),
                "4647 4849"
                );
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v2()),
                "4445"
                );

        // No modifications to V2

        dsc2_v2->full_sync(false);
        dsc2_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v2,
                "ff90 0000 0600 00c6 4647 4849 4445 0400 0000 0200 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v2, rctx_v2, cblkarr);
    }

    TEST(CompatibilityDescriptorTest, FwdBwdCompatibilityUnderALotInV2) {
        RuntimeContext rctx_v1({
                {0xff, FooV1::create}
                });

        RuntimeContext rctx_v2({
                {0xff, FooV2::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .own_content = false,
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .csize = 0,
            .segm = Segment::create_empty_zero_inline(cblkarr.blk_sz_order())
        };

        FooV1 dsc_v1 = FooV1(hdr, cblkarr);
        dsc_v1.set_content_v1({'A', 'B', 'C'});

        dsc_v1.full_sync(false);
        dsc_v1.write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, dsc_v1,
                "ff88 0000 0300 43c3 4142 0300 0000"
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc_v2 = tmp_v2->cast<FooV2>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v2->get_content_v1()),
                "4142 43"
                );

        // Expand V2 content by a lot
        std::vector<char> big_data((1 << 20) + 5);
        std::iota(std::begin(big_data), std::end(big_data), 0); // fill with numbers
        dsc_v2->set_content_v2(big_data);

        dsc_v2->full_sync(false);
        dsc_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v2,
                "ff90 0000 0880 2000 0004 0004 00c8 fdfe ff00 0102 0304 0300 0000 0500 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v2, rctx_v2, cblkarr);

        // From V2 to V1
        auto tmp2_v1 = Descriptor::load_struct_from(IOSpan(fp), rctx_v1, cblkarr);
        [[maybe_unused]] auto dsc2_v1 = tmp2_v1->cast<FooV1>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc2_v1->get_content_v1()),
                "4142 43"
                );

        // Shrink V1 to force the use fo in-disk buffers for moving the V2 future data
        dsc2_v1->set_content_v1({'F'});

        dsc2_v1->full_sync(false);
        dsc2_v1->write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v1,
                "ff90 0000 0680 2000 0004 0004 00c6 ff00 0102 0304 0100 0000 0500 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v1, rctx_v1, cblkarr);

        // From V1 to V2
        auto tmp2_v2 = Descriptor::load_struct_from(IOSpan(fp), rctx_v2, cblkarr);
        [[maybe_unused]] auto dsc2_v2 = tmp2_v2->cast<FooV2>();

        // Check data from V1 and V2 were preserved
        EXPECT_EQ(hexdump(dsc2_v2->get_content_v1()),
                "46"
                );

        // No modifications to V2

        dsc2_v2->full_sync(false);
        dsc2_v2->write_struct_into(IOSpan(fp), rctx_v2);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v2,
                "ff90 0000 0680 2000 0004 0004 00c6 ff00 0102 0304 0100 0000 0500 1000"
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v2);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v2, rctx_v2, cblkarr);
    }
}
