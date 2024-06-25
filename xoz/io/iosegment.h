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
    Segment& sg;

    const uint32_t sg_no_inline_sz;

    const std::vector<uint32_t> begin_positions;

public:
    /*
     * Note: the IOSegment takes a *mutable* non-const reference to the segment.
     * Such non-cost is needed because IOSegment will use the inline data space
     * of the segment (if any) for reading and writing.
     * Therefore the segment instance must *not* be moved nor destroyed while
     * this IOSegment is still alive.
     * */
    IOSegment(BlockArray& blkarr, Segment& sg);

    /*
     * Get a clone of the IOSegment.
     * The segment is shared by both io objects so:
     *  - the segment must outlive both io objects
     *  - writing from one io will be reflected on the other
     * The cloned IOSegment will have its own independent
     * rd/rw pointers and with the same values as the values
     * that the original io has.
     * */
    IOSegment dup() const;

    /*
     * Similar to IOBase::fill, this class method fill_c fills the data space pointed
     * by the segment in the given block array with a single byte c.
     *
     * This method fills the data space completely (that may include or not the inline
     * data space).
     * */
    static void fill_c(BlockArray& blkarr, Segment& sg, const char c, const bool include_inline);

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

protected:
    IOSegment(const IOSegment& io) = default;
};
