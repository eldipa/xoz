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

#include "xoz/dsc/spy.h"

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

typedef ::xoz::dsc::internals::DescriptorInnerSpyForTesting DSpy;

#define XOZ_RESET_FP(fp, sz) do {           \
    (fp).assign(sz, 0);                     \
} while (0)

// Check the size in bytes of the segm in terms of how much is needed
// to store the extents and how much they are pointing (allocated)
#define XOZ_EXPECT_SIZES(dsc, disk_sz, idata_sz, cdata_sz, obj_data_sz) do {      \
    EXPECT_EQ(DSpy(dsc).calc_struct_footprint_size(), (unsigned)(disk_sz));                            \
    EXPECT_EQ(DSpy(dsc).calc_internal_data_space_size(), (unsigned)(idata_sz));                                  \
    EXPECT_EQ(DSpy(dsc).calc_segm_data_space_size(0), (unsigned)(cdata_sz));      \
    EXPECT_EQ(DSpy(dsc).calc_declared_hdr_csize(0), (unsigned)(obj_data_sz));       \
} while (0)

// Check that the serialization of the obj in fp match
// byte-by-byte with the expected data (in hexdump) in the first
// N bytes and the rest of fp are zeros
#define XOZ_EXPECT_SERIALIZATION_v2(fp, dsc, data) do {                                 \
    EXPECT_EQ(hexdump((fp), 0, DSpy(dsc).calc_struct_footprint_size()), (data));         \
} while (0)

// Calc checksum over the fp (bytes) and expect to be the same as the descriptor's checksum
// Note: this requires a load_struct_from/write_struct_into call before to make
// the descriptor's checksum updated
#define XOZ_EXPECT_CHECKSUM(fp, dsc) do {    \
    EXPECT_EQ(inet_checksum((uint8_t*)(fp).data(), DSpy(dsc).calc_struct_footprint_size()), (dsc).checksum); \
} while (0)

#define XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc, rctx, cblkarr) do {                         \
    std::vector<char> buf2;                                              \
    XOZ_RESET_FP(buf2, FP_SZ);                                           \
    uint32_t checksum2 = 0;                                              \
    uint32_t checksum3 = 0;                                              \
                                                                         \
    uint32_t sz1 = DSpy(dsc).calc_struct_footprint_size();                   \
    auto d1 = hexdump((fp), 0, sz1);                                      \
                                                                         \
    auto dsc2_ptr = Descriptor::load_struct_from(IOSpan(fp), (rctx), (cblkarr));   \
    checksum2 = dsc2_ptr->checksum;                                      \
    dsc2_ptr->checksum = 0;                                              \
                                                                         \
    uint32_t sz2 = DSpy(*dsc2_ptr).calc_struct_footprint_size();               \
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
        FooV1(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr) : Descriptor(hdr, cblkarr, 1), content_v1_size(0) {}

        static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                                  [[maybe_unused]] ::xoz::RuntimeContext& rctx) {
            return std::make_unique<FooV1>(hdr, cblkarr);
        }

    public:
        void set_content_v1(const std::vector<char>& data) {
            content_v1_size = assert_u32(data.size());
            auto cpart = get_content_part(0);
            cpart.resize(content_v1_size);

            auto io = cpart.get_io();
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v1() {
            std::vector<char> data;
            auto io = get_content_part(0).get_io();

            // For V1, the entire content *is* content_v1
            io.readall(data);

            return data;
        }

        void del_content_v1() {
            get_content_part(0).resize(0);
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

        void update_isize(uint64_t& isize) override {
            isize = sizeof(content_v1_size);
        }

        void declare_used_content_space_on_load(std::vector<uint64_t>& cparts_sizes) const override {
            cparts_sizes[0] = content_v1_size;
        };

    private:
        uint32_t content_v1_size;
    };

    class FooV2 : public Descriptor {
    public:
        FooV2(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr) : Descriptor(hdr, cblkarr, 1), content_v1_size(0), content_v2_size(0) {}

        static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                                  [[maybe_unused]] ::xoz::RuntimeContext& rctx) {
            return std::make_unique<FooV2>(hdr, cblkarr);
        }

    public:
        void set_content_v1(const std::vector<char>& data) {
            content_v1_size = assert_u32(data.size());
            auto cpart = get_content_part(0);
            cpart.resize(content_v1_size + content_v2_size);

            auto io = cpart.get_io();
            io.limit_wr(0, content_v1_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v1() {
            std::vector<char> data;
            auto io = get_content_part(0).get_io();
            io.limit_rd(0, content_v1_size);
            io.readall(data);

            return data;
        }

        void del_content_v1() {
            auto cpart = get_content_part(0);
            auto io = cpart.get_io();

            io.seek_wr(0);
            io.seek_rd(content_v1_size);
            io.copy_into_self(content_v2_size);

            cpart.resize(content_v2_size);
            content_v1_size = 0;

            notify_descriptor_changed();
        }

        void set_content_v2(const std::vector<char>& data) {
            content_v2_size = assert_u32(data.size());
            auto cpart = get_content_part(0);
            cpart.resize(content_v1_size + content_v2_size);

            auto io = cpart.get_io();
            io.limit_wr(content_v1_size, content_v2_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v2() {
            std::vector<char> data;
            auto io = get_content_part(0).get_io();
            io.limit_rd(content_v1_size, content_v2_size);
            io.readall(data);

            return data;
        }

        void del_content_v2() {
            auto cpart = get_content_part(0);
            auto io = cpart.get_io();

            cpart.resize(content_v1_size);
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

        void update_isize(uint64_t& isize) override {
            isize = sizeof(content_v1_size) + sizeof(content_v2_size);
        }

        void declare_used_content_space_on_load(std::vector<uint64_t>& cparts_sizes) const override {
            cparts_sizes[0] = content_v1_size + content_v2_size;
        };

    private:
        uint32_t content_v1_size;
        uint32_t content_v2_size;
    };

    class FooV3 : public Descriptor {
    public:
        FooV3(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr) : Descriptor(hdr, cblkarr, 2), content_v1_size(0), content_v2_size(0), content_v3_size(0) {}

        static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                                  [[maybe_unused]] ::xoz::RuntimeContext& rctx) {
            return std::make_unique<FooV3>(hdr, cblkarr);
        }

    public:
        // Same as in FooV2
        void set_content_v1(const std::vector<char>& data) {
            content_v1_size = assert_u32(data.size());
            auto cpart = get_content_part(0);
            cpart.resize(content_v1_size + content_v2_size);

            auto io = cpart.get_io();
            io.limit_wr(0, content_v1_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        // Same as in FooV2
        const std::vector<char> get_content_v1() {
            std::vector<char> data;
            auto io = get_content_part(0).get_io();
            io.limit_rd(0, content_v1_size);
            io.readall(data);

            return data;
        }

        // Same as in FooV2
        void del_content_v1() {
            auto cpart = get_content_part(0);
            auto io = cpart.get_io();

            io.seek_wr(0);
            io.seek_rd(content_v1_size);
            io.copy_into_self(content_v2_size);

            cpart.resize(content_v2_size);
            content_v1_size = 0;

            notify_descriptor_changed();
        }

        // Same as in FooV2
        void set_content_v2(const std::vector<char>& data) {
            content_v2_size = assert_u32(data.size());
            auto cpart = get_content_part(0);
            cpart.resize(content_v1_size + content_v2_size);

            auto io = cpart.get_io();
            io.limit_wr(content_v1_size, content_v2_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        // Same as in FooV2
        const std::vector<char> get_content_v2() {
            std::vector<char> data;
            auto io = get_content_part(0).get_io();
            io.limit_rd(content_v1_size, content_v2_size);
            io.readall(data);

            return data;
        }

        // Same as in FooV2
        void del_content_v2() {
            auto cpart = get_content_part(0);
            auto io = cpart.get_io();

            cpart.resize(content_v1_size);
            content_v2_size = 0;

            notify_descriptor_changed();
        }

        // FooV3 uses a separated content part (part number 1)
        virtual void set_content_v3(const std::vector<char>& data) {
            content_v3_size = assert_u32(data.size());
            auto cpart = get_content_part(1);
            cpart.resize(content_v3_size);

            auto io = cpart.get_io();
            io.writeall(data);
            notify_descriptor_changed();
        }

        virtual const std::vector<char> get_content_v3() {
            std::vector<char> data;
            auto io = get_content_part(1).get_io();

            // For V3, the entire content *is* content_v3 (content part 2)
            io.readall(data);

            return data;
        }

        virtual void del_content_v3() {
            get_content_part(1).resize(0);
            content_v3_size = 0;
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

            if (io.remain_rd() > 0) {
                content_v3_size = io.read_u32_from_le();
            } else {
                // Backward compatible: V2 does not have content_v3_size field
                content_v3_size = 0;
            }
        }

        void write_struct_specifics_into(IOBase& io) override {
            io.write_u32_to_le(content_v1_size);
            io.write_u32_to_le(content_v2_size);
            io.write_u32_to_le(content_v3_size);
        }

        void update_isize(uint64_t& isize) override {
            isize = sizeof(content_v1_size) + sizeof(content_v2_size) + sizeof(content_v3_size);
        }

        void declare_used_content_space_on_load(std::vector<uint64_t>& cparts_sizes) const override {
            cparts_sizes[0] = content_v1_size + content_v2_size; // Same as in FooV2
            cparts_sizes[1] = content_v3_size;
        };

    protected:
        uint32_t content_v1_size;
        uint32_t content_v2_size;
        uint32_t content_v3_size;
    };

    // Example of how to code a new version inheriting from a older one.
    // It is not the recommended way to do it but for sake of the testing,
    // it is easier in this way.
    class FooV4 : public FooV3 {
    public:
        FooV4(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr) : FooV3(hdr, cblkarr), content_v4_size(0) {}

        static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                                  [[maybe_unused]] ::xoz::RuntimeContext& rctx) {
            return std::make_unique<FooV4>(hdr, cblkarr);
        }

    public:
        // V1 and V2 are inherit from V3
        // Here we override V3's to make it aware of the extra data from V4
        // in the same content part
        void set_content_v3(const std::vector<char>& data) override {
            content_v3_size = assert_u32(data.size());
            auto cpart = get_content_part(1);
            cpart.resize(content_v3_size + content_v4_size);

            auto io = cpart.get_io();
            io.limit_wr(0, content_v3_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v3() override {
            std::vector<char> data;
            auto io = get_content_part(1).get_io();
            io.limit_rd(0, content_v3_size);
            io.readall(data);

            return data;
        }

        void del_content_v3() override {
            auto cpart = get_content_part(1);
            auto io = cpart.get_io();

            io.seek_wr(0);
            io.seek_rd(content_v3_size);
            io.copy_into_self(content_v4_size);

            cpart.resize(content_v4_size);
            content_v3_size = 0;

            notify_descriptor_changed();
        }

        void set_content_v4(const std::vector<char>& data) {
            content_v4_size = assert_u32(data.size());
            auto cpart = get_content_part(1);
            cpart.resize(content_v3_size + content_v4_size);

            auto io = cpart.get_io();
            io.limit_wr(content_v3_size, content_v4_size);
            io.writeall(data);
            notify_descriptor_changed();
        }

        const std::vector<char> get_content_v4() {
            std::vector<char> data;
            auto io = get_content_part(1).get_io();
            io.limit_rd(content_v3_size, content_v4_size);
            io.readall(data);

            return data;
        }

        void del_content_v4() {
            auto cpart = get_content_part(1);
            auto io = cpart.get_io();

            cpart.resize(content_v3_size);
            content_v4_size = 0;

            notify_descriptor_changed();
        }

    protected:
        void read_struct_specifics_from(IOBase& io) override {
            FooV3::read_struct_specifics_from(io);

            if (io.remain_rd() > 0) {
                content_v4_size = io.read_u32_from_le();
            } else {
                // Backward compatible: V3 does not have content_v4_size field
                content_v4_size = 0;
            }
        }

        void write_struct_specifics_into(IOBase& io) override {
            FooV3::write_struct_specifics_into(io);
            io.write_u32_to_le(content_v4_size);
        }

        void update_isize(uint64_t& isize) override {
            FooV3::update_isize(isize);
            isize += sizeof(content_v4_size);
        }

        void declare_used_content_space_on_load(std::vector<uint64_t>& cparts_sizes) const override {
            FooV3::declare_used_content_space_on_load(cparts_sizes);
            cparts_sizes[1] += content_v4_size;
        };

    protected:
        uint32_t content_v4_size;
    };

    TEST(CompatibilityDescriptorTest, FwdBwdCompatibilityUnderNoChange) {
        RuntimeContext rctx_v1({
                {0xff, FooV1::create}
                });

        RuntimeContext rctx_v2({
                {0xff, FooV2::create}
                });

        RuntimeContext rctx_v3({
                {0xff, FooV3::create}
                });

        RuntimeContext rctx_v4({
                {0xff, FooV4::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
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

        // From V2 to V3
        auto tmp_v3 = Descriptor::load_struct_from(IOSpan(fp), rctx_v3, cblkarr);
        [[maybe_unused]] auto dsc_v3 = tmp_v3->cast<FooV3>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v3->get_content_v1()),
                "4142 43"
                );

        // Check data from V2 was preserved
        EXPECT_EQ(hexdump(dsc_v3->get_content_v2()),
                "4445"
                );

        dsc_v3->set_content_v3({'F', 'G'});

        dsc_v3->full_sync(false);
        dsc_v3->write_struct_into(IOSpan(fp), rctx_v3);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v3,
                "ff98 0100 "
                "0500 45c5 4142 4344 "  // cpart-0: csize: 5, data: A B C D E
                "0200 00c2 4647 "       // cpart-1: csize: 2, data: F G
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0200 0000"     // content_v3_size
                );

        XOZ_EXPECT_CHECKSUM(fp, *dsc_v3);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v3, rctx_v3, cblkarr);

        // From V3 to V4
        auto tmp_v4 = Descriptor::load_struct_from(IOSpan(fp), rctx_v4, cblkarr);
        [[maybe_unused]] auto dsc_v4 = tmp_v4->cast<FooV4>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v4->get_content_v1()),
                "4142 43"
                );

        // Check data from V2 was preserved
        EXPECT_EQ(hexdump(dsc_v4->get_content_v2()),
                "4445"
                );

        // Check data from V3 was preserved
        EXPECT_EQ(hexdump(dsc_v4->get_content_v3()),
                "4647"
                );

        dsc_v4->set_content_v4({'H', 'I', 'J', 'K'});

        dsc_v4->full_sync(false);
        dsc_v4->write_struct_into(IOSpan(fp), rctx_v4);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v4,
                "ffa0 0100 "
                "0500 45c5 4142 4344 "          // cpart-0: csize: 5, data: A B C D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0200 0000 "    // content_v3_size
                "0400 0000"     // content_v4_size
                );

        XOZ_EXPECT_CHECKSUM(fp, *dsc_v4);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v4, rctx_v4, cblkarr);

        // From V4 to V1
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
                "ffa0 0100 "
                "0500 45c5 4142 4344 "          // cpart-0: csize: 5, data: A B C D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0200 0000 "    // content_v3_size
                "0400 0000"     // content_v4_size
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v1, rctx_v1, cblkarr);

        // From V1 to V4
        auto tmp2_v4 = Descriptor::load_struct_from(IOSpan(fp), rctx_v4, cblkarr);
        [[maybe_unused]] auto dsc2_v4 = tmp2_v4->cast<FooV4>();

        // Check data from V1, V2, V3 and V4 were preserved
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v1()),
                "4142 43"
                );
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v2()),
                "4445"
                );
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v3()),
                "4647"
                );
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v4()),
                "4849 4a4b"
                );

        // No modifications to V4

        dsc2_v4->full_sync(false);
        dsc2_v4->write_struct_into(IOSpan(fp), rctx_v4);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v4,
                "ffa0 0100 "
                "0500 45c5 4142 4344 "          // cpart-0: csize: 5, data: A B C D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0200 0000 "    // content_v3_size
                "0400 0000"     // content_v4_size
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v4);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v4, rctx_v4, cblkarr);

        // From V4 to V3
        auto tmp2_v3 = Descriptor::load_struct_from(IOSpan(fp), rctx_v3, cblkarr);
        [[maybe_unused]] auto dsc2_v3 = tmp2_v3->cast<FooV3>();

        // Check data from V1, V2 and V3 were preserved
        EXPECT_EQ(hexdump(dsc2_v3->get_content_v1()),
                "4142 43"
                );
        EXPECT_EQ(hexdump(dsc2_v3->get_content_v2()),
                "4445"
                );
        EXPECT_EQ(hexdump(dsc2_v3->get_content_v3()),
                "4647"
                );

        // No modifications to V3

        dsc2_v3->full_sync(false);
        dsc2_v3->write_struct_into(IOSpan(fp), rctx_v3);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v3,
                "ffa0 0100 "
                "0500 45c5 4142 4344 "          // cpart-0: csize: 5, data: A B C D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0200 0000 "    // content_v3_size
                "0400 0000"     // content_v4_size
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc2_v3);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc2_v3, rctx_v3, cblkarr);

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
                "ffa0 0100 "
                "0500 45c5 4142 4344 "          // cpart-0: csize: 5, data: A B C D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0200 0000 "    // content_v3_size
                "0400 0000"     // content_v4_size
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

        RuntimeContext rctx_v3({
                {0xff, FooV3::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
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

        // From V1 to V3
        auto tmp_v3 = Descriptor::load_struct_from(IOSpan(fp), rctx_v3, cblkarr);
        [[maybe_unused]] auto dsc_v3 = tmp_v3->cast<FooV3>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v3->get_content_v1()),
                "4142 43"
                );

        // Check that we have nothing from V2 or V3
        EXPECT_EQ(hexdump(dsc_v3->get_content_v2()),
                ""
                );
        EXPECT_EQ(hexdump(dsc_v3->get_content_v3()),
                ""
                );

        // Set V2 content (we want to test how this is preserved later under V1 shrink)
        dsc_v3->set_content_v2({'D', 'E'});

        dsc_v3->full_sync(false);
        dsc_v3->write_struct_into(IOSpan(fp), rctx_v3);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v3,
                "ff98 0000 "
                "0500 45c5 4142 4344 " // cpart-0: csize: 5, data: A B C D E
                // cpart-1 (from V3) is not written because it was compressed.

                // idata
                "0300 0000 " // content_v1_size
                "0200 0000 " // content_v2_size
                "0000 0000"  // content_v3_size
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc_v3);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v3, rctx_v3, cblkarr);

        // From V3 to V1
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
                "ff98 0000 0300 45c3 4644 0100 0000 0200 0000 0000 0000"
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
                "ff98 0000 0300 45c3 4644 0100 0000 0200 0000 0000 0000"
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
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
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
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
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

    TEST(CompatibilityDescriptorTest, FwdBwdCompatibilityUnderContentPartDeletions) {
        RuntimeContext rctx_v1({
                {0xff, FooV1::create}
                });

        RuntimeContext rctx_v2({
                {0xff, FooV2::create}
                });

        RuntimeContext rctx_v3({
                {0xff, FooV3::create}
                });

        RuntimeContext rctx_v4({
                {0xff, FooV4::create}
                });

        std::vector<char> fp;
        XOZ_RESET_FP(fp, FP_SZ);

        VectorBlockArray cblkarr(1024);
        cblkarr.allocator().initialize_with_nothing_allocated();

        struct Descriptor::header_t hdr = {
            .type = 0xff,

            .id = 0x80000001,

            .isize = 0,
            .cparts = {}
        };

        FooV4 dsc_v4 = FooV4(hdr, cblkarr);
        dsc_v4.set_content_v1({'A', 'B', 'C'});
        dsc_v4.set_content_v2({'D', 'E'});
        dsc_v4.set_content_v3({'F', 'G', 'H', 'I'});
        dsc_v4.set_content_v4({'J', 'K'});

        dsc_v4.full_sync(false);
        dsc_v4.write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, dsc_v4,
                "ffa0 0100 "
                "0500 45c5 4142 4344 "          // cpart-0: csize: 5, data: A B C D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0300 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0400 0000 "    // content_v3_size
                "0200 0000"     // content_v4_size
                );
        XOZ_EXPECT_CHECKSUM(fp, dsc_v4);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, dsc_v4, rctx_v4, cblkarr);

        // From V4 to V1
        auto tmp_v1 = Descriptor::load_struct_from(IOSpan(fp), rctx_v1, cblkarr);
        [[maybe_unused]] auto dsc_v1 = tmp_v1->cast<FooV1>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v1->get_content_v1()),
                "4142 43"
                );

        // Delete V1
        dsc_v1->del_content_v1();

        dsc_v1->full_sync(false);
        dsc_v1->write_struct_into(IOSpan(fp), rctx_v1);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v1,
                "ffa0 0100 "
                "0200 00c2 4445 "               // cpart-0: csize: 2, data: D E
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0000 0000 "    // content_v1_size
                "0200 0000 "    // content_v2_size
                "0400 0000 "    // content_v3_size
                "0200 0000"     // content_v4_size
                );
        XOZ_EXPECT_CHECKSUM(fp, *dsc_v1);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v1, rctx_v1, cblkarr);

        // From V1 to V3
        auto tmp_v3 = Descriptor::load_struct_from(IOSpan(fp), rctx_v3, cblkarr);
        [[maybe_unused]] auto dsc_v3 = tmp_v3->cast<FooV3>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc_v3->get_content_v1()),
                ""
                );

        // Check data from V2 was preserved
        EXPECT_EQ(hexdump(dsc_v3->get_content_v2()),
                "4445"
                );

        // Check data from V3 was preserved
        EXPECT_EQ(hexdump(dsc_v3->get_content_v3()),
                "4647 4849"
                );

        // Delete V2 (not V3) so cpart[0] gets empty
        dsc_v3->del_content_v2();

        dsc_v3->full_sync(false);
        dsc_v3->write_struct_into(IOSpan(fp), rctx_v3);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc_v3,
                "ffa0 0100 "
                "0000 00c0 "                    // cpart-0: csize: 0
                "0600 00c6 4647 4849 4a4b "     // cpart-1: csize: 6, data: F G H I J K
                // idata
                "0000 0000 "    // content_v1_size
                "0000 0000 "    // content_v2_size
                "0400 0000 "    // content_v3_size
                "0200 0000"     // content_v4_size
                );

        XOZ_EXPECT_CHECKSUM(fp, *dsc_v3);
        XOZ_EXPECT_DESERIALIZATION_v2(fp, *dsc_v3, rctx_v3, cblkarr);

        // From V3 to V4
        auto tmp_v4 = Descriptor::load_struct_from(IOSpan(fp), rctx_v4, cblkarr);
        [[maybe_unused]] auto dsc2_v4 = tmp_v4->cast<FooV4>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v1()),
                ""
                );

        // Check data from V2 was preserved
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v2()),
                ""
                );

        // Check data from V3 was preserved
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v3()),
                "4647 4849"
                );

        // Check data from V4 was preserved
        EXPECT_EQ(hexdump(dsc2_v4->get_content_v4()),
                "4a4b"
                );

        // Delete V3 and V4 so cpart[1] gets empty
        dsc2_v4->del_content_v3();
        dsc2_v4->del_content_v4();

        dsc2_v4->full_sync(false);
        dsc2_v4->write_struct_into(IOSpan(fp), rctx_v4);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v4,
                "ff20 " // no cparts_cnt counter
                // No cpart - header was compressed.
                // idata
                "0000 0000 "    // content_v1_size
                "0000 0000 "    // content_v2_size
                "0000 0000 "    // content_v3_size
                "0000 0000"     // content_v4_size
                );

        // Add some V4 data, this will force to write cpart[0]
        dsc2_v4->set_content_v4({'A'});

        dsc2_v4->full_sync(false);
        dsc2_v4->write_struct_into(IOSpan(fp), rctx_v4);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v4,
                "ffa0 0100 "
                "0000 00c0 "    // cpart-0: csize: 0
                "0100 41c1 "    // cpart-1: csize: 1, data: A
                // idata
                "0000 0000 "    // content_v1_size
                "0000 0000 "    // content_v2_size
                "0000 0000 "    // content_v3_size
                "0100 0000"     // content_v4_size
                );

        // From V4 to V3
        auto tmp2_v3 = Descriptor::load_struct_from(IOSpan(fp), rctx_v3, cblkarr);
        [[maybe_unused]] auto dsc2_v3 = tmp2_v3->cast<FooV3>();

        // Check data from V1 was preserved
        EXPECT_EQ(hexdump(dsc2_v3->get_content_v1()),
                ""
                );

        // Check data from V2 was preserved
        EXPECT_EQ(hexdump(dsc2_v3->get_content_v2()),
                ""
                );

        // Check data from V3 was preserved
        EXPECT_EQ(hexdump(dsc2_v3->get_content_v3()),
                ""
                );

        // No change

        dsc2_v3->full_sync(false);
        dsc2_v3->write_struct_into(IOSpan(fp), rctx_v3);
        XOZ_EXPECT_SERIALIZATION_v2(fp, *dsc2_v3,
                "ffa0 0100 "
                "0000 00c0 "    // cpart-0: csize: 0
                "0100 41c1 "    // cpart-1: csize: 1, data: A
                // idata
                "0000 0000 "    // content_v1_size
                "0000 0000 "    // content_v2_size
                "0000 0000 "    // content_v3_size
                "0100 0000"     // content_v4_size
                );
    }
}
