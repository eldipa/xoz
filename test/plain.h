#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

namespace testing_xoz {
/*
 * Plain descriptor: nothing interesting except the ability to carry idata.
 * Mostly used for testing.
 *
 * This descriptor has not a specified type by the xoz library.
 * It is application's responsibility to assign one.
 * */

class PlainDescriptor: public ::xoz::Descriptor {
public:
    PlainDescriptor(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr);

    static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                              ::xoz::RuntimeContext& rctx);


public:
    /*
     * The set_idata and get_idata methods are mostly for testing.
     * */
    void set_idata(const std::vector<char>& data);
    const std::vector<char>& get_idata() const;

protected:
    void read_struct_specifics_from(::xoz::IOBase& io) override;
    void write_struct_specifics_into(::xoz::IOBase& io) override;
    void update_sizes(uint64_t& isize, uint64_t& csize) override;

private:
    std::vector<char> idata;
};

/*
 * Same than PlainDescriptor but with an explicit track of the content (if any)
 * */
class PlainWithContentDescriptor : public PlainDescriptor {
public:
    PlainWithContentDescriptor(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr);

    static std::unique_ptr<::xoz::Descriptor> create(const struct ::xoz::Descriptor::header_t& hdr, ::xoz::BlockArray& cblkarr,
                                              ::xoz::RuntimeContext& rctx);

public:
    void set_content(const std::vector<char>& data);
    const std::vector<char> get_content();
    void del_content();

protected:
    void read_struct_specifics_from(::xoz::IOBase& io) override;
    void write_struct_specifics_into(::xoz::IOBase& io) override;
    void update_sizes(uint64_t& isize, uint64_t& csize) override;

private:
    uint32_t content_size;
    uint32_t optional_field_size() const;
};
}
