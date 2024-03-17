#pragma once

#include <string>
#include <tuple>
#include <vector>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/ext/extent.h"
#include "xoz/mem/bits.h"

class BlockArray {
protected:
    /*
     * Initialize the array of blocks of positive size <blk_sz>, with the blocks <begin_blk_nr>
     * to <past_end_blk_nr> available. Bblocks before <begin_blk_nr> exist but are not accesible,
     * blocks <past_end_blk_nr> and beyond don't exist but they can be added to the array
     * calling grow_by_blocks() (and can be removed with shrink_by_blocks()).
     *
     * This method should be called by subclass' constructors.
     *
     * The method will call manage_block_array() on the segment allocator that this array
     * has however the allocator will not be fully usable until the caller call
     * initialize_from_allocated (or similar) on the allocator.
     *
     * */
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
     *
     * In any case, the blocks "pending" to be shrunk must be kept allocated
     * so a subsequent call to grow_by_blocks() can use them to grow the array
     * without calling impl_grow_by_blocks().
     *
     * For example, if the user calls shrink_by_blocks() for N=4 blocks, impl_shrink_by_blocks
     * is called with N=4; let's assume that impl_shrink_by_blocks returns M=1, so the array
     * really shrank by 1 block but to the user, it looks like as if the array shrank by 4 blocks
     * (and all the public methods will reflect that). Only capacity() will reflect the fact
     * that there are more blocks than spouse to be.
     * There are 3 blocks "pending", not accessible by the user but accesible by grow_by_blocks.
     * If the user calls grow_by_blocks() with N=2 blocks, BlockArray will take those 2 blocks
     * from the 3 "pending" blocks *without* calling impl_grow_by_blocks().
     **/
    virtual uint32_t impl_shrink_by_blocks(uint32_t blk_cnt) = 0;

    /*
     * Release any pending block, shrinking the block array even more. The subclass must shrink
     * the block array as much as possible, freeing any pending block left of previous
     * impl_shrink_by_blocks calls.
     *
     * While the subclass must do its best effort, it may not be possible. If the user needs
     * to know how many blocks are still pending, it should calculate capacity()-blk_cnt().
     *
     * Return how many blocks the block array was shrank by.
     * */
    virtual uint32_t impl_release_blocks() = 0;

    /*
     * Read/write the amount exact_sz bytes from the block array. The block to read is given
     * by blk_nr and the offset is how many bytes skip from the begin of the block.
     *
     * Both the read and write can span multiple consecutive blocks.
     * */
    virtual void impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) = 0;
    virtual void impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) = 0;

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

    /*
     * Move constructor (for subclasses only).
     * */
    BlockArray(BlockArray&& blkarr) = default;

    /*
     * Move assignation and copy constructor/assignation is not allowed
     * */
    BlockArray& operator=(BlockArray&& blkarr) = delete;
    BlockArray(const BlockArray& blkarr) = delete;
    BlockArray& operator=(const BlockArray& blkarr) = delete;

protected:
    /*
     * Raise if the given block size is not suitable.
     *
     * If min_subblk_sz is non zero, the block size also must be large enough
     * to be suitable for suballocation with sub blocks of min_subblk_sz size at minimum.
     * If zero, no check is made.
     * */
    static void fail_if_bad_blk_sz(uint32_t blk_sz, uint32_t min_subblk_sz = 0);

    /*
     * Raise if the given block number is not a valid one.
     * */
    static void fail_if_bad_blk_nr(uint32_t blk_nr);

private:
    /*
     * Read or write an extent given by ext (based on is_read_op). How much bytes are being read/written
     * is given by to_rw_sz and from where to start the operation is given by start (start of 0 means
     * do the operation at the begin of the extent, a start > 0 means skip start bytes from the begin).
     *
     * rw_suballocated_extent is designed for suballocation extent while rw_fully_allocated_extent
     * is for non-suballocated extents.
     * */
    uint32_t rw_suballocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz, uint32_t start);
    uint32_t rw_fully_allocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz,
                                       uint32_t start);

private:
    uint32_t _blk_sz;
    uint8_t _blk_sz_order;

    uint32_t _begin_blk_nr;
    uint32_t _past_end_blk_nr;
    uint32_t _real_past_end_blk_nr;

    SegmentAllocator sg_alloc;

    bool blkarr_initialized;

private:
    uint64_t _grow_call_cnt;
    uint64_t _grow_expand_capacity_call_cnt;
    uint64_t _shrink_call_cnt;
    uint64_t _release_call_cnt;

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
     * Convenient block-to-bytes and bytes-to-block functions. When bytes-to-blocks conversion
     * happen, a check is made to ensure that the bytes number is divisible by the blk sz
     * (so the conversion does not lose any information).
     * */
    inline uint32_t blk2bytes(uint32_t cnt) const { return assert_u32(cnt << _blk_sz_order); }

    inline uint16_t bytes2blk_cnt(uint32_t bytes) const {
        assert(bytes % _blk_sz == 0);
        return assert_u16(bytes >> _blk_sz_order);
    }

    inline uint16_t bytes2subblk_cnt(uint32_t bytes) const {
        assert(bytes <= _blk_sz);
        assert(bytes % subblk_sz() == 0);
        assert(_blk_sz_order >= Extent::SUBBLK_SIZE_ORDER);
        return assert_u16(bytes >> (_blk_sz_order - Extent::SUBBLK_SIZE_ORDER));
    }

    inline uint32_t bytes2blk_nr(uint32_t bytes) const {
        assert(bytes % _blk_sz == 0);
        return assert_u32(bytes >> _blk_sz_order);
    }

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
     *
     * Callers *should* use allocator().alloc() and allocator().dealloc()
     * to reserve/release space (that may grow/shrink the array if needed).
     **/
    uint32_t /* internal */ grow_by_blocks(uint16_t blk_cnt);
    void /* internal */ shrink_by_blocks(uint32_t blk_cnt);

    /*
     * This method release any pending shrink operation that may
     * exit. The caller should call this once the operations with
     * the block array are done or very sporadically (for example when
     * there is a need to release the space as much as possible).
     *
     * The release implementation is a best effort: it may be possible that
     * some additional blocks are still allocated but not part of the array.
     * If the user needs to know how many blocks are in this condition,
     * it should calculate capacity()-blk_cnt().
     *
     * Returns how many blocks were released.
     *
     * Callers *should* use allocator().release().
     * */
    uint32_t /* internal */ release_blocks();

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

    inline uint32_t capacity() const { return blk_cnt() + (_real_past_end_blk_nr - _past_end_blk_nr); }

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

    struct stats_t {
        // What is the span of blocks (with and without the real past end)
        uint32_t begin_blk_nr;
        uint32_t past_end_blk_nr;
        uint32_t real_past_end_blk_nr;

        // How many blocks are officially exposed? What is the real capacity?
        uint32_t blk_cnt;
        uint32_t capacity;
        uint32_t total_blk_cnt;

        double accessible_blk_sz_kb;
        double capacity_blk_sz_kb;
        double total_blk_sz_kb;

        // Block array parameters: block size and order.
        uint32_t blk_sz;
        uint8_t blk_sz_order;

        // How many times grow() / shrink() / release() were called?
        // And grow_expand_capacity (aka impl_grow_by_blocks()) ?
        uint64_t grow_call_cnt;
        uint64_t grow_expand_capacity_call_cnt;
        uint64_t shrink_call_cnt;
        uint64_t release_call_cnt;
    };

    struct stats_t stats() const;

    friend void PrintTo(const BlockArray& blkarr, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const BlockArray& blkarr);

    virtual ~BlockArray() {}

private:
    void fail_if_block_array_not_initialized() const;
};
