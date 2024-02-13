#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

/*
 * Default descriptor: it is the most basic descriptor possible that just
 * carries the data read from a XOZ file about a descriptor and writes into
 * the file without further interpretation.
 *
 * It is meant to be used by XOZ to load and write descriptors of unknown
 * types.
 * */

class DefaultDescriptor: public Descriptor {
private:
    std::vector<char> dsc_data;

public:
    DefaultDescriptor(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr);

    void set_data(const std::vector<char>& data);

    void set_edata(const std::vector<char>& data);

    const std::vector<char>& get_data() const;

protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
};
