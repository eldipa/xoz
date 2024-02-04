#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/mem/bits.h"

class BlockArray {
protected:
    void initialize_block_array(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr);

    /*
     * Grow the block array for at least the given amount of blocks. The subclass may decide
     * to grow the array by more blocks if it makes more sense due the implementation details
     * of the underlaying data backend handled by the subclass.
     *
     * Return the first block number available (which should be the past_end_blk_nr() before the grow)
     * and the count of blocks really allocated.
     * */
    virtual std::tuple<uint32_t, uint16_t> impl_grow_by_blocks(uint16_t blk_cnt) = 0;

    /*
     * Shrink the block array by more or less the given amount o blocks. Return the exact
     * count of blocks removed.
     *
     * The subclass may decide to shrink the array by less blocks if it makes more sense
     * due the implementation details of the underlaying data backend handled by the subclass
     * or it may shrink by more but only if that is to compensate a previous call to
     * impl_shrink_by_blocks that returned less.
     **/
    virtual uint32_t impl_shrink_by_blocks(uint32_t blk_cnt) = 0;

    /*
     * Release any pending block, shrinking the block array even more. The subclass must shrink
     * the block array as much as possible, freeing any pending block left of previous
     * impl_shrink_by_blocks calls.
     *
     * Return how many blocks the block array was shrank by.
     * */
    virtual uint32_t impl_release_blocks() = 0;

    virtual uint32_t impl_read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) = 0;
    virtual uint32_t impl_write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) = 0;

    /*
     * Check that the read/write operation is within the bounds of this BlockArray and that the
     * start/max_data_sz are ok.
     *
     * The operation is interpreted as read (is_read_op) or write (not is_read_op). The extent ext is the
     * extent where the read/write takes place and it is checked for being within the bounds of the array.
     *
     * The start is the start position within the extent in bytes units. And max_data_sz is the maximum
     * in bytes to read/write. The maximum may be larger than the available in the extent (that's not an error).
     *
     * Any incompatibility will throw.
     *
     * If everything is ok, return exactly how much can be read/write in bytes (this may be less than the
     * max_data_sz if for example there is less space in the extent).
     *
     * Subclasses may extend this method to add additional checks.
     * */
    virtual uint32_t chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start);

    BlockArray(bool coalescing_enabled = true, uint16_t split_above_threshold = 0,
               const struct SegmentAllocator::req_t& default_req = SegmentAllocator::XOZDefaultReq);

private:
    uint32_t _blk_sz;
    uint8_t _blk_sz_order;

    uint32_t _begin_blk_nr;
    uint32_t _past_end_blk_nr;
    uint32_t _real_past_end_blk_nr;

    SegmentAllocator sg_alloc;

public:
    /*
     * Define a block array with block of the given size <blk_sz>.
     * The array begins at the given block number and spans to <past_end_blk_nr>
     * (the <begin_blk_nr> is inclusive but <past_end_blk_nr> is not).
     *
     * The subclasses of BlockArray must provide an implementation that support
     * adding/removing more blocks (impl_grow_by_blocks / impl_shrink_by_blocks)
     * and read/write extents of blocks from/to the array
     * (impl_read_extent / impl_write_extent)
     * */
    BlockArray(uint32_t blk_sz, uint32_t begin_blk_nr, uint32_t past_end_blk_nr, bool coalescing_enabled = true,
               uint16_t split_above_threshold = 0,
               const struct SegmentAllocator::req_t& default_req = SegmentAllocator::XOZDefaultReq);


    // Block definition
    inline uint32_t subblk_sz() const { return _blk_sz >> Extent::SUBBLK_SIZE_ORDER; }

    inline uint32_t blk_sz() const { return _blk_sz; }

    inline uint8_t blk_sz_order() const { return _blk_sz_order; }

    SegmentAllocator& allocator() { return sg_alloc; }

    /*
     * Main primitive to allocate / free blocks
     *
     * This expands/shrinks the block array and the underlying
     * backend space.
     *
     * The expansion/shrink is *not* visible by the allocator.
     * grow_by_blocks()/shrink_by_blocks()/release_blocks() are meant
     * to be used as a low-level API (for when the allocator is
     * still not initialized or it cannot be used).
     * Callers *should* use allocator().alloc() and allocator().dealloc()
     * to reserve/release space (that may grow/shrink the array if needed).
     **/
    uint32_t grow_by_blocks(uint16_t blk_cnt);
    void shrink_by_blocks(uint32_t blk_cnt);

    /*
     * This method release any pending shrink operation that may
     * exit. The caller should call this once the operations with
     * the block array are done or very sporadically.
     *
     * Returns how many blocks were released.
     * */
    uint32_t release_blocks();

    // Return the block number of the first block with data
    // (begin_blk_nr) and the past-the-end data section
    // (past_end_blk_nr).
    //
    // Blocks smaller (strict) than begin_blk_nr()
    // and the blocks equal to or greater than past_end_blk_nr()
    // are reserved (it may not even exist in the backend)
    //
    // The total count of readable/writable data blocks by
    // the callers is (past_end_blk_nr() - begin_blk_nr())
    // and it may be zero (blk_cnt)
    inline uint32_t begin_blk_nr() const { return _begin_blk_nr; }

    inline uint32_t past_end_blk_nr() const { return _past_end_blk_nr; }

    inline uint32_t blk_cnt() const { return past_end_blk_nr() - begin_blk_nr(); }

    // Check if the extent is within the boundaries of the block array.
    inline bool is_extent_within_boundaries(const Extent& ext) const {
        return not(ext.blk_nr() < begin_blk_nr() or ext.blk_nr() >= past_end_blk_nr() or
                   ext.past_end_blk_nr() > past_end_blk_nr());
    }

    // Call is_extent_within_boundaries(ext) and if it is false
    // raise ExtentOutOfBounds with the given message
    void fail_if_out_of_boundaries(const Extent& ext, const std::string& msg) const;

    // Read / write <blk_cnt> consecutive blocks starting from the given
    // <blk_nr> with <start> bytes offset (default 0)
    //
    // The data's buffer to read into / write from <blk_data> must be
    // allocated by the caller.
    //
    // On reading, if a std::vector is given, the vector
    // will be resized to reserve enough bytes to store the content
    // read up to <max_data_sz> bytes.
    //
    // If <max_data_sz> is given, no more than <max_data_sz> bytes
    // will be read/written.
    //
    // The space in-disk from which we are reading / writing must
    // be previously allocated.
    //
    // Reading / writing out of bounds may succeed *but* it is undefined
    // and it will probably lead to corruption.
    //
    // Returns the count of bytes effectively read/written.  A value of
    // 0 means the end of the stream (it could happen if <start> is past
    // the end of the extent or if <blk_cnt> is 0)
    uint32_t read_extent(const Extent& ext, char* data, uint32_t max_data_sz = uint32_t(-1), uint32_t start = 0);
    uint32_t read_extent(const Extent& ext, std::vector<char>& data, uint32_t max_data_sz = uint32_t(-1),
                         uint32_t start = 0);

    uint32_t write_extent(const Extent& ext, const char* data, uint32_t max_data_sz = uint32_t(-1), uint32_t start = 0);
    uint32_t write_extent(const Extent& ext, const std::vector<char>& data, uint32_t max_data_sz = uint32_t(-1),
                          uint32_t start = 0);

    virtual ~BlockArray() {}
};
