#pragma once
#include <memory>
#include <vector>

#include "xoz/dsc/descriptor.h"
#include "xoz/io/iobase.h"

// Default descriptor: it is the most basic descriptor possible that just
// carries the data read from a XOZ file about a descriptor and writes into
// the file without further interpretation.
class DefaultDescriptor: public Descriptor {
private:
    std::vector<char> dsc_data;

public:
    explicit DefaultDescriptor(const struct Descriptor::header_t& hdr);

    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr);

    void set_data(const std::vector<char>& data);

    void set_edata(const std::vector<char>& data);

protected:
    void read_struct_specifics_from(IOBase& io) override;
    void write_struct_specifics_into(IOBase& io) override;
};
