#pragma once

#include "xoz/parameters.h"
#include "xoz/extent.h"

#include <ios>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cassert>


class Repository {
    private:
        const char* fpath;

        std::fstream disk_fp;
        std::stringstream mem_fp;
        std::iostream& fp;

        bool closed;

        GlobalParameters gp;

        // In which position of the physical file the repository
        // begins (in bytes).
        uint64_t phy_repo_start_pos;

        // In which position of the physical file the repository
        // ends (in bytes).
        // It is calculated from phy_repo_start_pos plus the repo_sz
        //
        // Note: the physical file may extend beyond this position
        // and it may or may not be resized (shrink / truncate) when
        // the repository gets closed.
        uint64_t phy_repo_end_pos;

        // The size in bytes of the whole repository and it is
        // a multiple of the block size.
        //
        // This include the block 0 which contains the header
        // but it does not contain the trailer
        uint64_t repo_sz;

        // The size of the trailer
        uint64_t trailer_sz;

        // The end position of the file. It should be such
        // phy_repo_start_pos < phy_repo_end_pos <= fp_end
        uint64_t fp_end;

        // The total count of blocks reserved in the repository
        // including the block 0.
        // They may or may not be in use.
        uint32_t blk_total_cnt;

    public:
        // Open a physical file and read/load the repository from there
        // starting at the byte phy_repo_start_pos.
        //
        // If the file does not exist, it cannot be opened for read+write
        // or it contains an invalid repository, fail.
        //
        // To create a new repository, use Repository::create.
        Repository(const char* fpath, uint64_t phy_repo_start_pos = 0);

        // Open the repository from an in-memory file given by the iostream.
        // If the in-memory file does not have a valid repository, it will fail.
        //
        // To create a new repository with a memory based file,
        // use Repository::create_mem_based.
        Repository(std::stringstream&& mem, uint64_t phy_repo_start_pos = 0);

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
        static Repository create(const char* fpath, bool fail_if_exists = false, uint64_t phy_repo_start_pos = 0, const GlobalParameters& gp = GlobalParameters());

        // Like Repository::create but make the repository be memory based
        static Repository create_mem_based(uint64_t phy_repo_start_pos = 0, const GlobalParameters& gp = GlobalParameters());

        // Open a repository encoded in the given physical file at the
        // given offset (by default, 0).
        //
        // If the file does not exist, it will fail.
        // If it exists but not a valid repository is there, it will fail.
        //
        // If the repository is already open, it will fail. You must
        // call Repository::close before.
        //
        // It is an error also call open on a memory based repository.
        void open(const char* fpath, uint64_t phy_repo_start_pos = 0);

        // Close the repository and flush any pending write.
        // Multiple calls can be made without trouble.
        //
        // Also, close() is safe to be called for both disk based
        // and memory based repositories.
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
        uint32_t grow_by_blocks(uint16_t blk_cnt);

        void shrink_by_blocks(uint32_t blk_cnt);

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
        //
        // Suballocation is not supported neither for reading nor writing.
        void read_full_blks(const Extent& ext, char* blk_data) {
            assert (not ext.is_suballoc()); // TODO, exception?
            assert (ext.blk_nr() > 0); // TODO, exception?
            if (ext.blk_cnt() == 0) // TODO, exception?
                return;

            // Check this invariant only after checking is_suballoc is false
            assert (ext.blk_nr() + ext.blk_cnt() <= blk_total_cnt);

            seek_read_blk(ext.blk_nr());
            fp.read(blk_data, ext.blk_cnt() << gp.blk_sz_order);
        }

        void write_full_blks(const Extent& ext, const char* blk_data) {
            assert (not ext.is_suballoc()); // TODO, exception?
            assert (ext.blk_nr() > 0); // TODO, exception?
            if (ext.blk_cnt() == 0) // TODO, exception?
                return;

            // Check this invariant only after checking is_suballoc is false
            assert (ext.blk_nr() + ext.blk_cnt() <= blk_total_cnt);

            seek_write_blk(ext.blk_nr());
            fp.write(blk_data, ext.blk_cnt() << gp.blk_sz_order);
        }

        Repository(const Repository&) = delete;
        Repository& operator=(const Repository&) = delete;

    private:
        // Seek the underlying file for reading (seek_read_phy)
        // and for writing (seek_write_phy).
        //
        // These are aliases for std::istream::seekg and
        // std::ostream::seekp. The names seekg and seekp are
        // *very* similar so it is preferred to call
        // seek_read_phy and seek_write_phy to make it clear.
        //
        // Prefer these 2 aliases.
        //
        // For reading, seek beyond the end of the file is undefined
        // and very likely will end up in a failure.
        //
        // There is no point to do a check here because the seek could
        // be set to a few bytes *before* the end (so no error) but the
        // caller then may read bytes *beyond* the end (so we cannot check
        // it here).
        //
        // For writing, the seek goes beyond the end of the file is
        // also undefined.
        //
        // For disk-based files, the file system may support gaps/holes
        // and it may not fail.
        // For memory-based files, it will definitely fail.
        //
        // It is safe to call may_grow_and_seek_write_phy function
        // instead of seek_write_phy.
        //
        // The function will grow the file to the seek position so
        // it is left at the end, filling with zeros the gap between
        // the new and old end positions.
        static inline void seek_read_phy(std::istream& fp, std::streamoff offset, std::ios_base::seekdir way = std::ios_base::beg) {
            fp.seekg(offset, way);
        }

        static inline void seek_write_phy(std::ostream& fp, std::streamoff offset, std::ios_base::seekdir way = std::ios_base::beg) {
            fp.seekp(offset, way);
        }

        static inline void may_grow_and_seek_write_phy(std::ostream& fp, std::streamoff offset, std::ios_base::seekdir way = std::ios_base::beg) {
            // handle holes (seeks beyond the end of the file)
            may_grow_file_due_seek_phy(fp, offset, way);
            fp.seekp(offset, way);
        }

        // Fill with zeros the space between the end of the file and the seek
        // position if it is beyond the end.
        //
        // This effectively grows the file but no statistics are updated.
        // The file's write pointer is left as it was at the begin of the operation.
        static void may_grow_file_due_seek_phy(std::ostream& fp, std::streamoff offset, std::ios_base::seekdir way = std::ios_base::beg);

        // Alias for blk read / write positioning
        inline void seek_read_blk(uint32_t blk_nr, uint32_t offset = 0) {
            assert(blk_nr);
            seek_read_phy(fp, (blk_nr << gp.blk_sz_order) + phy_repo_start_pos + offset);
        }

        inline void seek_write_blk(uint32_t blk_nr, uint32_t offset = 0) {
            assert(blk_nr);
            seek_read_phy(fp, (blk_nr << gp.blk_sz_order) + phy_repo_start_pos + offset);
        }

        // Initialize  a new repository in the specified file.
        static void _init_new_repository_into(std::iostream& fp, uint64_t phy_repo_start_pos, const GlobalParameters& gp);

        // Create an empty file if it does not exist; truncate if it does
        static std::fstream _truncate_disk_file(const char* fpath);

        // Write the header/trailer moving the file pointer
        // to the correct position before.
        //
        // These are static/class method versions to work with
        // Repository::create
        static std::streampos _seek_and_write_header(std::ostream& fp, uint64_t phy_repo_start_pos, uint64_t trailer_sz, uint32_t blk_total_cnt, const GlobalParameters& gp);
        static std::streampos _seek_and_write_trailer(std::ostream& fp, uint64_t phy_repo_start_pos, uint32_t blk_total_cnt, const GlobalParameters& gp);

        // Read the header/trailer moving the file pointer
        // to the correct position and check that the header/trailer
        // is consistent
        void seek_read_and_check_header();
        void seek_read_and_check_trailer();

        // Open the given file *iff* the repository is disk based otherwise
        // reset the memory based file (and path can be any symbolic name)
        void open_internal(const char* fpath, uint64_t phy_repo_start_pos);

    private:
        friend class InconsistentXOZ;
};

