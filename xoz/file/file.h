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
#include "xoz/file/runtime_config.h"
#include "xoz/file/runtime_context.h"
#include "xoz/segm/segment.h"

namespace xoz {
class File {
public:
    struct default_parameters_t {
        uint32_t blk_sz;
    };

    constexpr static struct default_parameters_t DefaultsParameters = {.blk_sz = 128};

    /*
     * This is the minimum size of the blocks that the xoz file can use.
     * Larger blocks are allowed as long as they are power of 2.
     *
     * Max block size order is 16. This is the largest order such than an Extent
     * with its maximum number of blocks has a total size less or equal to uint32_t.
     * */
    constexpr static uint32_t MIN_BLK_SZ_ORDER = 7;
    constexpr static uint32_t MAX_BLK_SZ_ORDER = 16;
    constexpr static uint32_t MIN_BLK_SZ = (1 << MIN_BLK_SZ_ORDER);
    constexpr static uint32_t MAX_BLK_SZ = (1 << MAX_BLK_SZ_ORDER);
    constexpr static uint32_t HEADER_BLK_CNT = 1;

private:
    std::string fpath;

    std::unique_ptr<FileBlockArray> fblkarr;

    bool closed;
    bool closing;

    RuntimeContext rctx;

    Segment trampoline_segm;
    std::shared_ptr<DescriptorSet> root_set;

    uint32_t feature_flags_compat;
    uint32_t feature_flags_incompat;
    uint32_t feature_flags_ro_compat;

public:
    // Open a physical file and read/load the xoz file.
    //
    // If the file does not exist, it cannot be opened for read+write
    // or it contains an invalid xoz file, fail.
    //
    // To create a new xoz file, use File::create.
    explicit File(const DescriptorMapping& dmap, const char* fpath,
                  const struct runtime_config_t& runcfg = DefaultRuntimeConfig);

    // Create a new xoz file in the given physical file.
    //
    // If the file exists and fail_if_exists is False, try to open a
    // xoz file there (do not create a new one).
    //
    // During the open the xoz file will be checked and if
    // something does not look right, the open will fail.
    //
    // The check for the existence of the file and the subsequent creation
    // is not atomic so it may be possible that the file does not exist
    // and by the moment we want to create it some other process already
    // created and we will end up overwriting it.
    //
    // If the file exists and fail_if_exists is True, fail, otherwise
    // create a new file and a xoz file there.
    //
    // Only in this case the default parameters (def) will be used.
    static File create(const DescriptorMapping& dmap, const char* fpath, bool fail_if_exists = false,
                       const struct default_parameters_t& defaults = DefaultsParameters,
                       const struct runtime_config_t& runcfg = DefaultRuntimeConfig);

    // Like File::create but make the xoz file be memory based
    static File create_mem_based(const DescriptorMapping& dmap,
                                 const struct default_parameters_t& defaults = DefaultsParameters,
                                 const struct runtime_config_t& runcfg = DefaultRuntimeConfig);

    /*
     * Close the xoz file and flush any pending write.
     * Multiple calls can be made without trouble.
     *
     * Also, close() is safe to be called for both disk based
     * and memory based xoz files.
     *
     * To reopen a xoz file, you need create a new instance.
     *
     * The close() will write any pending change in the root descriptor set and may
     * require allocate additional space (the trampoline space) to save the root set
     * if it does not fit in the header.
     * */
    void close();

    /*
     * Close the xoz file abruptly without flushing any pending write.
     * This should be called as a last resort if a call to close() failed.
     * */
    void panic_close();

    /*
     * Flush any pending write.
     * */
    void full_sync(const bool release);

    // Call to close()
    ~File();

    inline std::shared_ptr<DescriptorSet> root() { return root_set; }

    const std::stringstream& expose_mem_fp() const;

    File(const File&) = delete;
    File& operator=(const File&) = delete;

public:
    /*
     * This is only for testing. Don't use it.
     * TODO this probably is not for testing at all!
     * */
    BlockArray& /* internal - for testing */ expose_block_array() { return *fblkarr.get(); }
    RuntimeContext& /* internal - for testing */ expose_runtime_context() { return rctx; }

private:
    /*
     * The given file block array must be a valid one with an opened file.
     * This constructor will grab it and take ownership of it and it will write
     * into to initialize it as a File with the given defaults if is_a_new_file
     * is true.
     * */
    File(const DescriptorMapping& dmap, std::unique_ptr<FileBlockArray>&& fblkarr_ptr,
         const struct default_parameters_t& defaults, bool is_a_new_file, const struct runtime_config_t& runcfg);

    /*
     * Initialize a xoz file: its block array, its allocator, any index and check for errors or inconsistencies.
     * */
    void bootstrap_file();

    /*
     * Scan the descriptor sets from the root set to the bottom of the tree.
     * Collect all the segments that are allocated by all the descriptors and descriptor sets.
     * */
    std::list<Segment> scan_descriptor_sets();

    /*
     * Initialize freshly new xoz file backed by an allocated but empty file block array.
     * The array must have allocated space in its header but otherwise nothing else is assumed.
     * This method will perform special write operations to initialize the xoz file
     * but it will not perform the bootstrap_file() call.
     * This *must* be made by the caller.
     *
     * The defaults parameters defines with which values initialize the xoz file.
     * */
    void init_new_file(const struct default_parameters_t& defaults);

    /*
     * Write the header/trailer.
     * Note that the write may be not flushed to disk depending of the implementation
     * of the file block array.
     * */
    void write_header();
    void write_trailer();

    /*
     * Read the header/trailer and check that the header/trailer
     * is consistent
     * */
    void read_and_check_header_and_trailer();

    // If the xoz file is disk based open the real file fpath,
    // otherwise, if the xoz file is memory based, initialize it
    // with the given memory stringstream.
    void open_internal(const char* fpath, std::stringstream&& mem);


public:
    struct stats_t {
        // xoz file dimensions
        uint64_t capacity_file_sz;
        double capacity_file_sz_kb;

        uint64_t in_use_file_sz;
        double in_use_file_sz_kb;
        double in_use_file_sz_rel;

        uint64_t header_sz;
        uint64_t trailer_sz;

        // FileBlockArray and SegmentAllocator own stats
        struct FileBlockArray::stats_t fblkarr_stats;
        struct SegmentAllocator::stats_t allocator_stats;

        // TODO add root descriptor set's stats and descriptors
        // in general, and idmgr's stats
    };

    struct stats_t stats() const;

    friend void PrintTo(const File& xfile, std::ostream* out);
    friend std::ostream& operator<<(std::ostream& out, const File& xfile);

private:
    friend class InconsistentXOZ;
    friend class ExtentOutOfBounds;

    /*
     * The preload_file function defines  the file block array geometry pre-loading the xoz file
     * and detect if the file was created recently or if not.
     *
     * The ctx is where we pass the default geometry (defaults) and we collect if the file
     * was created or not (was_file_created).
     *
     * See FileBlockArray for more context on how this function is used.
     *
     * The dummy static instance is used for the (internal) cases where no real ctx is needed.
     * */
    struct preload_file_ctx_t {
        bool was_file_created;
        struct default_parameters_t defaults;
    };

    static struct preload_file_ctx_t dummy;

    static void preload_file(struct preload_file_ctx_t& ctx, std::istream& is, struct FileBlockArray::blkarr_cfg_t& cfg,
                             bool on_create);

private:
    // In-disk xoz file's header
    struct file_header_t {
        // It should be "XOZ" followed by a NUL
        uint8_t magic[4];

        // This is the application name using xoz.
        // For Xournal++ it could be "Xournal++" but
        // the exact value is up to the application.
        // It may be NULL terminated but it is not required.
        uint8_t app_name[12];

        // Size of the whole xoz file, including the header
        // but not the trailer, in bytes. It is a multiple
        // of the block total count
        uint64_t file_sz;

        // The size in bytes of the trailer
        uint16_t trailer_sz;

        // Count of blocks in the xfile.
        // It should be equal to file_sz/blk_sz
        uint32_t blk_total_cnt;

        // Log base 2 of the block size in bytes
        // Order of 10 means block size of 1KB,
        // order of 11 means block size of 2KB, and so on
        uint8_t blk_sz_order;

        // Flags to control certain aspects of the xoz file
        uint8_t flags;

        // Feature flags. If the xoz library does not recognize one of those bits
        // it may or may not proceed reading. In specific:
        //
        // - if the unknown bit is in feature_flags_compat, it should be safe for
        //   the library to read and write the xoz file
        // - if the unknown bit is in feature_flags_incompat, the library must
        //   not read further and do not write anything.
        // - if the unknown bit is in feature_flags_ro_compat, the library can
        //   read the file bit it cannot write/update it.
        uint32_t feature_flags_compat;
        uint32_t feature_flags_incompat;
        uint32_t feature_flags_ro_compat;

        // This is where we store the "root" of the file. This can be a DescriptorSet
        // serialized here *or* a segment that points to somewhere else outside the xoz header
        // where the DescriptorSet lives.
        // See load_root_set() and write_root_set() methods.
        //
        // TODO ensure that the read and write preserves this "padding" for backward/forward compat
        // in the case of the root field not being fully used
        uint8_t root[32];

        // Inet checksum of the header, including the padding.
        uint16_t checksum;

        // TODO ensure that the read and write preserves this "padding" for backward/forward compat
        uint8_t padding[50];
    } __attribute__((packed));

    // In-disk xoz file's trailer
    struct file_trailer_t {
        // It should be "EOF" followed by a NUL
        uint8_t magic[4];
    } __attribute__((packed));

    static_assert(sizeof(struct file_header_t) == 128);

private:
    /*
     * Read and load the root descriptor set, root of the rest of the xoz content.
     *
     * If the has_trampoline is true, the root field of the xoz file header points
     * to another part of the file where the set descriptor is stored.
     * Otherwise, the set descriptor is read directly from the root field.
     *
     * In any case, the attribute root_set is initialized with a DescriptorSet object
     * and the attribute trampoline_segm with the segment that points to the allocated trampoline
     * blocks. In the case of has_trampoline equals false, this segment will be empty.
     * */
    void load_root_set(struct file_header_t& hdr);
    void write_root_set(uint8_t* rootbuf, const uint32_t rootbuf_sz, uint8_t& flag);
    void update_trampoline_space();

private:
    static void check_header_magic(struct file_header_t& hdr);
    static uint16_t compute_header_checksum(struct file_header_t& hdr);
    static void compute_and_check_header_checksum(struct file_header_t& hdr);
    static void check_blk_sz_order(const uint8_t blk_sz_order);

public:
    static constexpr auto HEADER_ROOT_SET_SZ = sizeof(static_cast<struct file_header_t*>(nullptr)->root);
    static_assert(HEADER_ROOT_SET_SZ >= 32);

    Segment /* testing */ trampoline_segment() const { return trampoline_segm; }
};
}  // namespace xoz
