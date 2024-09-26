#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

namespace xoz {
/*
 * Plain descriptor: nothing interesting except the ability to carry idata.
 * Mostly used for testing.
 *
 * This descriptor has not a specified type by the xoz library.
 * It is application's responsibility to assign one.
 * */

class PlainDescriptor: public Descriptor {
public:
    PlainDescriptor(const struct Descriptor::header_t& hdr, BlockArray& cblkarr);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              RuntimeContext& rctx);


public:
    /*
     * The set_idata and get_idata methods are mostly for testing.
     * */
    void set_idata(const std::vector<char>& data);

    const std::vector<char>& get_idata() const;

protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
    void update_sizes(uint8_t& isize, uint32_t& csize) override;

private:
    std::vector<char> idata;
};
}  // namespace xoz
