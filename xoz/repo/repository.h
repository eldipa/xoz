#pragma once

#include <cassert>
#include <cstdint>
#include <fstream>
#include <ios>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "xoz/blk/block_array.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/ext/extent.h"
#include "xoz/parameters.h"
#include "xoz/repo/id_manager.h"
#include "xoz/segm/segment.h"

class Repository: public BlockArray {
private:
    std::string fpath;

    std::fstream disk_fp;
    std::stringstream mem_fp;
    std::iostream& fp;

    bool closed;

    GlobalParameters gp;

    // The size in bytes of the whole repository and it is
    // a multiple of the block size.
    //
    // This include the block 0 which contains the header
    // but it does not contain the trailer
    uint64_t repo_sz;

    // The size of the trailer
    uint64_t trailer_sz;

    // The end position of the file.
    uint64_t fp_end;

    // The total count of blocks reserved in the repository
    // including the block 0.
    // They may or may not be in use.
    uint32_t blk_total_cnt;

    IDManager idmgr;

    // The root segment points to the root descriptor set. This is the
    // root of the file: other descriptor sets can exist being owned
    // by descriptors in the root set. (it works as a tree)
    Segment root_sg;
    std::shared_ptr<DescriptorSet> root_dset;

    Segment external_root_sg_loc;

public:
    // Open a physical file and read/load the repository.
    //
    // If the file does not exist, it cannot be opened for read+write
    // or it contains an invalid repository, fail.
    //
    // To create a new repository, use Repository::create.
    explicit Repository(const char* fpath);

    // Open the repository from an in-memory file given by the iostream.
    // If the in-memory file does not have a valid repository, it will fail.
    //
    // To create a new repository with a memory based file,
    // use Repository::create_mem_based.
    explicit Repository(std::stringstream&& mem);

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
    static Repository create(const char* fpath, bool fail_if_exists = false,
                             const GlobalParameters& gp = GlobalParameters());

    // Like Repository::create but make the repository be memory based
    static Repository create_mem_based(const GlobalParameters& gp = GlobalParameters());

    /*
     * Close the repository and flush any pending write.
     * Multiple calls can be made without trouble.
     *
     * Also, close() is safe to be called for both disk based
     * and memory based repositories.
     *
     * To reopen a repository, you need create a new instance.
     * */
    void close();

    // Call to close()
    ~Repository();

    inline std::shared_ptr<DescriptorSet> root() { return root_dset; }

    inline const GlobalParameters& params() const { return gp; }

    struct stats_t {
        struct BlockArray::stats_t blkarr_st;
    };

    struct stats_t stats() const;

    // Pretty print stats
    std::ostream& print_stats(std::ostream& out) const;

    const std::stringstream& expose_mem_fp() const;

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

    // Fill with zeros the space between the end of the file and the seek
    // position if it is beyond the end.
    //
    // This effectively grows the file but no statistics are updated.
    // The file's write pointer is left as it was at the begin of the operation.
    static void may_grow_file_due_seek_phy(std::ostream& fp, std::streamoff offset,
                                           std::ios_base::seekdir way = std::ios_base::beg);

    // Alias for blk read / write positioning
    inline void seek_read_blk(uint32_t blk_nr, uint32_t offset = 0) {
        assert(blk_nr);
        // TODO assert blk_nr << blk_sz_order does not overflow; and that + offset does not either
        // assert(not u64_add_will_overflow(phy_repo_start_pos, (blk_nr << gp.blk_sz_order) + offset));
        seek_read_phy(fp, (blk_nr << gp.blk_sz_order) + offset);
    }

    inline void seek_write_blk(uint32_t blk_nr, uint32_t offset = 0) {
        assert(blk_nr);
        // TODO
        // assert(not u64_add_will_overflow(phy_repo_start_pos, (blk_nr << gp.blk_sz_order) + offset));
        seek_write_phy(fp, (blk_nr << gp.blk_sz_order) + offset);
    }


    /*
     * Initialize a repository: its block array, its allocator, any index and check for errors or inconsistencies.
     * */
    void bootstrap_repository();

    /*
     * Scan the descriptor sets from the root set to the bottom of the tree.
     * Collect all the segments that are allocated by all the descriptors and descriptor sets.
     * */
    std::list<Segment> scan_descriptor_sets();

    // Initialize  a new repository in the specified file.
    static void _init_new_repository_into(std::iostream& fp, const GlobalParameters& gp);

    // Create an empty file if it does not exist; truncate if it does
    static std::fstream _truncate_disk_file(const char* fpath);

    // Write the header/trailer moving the file pointer
    // to the correct position before.
    //
    // These are static/class method versions to work with
    // Repository::create
    static std::streampos _seek_and_write_header(std::ostream& fp, uint64_t trailer_sz, uint32_t blk_total_cnt,
                                                 const GlobalParameters& gp, const std::vector<uint8_t>& root_sg_bytes);
    static std::streampos _seek_and_write_trailer(std::ostream& fp, uint32_t blk_total_cnt, const GlobalParameters& gp);

    /*
     * Write any pending change in the root descriptor set and update (indirectly) the root segment.
     * If there is enough space in the header to store the root segment, encode the segment
     * in the returned buffer and update external_root_sg_loc to be empty.
     * Otherwise, allocate space outside the header, store the root segment there and update the new allocated
     * external_root_sg_loc that it is this what is written in the returned buffer.
     * */
    std::vector<uint8_t> update_and_encode_root_segment_and_loc();

    /*
     * Write into the returned buffer an empty root segment.
     * This is for being used by _init_new_repository_into() at the moment of a new Repository
     * creation (and therefore with an empty DescriptorSet & Segment).
     * */
    static std::vector<uint8_t> _encode_empty_root_segment();

    // Read the header/trailer moving the file pointer
    // to the correct position and check that the header/trailer
    // is consistent
    //
    // clear_trailer, if true, will override the trailer with zeros
    // after checking it
    void seek_read_and_check_header();
    void seek_read_and_check_trailer(bool clear_trailer);

    // If the repository is disk based open the real file fpath,
    // otherwise, if the repository is memory based, initialize it
    // with the given memory stringstream.
    void open_internal(const char* fpath, std::stringstream&& mem);

    // Read a fully alloc'd and suballoc'd extent.
    // Called from impl_read_extent()
    uint32_t rw_suballocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_read_sz,
                                    uint32_t start);
    uint32_t rw_fully_allocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz,
                                       uint32_t start);

private:
    friend class InconsistentXOZ;
    friend class ExtentOutOfBounds;

    constexpr static const char* IN_MEMORY_FPATH = "@in-memory";

    std::tuple<uint32_t, uint16_t> impl_grow_by_blocks(uint16_t blk_cnt) override;
    uint32_t impl_shrink_by_blocks(uint32_t blk_cnt) override;
    uint32_t impl_release_blocks() override;

    void impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) override;
    void impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) override;

    uint32_t chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start) override;
};
