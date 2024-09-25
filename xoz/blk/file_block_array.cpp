#include "xoz/blk/file_block_array.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/mem/asserts.h"
#include "xoz/mem/casts.h"

namespace xoz {
FileBlockArray::FileBlockArray(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr):
        BlockArray(), fpath(fpath), fp(disk_fp), closed(true), closing(false) {
    std::stringstream ignored;
    open_internal(fpath, std::move(ignored), blk_sz, begin_blk_nr, false, nullptr);
    assert(not closed);
}

FileBlockArray::FileBlockArray(std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr):
        BlockArray(), fp(mem_fp), closed(true), closing(false) {
    open_internal(FileBlockArray::IN_MEMORY_FPATH, std::move(mem), blk_sz, begin_blk_nr, false, nullptr);
    assert(not closed);
}

FileBlockArray::FileBlockArray(const char* fpath, FileBlockArray::preload_fn fn):
        BlockArray(), fpath(fpath), fp(disk_fp), closed(true), closing(false) {
    std::stringstream ignored;
    open_internal(fpath, std::move(ignored), 0, 0, false, fn);
    assert(not closed);
}

FileBlockArray::~FileBlockArray() {
    if (not closed) {
        close();
    }
}

std::tuple<uint32_t, uint16_t> FileBlockArray::impl_grow_by_blocks(uint16_t blk_cnt) {
    // BlockArray::grow_by_blocks should had checked for overflow on past_end_blk_nr() + blk_cnt.
    // If not overflow happen, shifting by blk_sz_order() assuming 64 bits should not
    // overflow either
    uint64_t sz = uint64_t(past_end_blk_nr() + blk_cnt) << blk_sz_order();
    may_grow_file_due_seek_phy(fp, assert_streamoff(sz));

    return {past_end_blk_nr(), blk_cnt};
}

uint32_t FileBlockArray::impl_shrink_by_blocks([[maybe_unused]] uint32_t blk_cnt) {
    // We never shrink the file until release_blocks() is explicitly called
    return 0;
}

uint32_t FileBlockArray::impl_release_blocks() {
    uint32_t cnt = capacity() - blk_cnt();
    if (not cnt and not closing) {
        // fast path when there is nothing to release *and* we don't care about
        // the leaving an undefined trailer in the disk because we are not closing the file
        return 0;
    }

    // we do the truncate of the file if either:
    //  - there are blocks to release
    //  - or we are closing the file and we want to be sure that a pre-existing trailer (in disk)
    //    is removed
    auto new_file_sz = past_end_blk_nr() << blk_sz_order();

    if (not is_mem_based()) {
        disk_fp.close();
        std::filesystem::resize_file(fpath, new_file_sz);

        // this is necessary to make open_internal() work
        closed = true;

        std::stringstream ignored;
        open_internal(fpath.data(), std::move(ignored), blk_sz(), begin_blk_nr(), true, nullptr);
        assert(not closed);
    } else {
        // Quite ugly way to "truncate" an in-memory file
        // We copy chunk by chunk to a temporal stringstream
        // until reach the desired "new" file size and then
        // we do a swap (implicit in open_internal()).
        std::stringstream alt_mem_fp;
        mem_fp.seekg(0);

        char buf[128];
        uintmax_t remain = new_file_sz;
        while (remain) {
            const auto chk_sz = std::min(sizeof(buf), remain);
            mem_fp.read(buf, assert_streamsize(chk_sz));
            alt_mem_fp.write(buf, assert_streamsize(chk_sz));

            remain -= chk_sz;
        }

        // this is necessary to make open_internal() work
        closed = true;

        open_internal(FileBlockArray::IN_MEMORY_FPATH, std::move(alt_mem_fp), blk_sz(), begin_blk_nr(), true, nullptr);
    }

    return cnt;
}

void FileBlockArray::impl_read(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    seek_read_blk(blk_nr, offset);
    fp.read(buf, exact_sz);
}

void FileBlockArray::impl_write(uint32_t blk_nr, uint32_t offset, char* buf, uint32_t exact_sz) {
    seek_write_blk(blk_nr, offset);
    fp.write(buf, exact_sz);
}

void FileBlockArray::may_grow_file_due_seek_phy(std::ostream& fp, std::streamoff offset, std::ios_base::seekdir way) {
    // way == way == std::ios_base::end makes no sense for this method
    assert(way == std::ios_base::cur or way == std::ios_base::beg);

    if ((way == std::ios_base::cur and offset > 0) or way == std::ios_base::beg) {
        const auto cur_pos = fp.tellp();

        fp.seekp(0, std::ios_base::end);
        const auto end_pos = fp.tellp();
        const auto ref_pos = way == std::ios_base::beg ? std::streampos(0) : cur_pos;

        // Note: for physical disk-based files we could use truncate/ftruncate
        // or C++ fs::resize_file *but* that will require to close the file and
        // reopen it again. This is an unhappy thing. Also, it does not work for
        // memory-based files.
        if ((ref_pos + offset) > end_pos) {
            const auto hole = assert_u64((ref_pos + offset) - end_pos);
            const char zeros[16] = {0};  // TODO chg buffer size to 128
            for (unsigned batch = 0; batch < hole / sizeof(zeros); ++batch) {
                fp.write(zeros, sizeof(zeros));
            }
            fp.write(zeros, hole % sizeof(zeros));
        }

        // restore the pointer
        fp.seekp(cur_pos, std::ios_base::beg);
    }
}


const std::stringstream& FileBlockArray::expose_mem_fp() const {
    if (not is_mem_based()) {
        throw std::runtime_error("The file block array is not memory backed.");
    }

    return mem_fp;
}

bool FileBlockArray::is_mem_based() const { return (std::addressof(fp) != std::addressof(disk_fp)); }

uint32_t FileBlockArray::phy_file_sz() const {
    seek_read_phy(fp, 0);
    auto begin = fp.tellg();
    seek_read_phy(fp, 0, std::ios_base::end);
    uint64_t sz = assert_u64(fp.tellg() - begin);

    return assert_u32(sz);
}

void FileBlockArray::open_internal(const char* fpath, std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr,
                                   bool is_reopening, FileBlockArray::preload_fn fn) {
    if (not closed) {
        throw std::runtime_error("The current file block array is not closed. You need "
                                 "to close it before opening a new one");
    }

    // Try to avoid raising an exception on the opening so we can
    // raise better exceptions.
    //
    // Enable the exception mask after the open
    fp.exceptions(std::ifstream::goodbit);
    fp.clear();

    if (not is_mem_based()) {
        // note: iostream does not have open() so we must access disk_fp directly
        // but the check above ensure that fp *is* disk_fp
        disk_fp.open(fpath,
                     // in/out binary file stream
                     std::fstream::in | std::fstream::out | std::fstream::binary);
    } else {
        // initialize mem_fp with a new content
        mem_fp = std::move(mem);

        // note: fstream.open implicitly reset the read/write pointers
        // so we emulate the same for the memory based file
        mem_fp.seekg(0, std::ios_base::beg);
        mem_fp.seekp(0, std::ios_base::beg);
    }

    if (!fp) {
        throw OpenXOZError(fpath, "FileBlockArray::open could not open the file. May "
                                  "not exist or may not have permissions.");  // TODO exception
    }

    this->fpath = std::string(fpath);
    auto fp_begin = fp.tellg();

    // Renable the exception mask
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // Calculate the end of the file
    // If it cannot be represented by uint64_t, fail.
    seek_read_phy(fp, 0, std::ios_base::end);
    auto tmp_fp_sz = fp.tellg() - fp_begin;

    // TODO signed or unsigned check?, probably we should go a little less like INT32_MAX-blk_sz()
    if (tmp_fp_sz >= INT32_MAX) {
        throw OpenXOZError(fpath, "the file is huge, it cannot be handled by xoz.");  // TODO exceptions
    }

    assert(tmp_fp_sz >= 0);
    uint32_t fp_sz = uint32_t(tmp_fp_sz);

    {
        // Use these as initial values
        struct blkarr_cfg_t cfg = {
                .blk_sz = blk_sz,
                .begin_blk_nr = begin_blk_nr,
        };

        if (fn) {
            // Call fn, pass cfg by reference to be updated
            fp.seekg(0);
            fp.seekp(0);
            fn(fp, cfg, false);
        }

        // Unpack
        blk_sz = cfg.blk_sz;
        begin_blk_nr = cfg.begin_blk_nr;
    }

    fail_if_bad_blk_sz(blk_sz);
    fail_if_bad_blk_nr(begin_blk_nr);


    uint32_t past_end_blk_nr = fp_sz / blk_sz;  // truncate to integer
    if (begin_blk_nr > past_end_blk_nr) {
        // The file is too small!
        throw std::runtime_error((F() << "File has a size of " << fp_sz << " bytes (" << (fp_sz >> 10) << " kb) "
                                      << "and with blocks of size " << blk_sz << " bytes, it gives a 'past the end' "
                                      << "block number of " << past_end_blk_nr << " that it is lower than "
                                      << "the begin block number " << begin_blk_nr << ".")
                                         .str());
    }

    // Read the trailer only if we are not reopening. If we are reopening assume
    // that the attribute this->trailer already has the trailer loaded (and possibly
    // modified by the user).
    if (not is_reopening) {
        auto _trailer_sz = fp_sz % blk_sz;
        seek_read_phy(fp, -int32_t(_trailer_sz), std::ios_base::end);  // head up: the position is negative

        trailer.resize(_trailer_sz);
        fp.read(trailer.data(), _trailer_sz);

        initialize_block_array(blk_sz, begin_blk_nr, past_end_blk_nr);
    }

    closed = false;
}

void FileBlockArray::_create_initial_block_array_in_disk(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr) {
    std::fstream fp(fpath,
                    // in/out binary file stream
                    std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);

    if (!fp) {
        throw OpenXOZError(fpath, "FileBlockArray::(truncate and create) could not "
                                  "truncate+create the file. May not have permissions.");
    }

    if (begin_blk_nr) {
        uint64_t sz = uint64_t(blk_sz) * uint64_t(begin_blk_nr);
        if (sz > 0xffffffff) {
            throw "";  // TODO
        }

        _extend_file_with_zeros(fp, blk_sz * begin_blk_nr);
    }

    fp.close();
}

void FileBlockArray::_extend_file_with_zeros(std::iostream& fp, uint64_t sz) {
    fp.seekp(0, std::ios_base::end);
    char buf[128] = {0};

    uint64_t blks = (sz >> 7);
    while (blks) {
        fp.write(buf, sizeof(buf));
        --blks;
    }

    uint64_t remain = sz % 128;
    if (remain) {
        fp.write(buf, assert_streamsize(remain));
    }
}

std::unique_ptr<FileBlockArray> FileBlockArray::create(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr,
                                                       bool fail_if_exists) {
    return create_internal(fpath, blk_sz, begin_blk_nr, nullptr, fail_if_exists);
}

std::unique_ptr<FileBlockArray> FileBlockArray::create(const char* fpath, preload_fn fn, bool fail_if_exists) {
    return create_internal(fpath, 0, 0, fn, fail_if_exists);
}

std::unique_ptr<FileBlockArray> FileBlockArray::create_internal(const char* fpath, uint32_t blk_sz,
                                                                uint32_t begin_blk_nr, preload_fn fn,
                                                                bool fail_if_exists) {
    std::fstream test(fpath, std::fstream::in | std::fstream::binary);
    if (test) {
        // File already exists: ...
        if (fail_if_exists) {
            // ... bad we don't want to corrupt a file
            // by mistake. Abort.
            throw OpenXOZError(fpath, "the file already exist and FileBlockArray::create "
                                      "is configured to not override it.");
        } else {
            // ... ok, try to open (the constructor will fail
            // if it cannot open it)
            if (fn) {
                return std::make_unique<FileBlockArray>(fpath, fn);
            } else {
                return std::make_unique<FileBlockArray>(fpath, blk_sz, begin_blk_nr);
            }
        }
    } else {

        // File does not exist: create a new one and the open it
        // Write any header blocks based on begin_blk_nr
        if (fn) {
            // Dummy values, the preload function fn should not read those
            std::stringstream fp;
            struct blkarr_cfg_t cfg = {0, 0};
            fn(fp, cfg, true);
            fail_if_bad_blk_sz(cfg.blk_sz);
            fail_if_bad_blk_nr(cfg.begin_blk_nr);

            _create_initial_block_array_in_disk(fpath, cfg.blk_sz, cfg.begin_blk_nr);
            return std::make_unique<FileBlockArray>(fpath, cfg.blk_sz, cfg.begin_blk_nr);
        } else {
            fail_if_bad_blk_sz(blk_sz);
            fail_if_bad_blk_nr(begin_blk_nr);
            _create_initial_block_array_in_disk(fpath, blk_sz, begin_blk_nr);
            return std::make_unique<FileBlockArray>(fpath, blk_sz, begin_blk_nr);
        }
    }
}

std::unique_ptr<FileBlockArray> FileBlockArray::create_mem_based(uint32_t blk_sz, uint32_t begin_blk_nr) {
    std::stringstream fp;
    _extend_file_with_zeros(fp, begin_blk_nr * blk_sz);
    fp.seekp(0);
    fp.seekg(0);
    return std::make_unique<FileBlockArray>(std::move(fp), blk_sz, begin_blk_nr);
}

void FileBlockArray::close() {
    if (closed)
        return;

    // this will make release_blocks to truncate the file even if no blocks would be released
    // so we can be sure that the pre-existing trailer in disk is removed before we write
    // a new one.
    closing = true;
    release_blocks();

    if (trailer.size() > 0) {
        // note: the seek relays on that the file fp was truncated to a size exactly
        // of the blocks in the array plus the header so we can write the trailer at the end
        fp.seekp(0, std::ios_base::end);
        fp.write(trailer.data(), assert_streamsize(trailer.size()));
    }

    if (not is_mem_based()) {
        disk_fp.close();
    }
    closed = true;
}

void FileBlockArray::panic_close() {
    if (closed)
        return;

    if (not is_mem_based()) {
        disk_fp.close();
    }

    closed = true;
}

bool FileBlockArray::is_closed() const { return closed; }

uint32_t FileBlockArray::header_sz() const { return begin_blk_nr() << blk_sz_order(); }

uint32_t FileBlockArray::trailer_sz() const { return assert_u32(trailer.size()); }

void FileBlockArray::write_header(const char* buf, uint32_t exact_sz) {
    if (exact_sz > header_sz()) {
        throw NotEnoughRoom(exact_sz, header_sz(), "Bad write header");
    }

    fp.seekp(0);
    fp.write(buf, assert_streamsize(exact_sz));
}

void FileBlockArray::read_header(char* buf, uint32_t exact_sz) {
    if (exact_sz > header_sz()) {
        throw NotEnoughRoom(exact_sz, header_sz(), "Bad read header");
    }

    fp.seekg(0);
    fp.read(buf, exact_sz);
}

void FileBlockArray::write_trailer(const char* buf, uint32_t exact_sz) {
    if (exact_sz >= blk_sz()) {
        throw NotEnoughRoom(exact_sz, blk_sz() - 1, "Bad write trailer, trailer must be smaller than the block size");
    }

    trailer.resize(exact_sz);
    memcpy(trailer.data(), buf, exact_sz);
}

void FileBlockArray::read_trailer(char* buf, uint32_t exact_sz) {
    if (exact_sz > trailer_sz()) {
        throw NotEnoughRoom(exact_sz, trailer_sz(), "Bad read trailer");
    }

    memcpy(buf, trailer.data(), exact_sz);
}
}  // namespace xoz
