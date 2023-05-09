#pragma once

#include "xoz/parameters.h"
#include "xoz/extent.h"

#include <ios>
#include <fstream>
#include <cstdint>
#include <cassert>

class Repository {
    private:
        const char* fpath;
        std::fstream fp;
        bool closed;

        GlobalParameters gp;

        // In which position of the physical file the repository
        // begins (in bytes).
        std::streampos repo_start_pos;

        // In which position of the physical file the repository
        // ends (in bytes).
        // It is calculated from repo_start_pos plus the repo_sz
        //
        // Note: the physical file may extend beyond this position
        // and it may or may not be resized (shrink / truncate) when
        // the repository gets closed.
        std::streampos repo_end_pos;

        // The size in bytes of the whole repository.
        uint64_t repo_sz;

        // The total count of blocks reserved in the repository
        // including the block 0.
        // They may or may not be in use.
        uint32_t blk_total_cnt;

    public:
        // Open a physical file and read/load the repository from there
        // starting at the byte repo_start_pos.
        //
        // If the file does not exist, it cannot be opened for read+write
        // or it contains an invalid repository, fail.
        //
        // Te create a new repository, use Repository::create.
        Repository(const char* fpath, uint32_t repo_start_pos = 0);

        // Create a new repository in the given physical file.
        //
        // If the file exists and fail_if_exists is False, try to open a
        // repository there (do not create a new one).
        //
        // During the open the repository will be checked and if
        // something does not look right, the open will fail.
        //
        // The check for the existence of the file and the subsequent creation
        // is not atomic so it may be possible that the file does not exist
        // and by the moment we want to create it some other process already
        // created and we will end up overwriting it.
        //
        // If the file exists and fail_if_exists is True, fail, otherwise
        // create a new file and a repository there.
        //
        // Only in this case the global parameters (gp) will be used.
        static Repository create(const char* fpath, bool fail_if_exists = false, uint32_t repo_start_pos = 0, const GlobalParameters& gp = GlobalParameters());

        // Close the repository and flush any pending write.
        // Multiple calls can be made without trouble.
        void close();

        // Call to close()
        ~Repository();


        inline const GlobalParameters& params() const {
            return gp;
        }

        // Main primitive to allocate / free blocks
        //
        // This expands/shrinks the underlying physical file.
        //
        // alloc_blocks() returns the block number of the first
        // new allocated blocks.
        uint32_t alloc_blocks(uint16_t blk_cnt);

        void free_blocks(uint16_t blk_cnt);

        // Pretty print stats
        std::ostream& print_stats(std::ostream& out) const;


        // Read / write <blk_cnt> consecutive blocks starting from the given
        // <blk_nr>.
        //
        // The data's buffer to read into / write from <blk_data> must be
        // allocated by the caller.
        //
        // The physical space in-disk from which we are reading / writing must
        // be previously allocated.
        //
        // Reading / writing out of bounds may succeed *but* it is undefined
        // and it will probably lead to corruption.
        inline void read_blks(uint32_t blk_nr, uint16_t blk_cnt, char* blk_data) {
            assert (blk_nr > 0);
            assert (blk_cnt > 0);
            assert (blk_nr + blk_cnt <= blk_total_cnt);

            seek_read_blk(blk_nr);
            fp.read(blk_data, blk_cnt << gp.blk_sz_order);
        }

        inline void write_blks(uint32_t blk_nr, uint16_t blk_cnt, const char* blk_data) {
            assert (blk_nr > 0);
            assert (blk_cnt > 0);
            assert (blk_nr + blk_cnt <= blk_total_cnt);

            seek_write_blk(blk_nr);
            fp.write(blk_data, blk_cnt << gp.blk_sz_order);
        }

        // The same but using the Extent structure
        inline void read_blks(const Extent& ext, char* blk_data) {
            return read_blks(ext.blk_nr, ext.blk_cnt, blk_data);
        }

        inline void write_blks(const Extent& ext, const char* blk_data) {
            return write_blks(ext.blk_nr, ext.blk_cnt, blk_data);
        }

        Repository(const Repository&) = delete;
        Repository& operator=(const Repository&) = delete;

    private:
        // Alias for fp.seekg (get / read) and fp.seekp (put / write)
        // Honestly, it is too easy to confuse those two and set the
        // reading pointer (seekg) thinking that you are setting the
        // write pointer (seekp).
        //
        // Prefer these 4 aliases.
        inline void seek_read_phy(std::streampos pos) {
            fp.seekg(pos);
        }

        inline void seek_read_phy(std::streamoff offset, std::ios_base::seekdir way) {
            fp.seekg(offset, way);
        }

        inline void seek_write_phy(std::streampos pos) {
            fp.seekp(pos);
        }

        inline void seek_write_phy(std::streamoff offset, std::ios_base::seekdir way) {
            fp.seekp(offset, way);
        }

        // Alias for blk read / write positioning
        inline void seek_read_blk(uint32_t blk_nr) {
            assert(blk_nr);
            seek_read_phy((blk_nr << gp.blk_sz_order) + repo_start_pos);
        }

        inline void seek_write_blk(uint32_t blk_nr) {
            assert(blk_nr);
            seek_read_phy((blk_nr << gp.blk_sz_order) + repo_start_pos);
        }

        void expand_phy_bytes(uint32_t sz);

        void shrink_phy_bytes(uint32_t sz);


        void load_global_parameters();

        void store_global_parameters();

        static void _store_global_parameters(std::fstream& fp, uint64_t repo_sz, uint32_t blk_total_cnt, const GlobalParameters& gp);

        static void _create_new_repository(const char* fpath, uint32_t repo_start_pos, const GlobalParameters& gp);
};


namespace {

    // In-disk repository's header
    struct repo_header_t {
        // It should be "XOZ" followed by a NUL
        uint8_t magic[4];

        // Log base 2 of the block size in bytes
        // Order of 10 means block size of 1KB,
        // order of 11 means block size of 2KB, and so on
        uint8_t blk_sz_order;

        // Size of the whole repository, including the header
        // (and trailer?), in bytes
        uint64_t repo_sz;

        // Count of blocks in the repo.
        // It should be greater than or equal to repo_sz/blk_sz
        uint32_t blk_total_cnt;

    } __attribute__ ((aligned (1)));

    struct repo_eof_t {
        // It should be "EOF" followed by a NUL
        uint8_t magic[4];
    } __attribute__ ((aligned (1)));
}
