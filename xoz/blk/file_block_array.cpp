#include "xoz/blk/file_block_array.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <utility>

#include "xoz/err/exceptions.h"
#include "xoz/mem/bits.h"

FileBlockArray::FileBlockArray(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr):
        BlockArray(), fpath(fpath), fp(disk_fp), closed(true) {
    std::stringstream ignored;
    open_internal(fpath, std::move(ignored), blk_sz, begin_blk_nr, false);
    assert(not closed);
}

FileBlockArray::FileBlockArray(std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr):
        BlockArray(), fp(mem_fp), closed(true) {
    open_internal(FileBlockArray::IN_MEMORY_FPATH, std::move(mem), blk_sz, begin_blk_nr, false);
    assert(not closed);
}

FileBlockArray::~FileBlockArray() { close(); }

std::tuple<uint32_t, uint16_t> FileBlockArray::impl_grow_by_blocks(uint16_t blk_cnt) {
    // BlockArray::grow_by_blocks should had checked for overflow on past_end_blk_nr() + blk_cnt.
    // If not overflow happen, shifting by blk_sz_order() assuming 64 bits should not
    // overflow either
    uint64_t sz = uint64_t(past_end_blk_nr() + blk_cnt) << blk_sz_order();
    may_grow_file_due_seek_phy(fp, sz);

    return {past_end_blk_nr(), blk_cnt};
}

uint32_t FileBlockArray::impl_shrink_by_blocks([[maybe_unused]] uint32_t blk_cnt) {
    // We never shrink the file until release_blocks() is explicitly called
    return 0;
}

uint32_t FileBlockArray::impl_release_blocks() {
    uint32_t cnt = capacity() - blk_cnt();
    if (not cnt) {
        return 0;
    }

    auto file_sz = past_end_blk_nr() << blk_sz_order();

    if (std::addressof(fp) == std::addressof(disk_fp)) {
        disk_fp.close();
        std::filesystem::resize_file(fpath, file_sz);

        // this is necessary to make open_internal() work
        closed = true;

        std::stringstream ignored;
        open_internal(fpath.data(), std::move(ignored), blk_sz(), begin_blk_nr(), true);
        assert(not closed);
    } else {
        // Quite ugly way to "truncate" an in-memory file
        // We copy chunk by chunk to a temporal stringstream
        // until reach the desired "new" file size and then
        // we do a swap (implicit in open_internal()).
        std::stringstream alt_mem_fp;
        mem_fp.seekg(0);

        char buf[128];
        uintmax_t remain = file_sz;
        while (remain) {
            const auto chk_sz = std::min(sizeof(buf), remain);
            mem_fp.read(buf, chk_sz);
            alt_mem_fp.write(buf, chk_sz);

            remain -= chk_sz;
        }

        open_internal(FileBlockArray::IN_MEMORY_FPATH, std::move(alt_mem_fp), blk_sz(), begin_blk_nr(), true);
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
            const auto hole = (ref_pos + offset) - end_pos;
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
    if (std::addressof(fp) == std::addressof(disk_fp)) {
        throw std::runtime_error("The repository is not memory backed.");  // TODO chg name
    }

    return mem_fp;
}

uint32_t FileBlockArray::phy_file_sz() const {
    seek_read_phy(fp, 0);
    auto begin = fp.tellg();
    seek_read_phy(fp, 0, std::ios_base::end);
    auto sz = fp.tellg() - begin;

    assert(0 <= sz and sz < INT32_MAX);
    return uint32_t(sz);
}

void FileBlockArray::open_internal(const char* fpath, std::stringstream&& mem, uint32_t blk_sz, uint32_t begin_blk_nr,
                                   bool is_reopening) {
    if (not closed) {
        throw std::runtime_error("The current repository is not closed. You need "
                                 "to close it before opening a new one");  // TODO chg name
    }

    // Try to avoid raising an exception on the opening so we can
    // raise better exceptions.
    //
    // Enable the exception mask after the open
    fp.exceptions(std::ifstream::goodbit);
    fp.clear();

    if (std::addressof(fp) == std::addressof(disk_fp)) {
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
                                  "not exist or may not have permissions.");  // TODO chg name / exception
    }

    this->fpath = std::string(fpath);

    // Renable the exception mask
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // Calculate the end of the file
    // If it cannot be represented by uint64_t, fail.
    seek_read_phy(fp, 0, std::ios_base::end);
    auto tmp_fp_end = fp.tellg();
    if (tmp_fp_end >=
        INT32_MAX) {  // TODO signed or unsigned check?, probably we should go a little less like INT32_MAX-blk_sz()
        throw OpenXOZError(fpath, "the file is huge, it cannot be handled by xoz.");  // TODO chg name / exceptions
    }

    assert(tmp_fp_end >= 0);
    uint32_t fp_sz = uint32_t(tmp_fp_end);

    if (fp_sz % blk_sz) {
        // File not divisible in an integer number of blocks
        // For now we throw
        throw std::runtime_error("");  // TODO
    }

    uint32_t past_end_blk_nr = fp_sz / blk_sz;
    if (begin_blk_nr > past_end_blk_nr) {
        // The file is too small!
        throw std::runtime_error((F() << "File has a size of " << fp_sz << " bytes (" << (fp_sz >> 10) << " kb) "
                                      << "and with blocks of size " << blk_sz << " bytes, it gives a 'past the end' "
                                      << "block number of " << past_end_blk_nr << " that it is lower than "
                                      << "the begin block number " << begin_blk_nr << ".")
                                         .str());
    }

    if (not is_reopening) {
        initialize_block_array(blk_sz, begin_blk_nr, past_end_blk_nr);
    }

    closed = false;
}

std::fstream FileBlockArray::_truncate_disk_file(const char* fpath) {
    std::fstream fp(fpath,
                    // in/out binary file stream
                    std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc);

    if (!fp) {
        throw OpenXOZError(fpath, "FileBlockArray::(truncate and create) could not "
                                  "truncate+create the file. May not have permissions.");
    }

    return fp;
}

void FileBlockArray::_extend_file_with_zeros(std::fstream& fp, uint64_t sz) {
    fp.seekp(0, std::ios_base::end);
    char buf[128] = {0};

    uint64_t blks = (sz >> 7);
    while (blks) {
        fp.write(buf, sizeof(buf));
        --blks;
    }

    uint64_t remain = sz % 128;
    if (remain) {
        fp.write(buf, remain);
    }
}

FileBlockArray FileBlockArray::create(const char* fpath, uint32_t blk_sz, uint32_t begin_blk_nr, bool fail_if_exists) {
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
            return FileBlockArray(fpath, blk_sz, begin_blk_nr);
        }
    } else {
        // File does not exist: create a new one and the open it
        std::fstream fp = _truncate_disk_file(fpath);
        _extend_file_with_zeros(fp, begin_blk_nr * blk_sz);
        fp.close();  // flush the content make the constructor to open it back
        return FileBlockArray(fpath, blk_sz, begin_blk_nr);
    }
}

FileBlockArray FileBlockArray::create_mem_based(uint32_t blk_sz, uint32_t begin_blk_nr) {
    std::stringstream fp;
    return FileBlockArray(std::move(fp), blk_sz, begin_blk_nr);
}

void FileBlockArray::close() {
    if (closed)
        return;

    release_blocks();
    if (std::addressof(fp) == std::addressof(disk_fp)) {
        disk_fp.close();
    }
    closed = true;
}
