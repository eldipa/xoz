#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "xoz/blk/block_array.h"
#include "xoz/io/iospan.h"

class Segment;

class FileBlockArray: public BlockArray {
public:
    struct blkarr_cfg_t {
        uint32_t blk_sz;
        uint32_t begin_blk_nr;
    };

    /*
     * A function that will be called after opening a disk-based block array (file)
     * but before loading it.
     *
     * The function must fill the cfg object with the correct values about the geometry
     * of the block array.
     * If on_create is false, the function can read from the file (istream) to (possibly)
     * read from the file its own geometry. If on_create is true, the function must not
     * read anything and fill the cfg object with some suitable defaults.
     *
     * The function may throw an exception if it detected some kind of corruption or
     * if the geometry could not be determined.
     *
     * Note: the given stream is read only: the function must not write anything on it
     * If the caller wants to store metadata about the geometry (like after creating the file)
     * he/she must do it after creating/loading the block array.
     * */
    typedef std::function<void(std::istream& is, struct blkarr_cfg_t& cfg, bool on_create)> preload_fn;

public:
    /*
     * Open a given file block array either from a physical file in disk or from
     * an in-memory file.
     *
     * If the file cannot be open (may not exist, may not have the correct read/write permissions)
     * fail. To create a new file see FileBlockArray::create.
     *
     * The geometry of the block array (blk_sz, begin_blk_nr) either must be given by
     * parameter or it is obtained calling with the opened file the preload function.
     * If the preload detects an inconsistency that would prevent the block array
     * from being used, it must raise an exception.
     * */
    FileBlockArray(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr = 0);
    FileBlockArray(std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr = 0);

    FileBlockArray(const char* fpath, preload_fn fn);

    ~FileBlockArray();

    /*
     * Create a new file block array in the given physical file.
     *
     * If the file exists and fail_if_exists is False, try to open a
     * file there (do not create a new one). Use fn to define the block
     * array geometry (if given) or pass it explicitly with begin_blk_nr
     * and begin_blk_nr.
     *
     * The check for the existence of the file and the subsequent creation
     * is not atomic so it may be possible that the file does not exist
     * and by the moment we want to create it some other process already
     * created and we will end up overwriting it.
     *
     * If the file exists and fail_if_exists is True, fail, otherwise
     * create a new file there. In this case, fn must return a default
     * settings (it will receive the file size which should be zero in this case;
     * there is no way to distinguish an pre-existing empty file from a new one)
     *
     * There is not check of any kind on the content of the file. If it can
     * be open/created, it is good. If the preload function is used, it may perform
     * a simplified initial check and abort the opening by raising an exception.
     * */
    static std::unique_ptr<FileBlockArray> create(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr = 0,
                                                  bool fail_if_exists = false);

    static std::unique_ptr<FileBlockArray> create(const char* fpath, preload_fn fn, bool fail_if_exists = false);
    /*
     * Like FileBlockArray::create but make the file be memory based.
     * In this case there is no possible to "open" a preexisting file so this
     * is truly a create-only method.
     * */
    static std::unique_ptr<FileBlockArray> create_mem_based(uint32_t blk_sz, uint32_t begin_blk_nr = 0);

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
     *
     * Also, once it is closed, calling any other method except the destructor
     * or is_closed() is undefined.
     * */
    void close();
    bool is_closed() const;

    /*
     * Construction/assignation by movement/by copy are not allowed.
     * */
    FileBlockArray(FileBlockArray&&) = delete;
    FileBlockArray(const FileBlockArray&) = delete;
    FileBlockArray& operator=(const FileBlockArray&) = delete;
    FileBlockArray& operator=(FileBlockArray&&) = delete;

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
     * Return true if the block array is based on memory or false if it is based on disk.
     * */
    bool is_mem_based() const;

    /*
     * Return the current file size (either for disk-based and for memory-based).
     * Note that this may be larger than (past_end_blk_nr() << blk_sz_order())
     * due pending release blocks and it includes the trailer that it is in the file
     * which may not be the updated version in memory.
     * */
    uint32_t phy_file_sz() const;

    const std::iostream& phy_file_stream() const { return fp; }

    /*
     * Return the file path of the disk-based file was either created
     * or opened at. If the block array is not disk-based but memory-based,
     * the path returned is a symbolic name and not a real file-system path
     * (use it only for printing).
     *
     * Caller *must* check is_mem_based() to see of the block array
     * is memory or disk based and if the path is or not a valid
     * file-system path.
     *
     * The string returned will be valid as long as the FileBlockArray instance is valid.
     * If the caller wants to keep a valid value that outlives the instance lifetime,
     * it must perform a copy.
     * */
    const std::string& get_file_path() const { return fpath; }

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
                       bool is_reopening, FileBlockArray::preload_fn fn);

    /*
     * See the documentation of create()
     * */
    static std::unique_ptr<FileBlockArray> create_internal(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr,
                                                           preload_fn fn, bool fail_if_exists);

    /*
     * Create an "empty" block array if the does not exist; truncate the file if it does.
     * The resulting file will not be empty if the begin_blk_nr is non zero.
     * */
    static void _create_initial_block_array_in_disk(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr);

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
