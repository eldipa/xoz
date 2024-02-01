#pragma once

#include <memory>
#include <tuple>
#include <vector>

#include "xoz/blk/block_array.h"
#include "xoz/io/iospan.h"

class Segment;

/*
 * Vector based BlockArray. This subclass implements the BlockArray interface
 * using a std::vector as its underlying data container.
 * It is mostly for testing purpose and as an example of a simple implementation
 * of a BlockArray subclass.
 * */
class VectorBlockArray: public BlockArray {
protected:
    std::tuple<uint32_t, uint16_t> impl_grow_by_blocks(uint16_t ar_blk_cnt) override;

    uint32_t impl_shrink_by_blocks(uint32_t ar_blk_cnt) override;

    uint32_t impl_read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) override;

    uint32_t impl_write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) override;

    uint32_t impl_release_blocks() override;

private:
    std::vector<char> buf;
    std::unique_ptr<IOSpan> io;

public:
    explicit VectorBlockArray(uint32_t blk_sz);
    ~VectorBlockArray();

    const std::vector<char>& expose_mem_fp() const { return buf; }
};
