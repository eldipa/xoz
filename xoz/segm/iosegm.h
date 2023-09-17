#pragma once

#include <cstdint>
#include <vector>

#include "xoz/mem/iobase.h"
#include "xoz/repo/repo.h"
#include "xoz/segm/segment.h"


class IOSegment final: public IOBase {
private:
    Repository& repo;
    Segment sg;

    const uint32_t sg_no_inline_sz;

    const std::vector<uint32_t> begin_positions;

public:
    IOSegment(Repository& repo, const Segment& sg);

private:
    struct ext_ptr_t {
        Extent ext;
        uint32_t offset;
        uint32_t remain;
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
