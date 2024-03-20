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

#include "xoz/alloc/segment_allocator.h"
#include "xoz/blk/block_array.h"
#include "xoz/blk/file_block_array.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/ext/extent.h"
#include "xoz/repo/id_manager.h"
#include "xoz/segm/segment.h"

class Repository {
public:
    struct default_parameters_t {
        uint32_t blk_sz;
    };

    constexpr static struct default_parameters_t DefaultsParameters = {.blk_sz = 512};

    /*
     * This is the minimum size of the blocks that the repository can use.
     * Larger blocks are allowed as long as they are power of 2.
     * */
    constexpr static uint32_t REPOSITORY_MIN_BLK_SZ = 64;
    constexpr static uint32_t REPOSITORY_HEADER_BLK_CNT = 1;

private:
    std::string fpath;

    FileBlockArray fblkarr;

    bool closed;

    // TODO almost all of these variables should gone
    // The size in bytes of the whole repository and it is
    // a multiple of the block size.
    //
    // This include the block 0 which contains the header
    // but it does not contain the trailer
    uint64_t repo_sz;

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
    // Only in this case the default parameters (def) will be used.
    static Repository create(const char* fpath, bool fail_if_exists = false,
                             const struct default_parameters_t& defaults = DefaultsParameters);

    // Like Repository::create but make the repository be memory based
    static Repository create_mem_based(const struct default_parameters_t& defaults = DefaultsParameters);

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


    const std::stringstream& expose_mem_fp() const;

    Repository(const Repository&) = delete;
    Repository& operator=(const Repository&) = delete;

public:
    /*
     * This is only for testing. Don't use it.
     * */
    BlockArray& /* internal - for testing */ expose_block_array() { return fblkarr; }

private:
    /*
     * The given file block array must be a valid one with an opened file.
     * This constructor will grab it and take ownership of it and it will write
     * into to initialize it as a Repository with the given defaults if is_a_new_repository
     * is true.
     * */
    Repository(FileBlockArray&& fblkarr, const struct default_parameters_t& defaults, bool is_a_new_repository);

    /*
     * Initialize a repository: its block array, its allocator, any index and check for errors or inconsistencies.
     * */
    void bootstrap_repository();

    /*
     * Scan the descriptor sets from the root set to the bottom of the tree.
     * Collect all the segments that are allocated by all the descriptors and descriptor sets.
     * */
    std::list<Segment> scan_descriptor_sets();

    /*
     * Initialize freshly new repository backed by an allocated but empty file block array.
     * The array must have allocated space in its header but otherwise nothing else is assumed.
     * This method will perform special write operations to initialize the repository
     * but it will not perform the bootstrap_repository() call.
     * This *must* be made by the caller.
     *
     * The defaults parameters defines with which values initialize the repository.
     * */
    void init_new_repository(const struct default_parameters_t& defaults);

    /*
     * Write the header/trailer.
     * Note that the write may be not flushed to disk depending of the implementation
     * of the file block array.
     * */
    void write_header(const std::vector<uint8_t>& root_sg_bytes);
    void write_trailer();

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

    /*
     * Read the header/trailer and check that the header/trailer
     * is consistent
     * */
    void read_and_check_header_and_trailer();

    // If the repository is disk based open the real file fpath,
    // otherwise, if the repository is memory based, initialize it
    // with the given memory stringstream.
    void open_internal(const char* fpath, std::stringstream&& mem);


public:
    struct stats_t {
        // repository dimensions
        uint64_t capacity_repo_sz;
        double capacity_repo_sz_kb;

        uint64_t in_use_repo_sz;
        double in_use_repo_sz_kb;
        double in_use_repo_sz_rel;

        uint64_t header_sz;
        uint64_t trailer_sz;

        // FileBlockArray and SegmentAllocator own stats
        struct FileBlockArray::stats_t fblkarr_stats;
        struct SegmentAllocator::stats_t allocator_stats;

        // TODO add root descriptor set's stats and descriptors
        // in general, and idmgr's stats
    };

    struct stats_t stats() const;

    friend void PrintTo(const Repository& repo, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const Repository& repo);

private:
    friend class InconsistentXOZ;
    friend class ExtentOutOfBounds;

    /*
     * The preload_repo function defines  the file block array geometry pre-loading the repository
     * and detect if the file was created recently or if not.
     *
     * The ctx is where we pass the default geometry (defaults) and we collect if the file
     * was created or not (was_file_created).
     *
     * See FileBlockArray for more context on how this function is used.
     *
     * The dummy static instance is used for the (internal) cases where no real ctx is needed.
     * */
    struct preload_repo_ctx_t {
        bool was_file_created;
        struct default_parameters_t defaults;
    };

    static struct preload_repo_ctx_t dummy;

    static void preload_repo(struct preload_repo_ctx_t& ctx, std::istream& is, struct FileBlockArray::blkarr_cfg_t& cfg,
                             bool on_create);
};
