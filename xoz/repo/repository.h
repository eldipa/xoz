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
#include "xoz/blk/file_block_array.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/ext/extent.h"
#include "xoz/parameters.h"
#include "xoz/repo/id_manager.h"
#include "xoz/segm/segment.h"

class Repository {
private:
    std::string fpath;

    FileBlockArray fblkarr;

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
    // TODO rm? explicit Repository(std::stringstream&& mem);

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

    // Block definition (TODO)
    inline uint32_t subblk_sz() const { return fblkarr.subblk_sz(); }
    inline uint32_t blk_sz() const { return fblkarr.blk_sz(); }
    inline uint8_t blk_sz_order() const { return fblkarr.blk_sz_order(); }
    inline uint32_t begin_blk_nr() const { return fblkarr.begin_blk_nr(); }
    inline uint32_t past_end_blk_nr() const { return fblkarr.past_end_blk_nr(); }
    inline uint32_t blk_cnt() const { return fblkarr.blk_cnt(); }
    inline uint32_t capacity() const { return fblkarr.capacity(); }


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

public:
    /*
     * These two are for testing only. Don't use it.
     * */
    uint32_t /* internal - for testing */ _grow_by_blocks(uint16_t blk_cnt);
    void /* internal - for testing */ _shrink_by_blocks(uint16_t blk_cnt);

private:
    /*
     * The given file block array must be a valid one with an opened file.
     * This constructor will grab it and take ownership of it and it will write
     * into to initialize it as a Repository with the given gp defaults if is_a_new_repository
     * is true.
     * */
    Repository(FileBlockArray&& fblkarr, const GlobalParameters& gp, bool is_a_new_repository);

    /*
     * Initialize a repository: its block array, its allocator, any index and check for errors or inconsistencies.
     * */
    void bootstrap_repository();

    /*
     * Scan the descriptor sets from the root set to the bottom of the tree.
     * Collect all the segments that are allocated by all the descriptors and descriptor sets.
     * */
    std::list<Segment> scan_descriptor_sets();

    // Initialize  a new repository in the specified file. TODO
    void _init_new_repository(const GlobalParameters& gp);

    // Write the header/trailer TODO
    //
    // These are static/class method versions to work with
    // Repository::create TODO
    void _write_header(uint64_t trailer_sz, uint32_t blk_total_cnt, const GlobalParameters& gp,
                       const std::vector<uint8_t>& root_sg_bytes);
    void _write_trailer();

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

    // Read the header/trailer and check that the header/trailer
    // is consistent
    //
    // clear_trailer, if true, will override the trailer with zeros
    // after checking it
    void read_and_check_header();
    void read_and_check_trailer(bool clear_trailer);

    // If the repository is disk based open the real file fpath,
    // otherwise, if the repository is memory based, initialize it
    // with the given memory stringstream.
    void open_internal(const char* fpath, std::stringstream&& mem);


public:
    struct preload_repo_ctx_t {
        bool was_file_created;
        GlobalParameters gp;
    };

private:
    friend class InconsistentXOZ;
    friend class ExtentOutOfBounds;

    constexpr static const char* IN_MEMORY_FPATH = "@in-memory";

    uint32_t chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start);

    /*
     * Function to retrieve the file block array geometry pre-loading the repository.
     * See FileBlockArray
     *
     * The on_create_defaults argument is a read-only struct passed by copy
     * that must be read only if on_create is true, otherwise it is undefined.
     * TODO
     * */
    static void preload_repo(struct preload_repo_ctx_t& ctx, std::istream& is, struct FileBlockArray::blkarr_cfg_t& cfg,
                             bool on_create);
};
