#pragma once

#include <cstdint>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "xoz/blk/block_array.h"
#include "xoz/io/iospan.h"

class Segment;

class FileBlockArray: public BlockArray {
public:
    FileBlockArray(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr = 0);
    FileBlockArray(std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr = 0);
    ~FileBlockArray();

    /*
     * Create a new file block array in the given physical file.
     *
     * If the file exists and fail_if_exists is False, try to open a
     * file there (do not create a new one).
     *
     * The check for the existence of the file and the subsequent creation
     * is not atomic so it may be possible that the file does not exist
     * and by the moment we want to create it some other process already
     * created and we will end up overwriting it.
     *
     * If the file exists and fail_if_exists is True, fail, otherwise
     * create a new file there.
     *
     * There is not check of any kind on the content of the file. If it can
     * be open/created, it is good.
     * */
    static FileBlockArray create(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr = 0,
                                 bool fail_if_exists = false);

    /*
     * Like FileBlockArray::create but make the file be memory based
     * */
    static FileBlockArray create_mem_based(uint32_t blk_sz, uint32_t begin_blk_nr = 0);

    /*
     * Release any block, shrink the file and close it.
     * Note: once a FileBlockArray is closed, it cannot be reopen: you will have
     * to create a new instance of FileBlockArray.
     *
     * The reasoning is for safety: if you have a file A of N blocks and then
     * close and open a file B of M blocks, if we allow the same FileBlockArray object
     * instance to "switch" from A to B, even if FileBlockArray maintains inside
     * a consistent state (it changes from N to M blocks), other objects having
     * a reference to the FileBlockArray object may not be aware of the change.
     *
     * Therefore, we don't support reopen by design.
     * */
    void close();

public:
    /*
     * Return the size in bytes between the begin of the physical file and the begin
     * of the block array (header) and the size in between the end of the block array
     * and the end of the physical file (trailer)
     * */
    uint32_t header_sz() const;
    uint32_t trailer_sz() const;

    /*
     * Write or read the space between the begin of the physical file and the begin
     * of the block array.
     *
     * The buffer must have <exact_sz> bytes of valid data (for writing it) or available
     * space (for reading into).
     *
     * The <exact_sz> cannot be larger that the available space in the header and this
     * space cannot be neither grow nor shrink once the FileBlockArray was created.
     *
     * Both the read and the write involve an access (IO) to the physical file.
     * */
    void write_header(const char* buf, uint32_t exact_sz);
    void read_header(char* buf, uint32_t exact_sz);

    /*
     * Write or read the trailer.
     *
     * The read is limited by how large is the trailer (trailer_sz()) but
     * the write *can* make the trailer grow or shrink, depending of
     * the value <exact_sz> respect to the current trailer_sz().
     * The only restriction is that the trailer size must be smaller
     * than a block size.
     *
     * The trailer is loaded from the physical file at the moment of the opening,
     * so the read_trailer does not incur in any IO operation. The same goes
     * for write_trailer: the method stores in-memory the new trailer without
     * touching the disk, only when the FileBlockArray is closed (method close()
     * or destructor) the trailer is actually written.
     * */
    void write_trailer(const char* buf, uint32_t exact_sz);
    void read_trailer(char* buf, uint32_t exact_sz);

public:
    /*
     * Expose the content of the file as a string in memory.
     * This is only supported for memory-based file block arrays.
     * */
    const std::stringstream& expose_mem_fp() const;

    /*
     * Return the current file size (either for disk-based and for memory-based).
     * Note that this may be larger than (past_end_blk_nr() << blk_sz_order())
     * due pending release blocks and it includes the trailer that it is in the file
     * which may not be the updated version in memory.
     * */
    uint32_t phy_file_sz() const;

    const std::iostream& phy_file_stream() const { return fp; }

protected:
    std::tuple<uint32_t, uint16_t> impl_grow_by_blocks(uint16_t ar_blk_cnt) override;

    uint32_t impl_shrink_by_blocks(uint32_t ar_blk_cnt) override;

    uint32_t impl_release_blocks() override;

    void impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) override;

    void impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) override;

private:
    /*
     * Seek the underlying file for reading (seek_read_phy)
     * and for writing (seek_write_phy).
     *
     * These are aliases for std::istream::seekg and
     * std::ostream::seekp. The names seekg and seekp are
     * *very* similar so it is preferred to call
     * seek_read_phy and seek_write_phy to make it clear.
     *
     * Prefer these 2 aliases.
     *
     * For reading, seek beyond the end of the file is undefined
     * and very likely will end up in a failure.
     *
     * There is no point to do a check here because the seek could
     * be set to a few bytes *before* the end (so no error) but the
     * caller then may read bytes *beyond* the end (so we cannot check
     * it here).
     *
     * For writing, the seek goes beyond the end of the file is
     * also undefined.
     *
     * For disk-based files, the file system may support gaps/holes
     * and it may not fail.
     * For memory-based files, it will definitely fail.
     *
     * It is safe to call may_grow_and_seek_write_phy function
     * instead of seek_write_phy.
     *
     * The function will grow the file to the seek position so
     * it is left at the end, filling with zeros the gap between
     * the new and old end positions.
     *
     * */
    static inline void seek_read_phy(std::istream& fp, std::streamoff offset,
                                     std::ios_base::seekdir way = std::ios_base::beg) {
        fp.seekg(offset, way);
    }

    static inline void seek_write_phy(std::ostream& fp, std::streamoff offset,
                                      std::ios_base::seekdir way = std::ios_base::beg) {
        fp.seekp(offset, way);
    }

    static inline void may_grow_and_seek_write_phy(std::ostream& fp, std::streamoff offset,
                                                   std::ios_base::seekdir way = std::ios_base::beg) {
        // handle holes (seeks beyond the end of the file)
        may_grow_file_due_seek_phy(fp, offset, way);
        fp.seekp(offset, way);
    }

    /*
     * Fill with zeros the space between the end of the file and the seek
     * position if it is beyond the end.
     *
     * This effectively grows the file but no statistics are updated.
     * The file's write pointer is left as it was at the begin of the operation.
     * */
    static void may_grow_file_due_seek_phy(std::ostream& fp, std::streamoff offset,
                                           std::ios_base::seekdir way = std::ios_base::beg);

private:
    // Alias for blk read / write positioning
    inline void seek_read_blk(uint32_t blk_nr, uint32_t offset = 0) {
        seek_read_phy(fp, (blk_nr << blk_sz_order()) + offset);
    }

    inline void seek_write_blk(uint32_t blk_nr, uint32_t offset = 0) {
        seek_write_phy(fp, (blk_nr << blk_sz_order()) + offset);
    }

private:
    /*
     * If the file is disk based open the real file fpath,
     * otherwise, if the file is memory based, initialize it
     * with the given memory stringstream.
     *
     * The boolean is_reopening says if the BlockArray should be initialized
     * or not as part of the opening.
     * */
    void open_internal(const char* fpath, std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr,
                       bool is_reopening);


    /*
     * Create an empty file if it does not exist; truncate if it does
     * */
    static std::fstream _truncate_disk_file(const char* fpath);

    /*
     * Write sz bytes of zeros at the end of the file which effectively
     * extends/grows the file by sz bytes.
     * */
    static void _extend_file_with_zeros(std::iostream& fp, uint64_t sz);

private:
    std::string fpath;

    std::fstream disk_fp;
    std::stringstream mem_fp;
    std::iostream& fp;

    bool closed;
    bool closing;

    std::vector<char> trailer;

    constexpr static const char* IN_MEMORY_FPATH = "@in-memory";
};
