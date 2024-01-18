#pragma once

#include <cstdint>
#include <vector>

#include "xoz/io/iobase.h"
#include "xoz/segm/segment.h"

class BlockArray;

/*
 * Read/write the data hold by a Segment handling all the details
 * required to provide a continuous byte stream from a discontinuos
 * unordered set of Extents in the BlockArray.
 *
 * The read/write operation will have a direct impact on the repository
 * (so also in the file in disk). IOSegment may offer buffering for
 * performance reasons but it must be assumed that each operation
 * is a I/O disk operation.
 * */
class IOSegment final: public IOBase {
private:
    BlockArray& blkarr;
    Segment sg;

    const uint32_t sg_no_inline_sz;

    const std::vector<uint32_t> begin_positions;

public:
    IOSegment(BlockArray& blkarr, const Segment& sg);

private:
    struct ext_ptr_t {
        Extent ext;
        uint32_t offset;
        uint32_t remain;
        bool end;
    };

    const struct ext_ptr_t abs_pos_to_ext(const uint32_t pos) const;

    /*
     * The given buffer must have enough space to hold max_data_sz bytes The operation
     * will read/write up to max_data_sz bytes but it may less.
     *
     * The count of bytes read/written is returned.
     *
     * */
    uint32_t rw_operation(const bool is_read_op, char* data, const uint32_t max_data_sz) override final;
};
