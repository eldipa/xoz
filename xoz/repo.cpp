#include "xoz/repo.h"
#include "xoz/arch.h"
#include "xoz/exceptions.h"
#include <cstring>
#include <filesystem>

#define MAX_SIGNED_INT64 int64_t((~((uint64_t)0)) >> 1)

#define MIN(x,y) (((x) < (y)) ? (x) : (y))

static_assert(MAX_SIGNED_INT64 > 0);

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
        // but not the trailer, in bytes. It is a multiple
        // of the block total count
        uint64_t repo_sz;

        // The size in bytes of the trailer
        uint64_t trailer_sz;

        // Count of blocks in the repo.
        // It should be equal to repo_sz/blk_sz
        uint32_t blk_total_cnt;

        // Count of blocks in the repo at the moment of
        // its initialization (when it was created)
        uint32_t blk_init_cnt;

    } __attribute__ ((aligned (1)));

    // In-disk repository's trailer
    struct repo_trailer_t {
        // It should be "EOF" followed by a NUL
        uint8_t magic[4];
    } __attribute__ ((aligned (1)));

    static_assert(sizeof(struct repo_header_t) <= 64);
}

Repository::Repository(const char* fpath, uint64_t phy_repo_start_pos) : fpath(fpath), fp(disk_fp), closed(true) {
    open(fpath, phy_repo_start_pos);
    assert(not closed);
}

Repository::Repository(std::stringstream&& mem, uint64_t phy_repo_start_pos) : fpath("#memory#"), mem_fp(std::move(mem)), fp(mem_fp), closed(true) {
    open_internal(fpath, phy_repo_start_pos);
    assert(not closed);
}

void Repository::close() {
    if (closed)
        return;

    // Note: currently the trailer size is fixed but we may decide
    // to make it variable later.
    //
    // The header will store the trailer size so we may decide
    // here to change it because at the moment of calling close()
    // we should have all the info needed.
    assert (trailer_sz == sizeof(struct repo_trailer_t));

    _seek_and_write_header(fp, phy_repo_start_pos, trailer_sz, blk_total_cnt, gp);
    std::streampos pos_after_trailer = _seek_and_write_trailer(fp, phy_repo_start_pos, blk_total_cnt, gp);

    fp.seekp(0);
    uintmax_t file_sz = pos_after_trailer - fp.tellp();

    if (std::addressof(fp) == std::addressof(disk_fp)) {
        disk_fp.close();
    }

    closed = true;

    if (std::addressof(fp) == std::addressof(disk_fp)) {
        std::filesystem::resize_file(fpath, file_sz);
    } else {
        // TODO
    }
}

void Repository::open(const char* fpath, uint64_t phy_repo_start_pos) {
    if (std::addressof(fp) != std::addressof(disk_fp)) {
        throw std::runtime_error("The current repository is memory based. You cannot open a disk based file.");
    }

    open_internal(fpath, phy_repo_start_pos);
}

void Repository::open_internal(const char* fpath, uint64_t phy_repo_start_pos) {
    if (not closed) {
        throw std::runtime_error("The current repository is not closed. You need to close it before opening a new one");
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
        disk_fp.open(
                fpath,
                // in/out binary file stream
                std::fstream::in | std::fstream::out | std::fstream::binary
                );
    } else {
        // note: fstream.open implicitly reset the read/write pointers
        // so we emulate the same for the memory based file
        mem_fp.seekg(0, std::ios_base::beg);
        mem_fp.seekp(0, std::ios_base::beg);
    }

    if (!fp) {
        throw OpenXOZError(fpath, "Repository::open could not open the file. May not exist or may not have permissions.");
    }

    // Renable the exception mask
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // Calculate the end of the file
    // If it cannot be represented by uint64_t, fail.
    seek_read_phy(fp, 0, std::ios_base::end);
    auto tmp_fp_end = fp.tellg();
    if (tmp_fp_end >= MAX_SIGNED_INT64) { // TODO signed or unsigned check?
        throw OpenXOZError(fpath, "the file is huge, it cannot be handled by xoz.");
    }

    // Save it
    fp_end = tmp_fp_end;

    // Check that the physical file is large enough to make
    // the phy_repo_start_pos valid.
    if (phy_repo_start_pos > fp_end) {
        // This should never happen but...
        throw InconsistentXOZ(*this, F()
                << "the repository started at an offset ("
                << phy_repo_start_pos
                << ") beyond the file physical size ("
                << fp_end
                << "."
                );
    }

    // Set the physical file positions to the expected start
    this->phy_repo_start_pos = phy_repo_start_pos;

    // We don't know yet where the repository ends.
    // It may end at the end of the file or before
    // so let's set it to zero
    this->phy_repo_end_pos = (uint64_t)0;

    gp.phy_repo_start_pos = phy_repo_start_pos;

    seek_read_and_check_header();
    seek_read_and_check_trailer();

    closed = false;
}

uint32_t Repository::grow_by_blocks(uint16_t blk_cnt) {
    if (blk_cnt == 0)
        throw std::runtime_error("alloc of 0 blocks is not allowed");

    // prevent overflow
    assert (blk_total_cnt + blk_cnt >= blk_cnt);

    uint64_t sz = (blk_cnt << gp.blk_sz_order);

    may_grow_file_due_seek_phy(fp, phy_repo_end_pos + sz);

    // Update the stats
    phy_repo_end_pos += sz;
    blk_total_cnt += blk_cnt;

    return blk_total_cnt - blk_cnt;
}

void Repository::shrink_by_blocks(uint32_t blk_cnt) {
    if (blk_cnt == 0) {
        throw std::runtime_error("free of 0 blocks is not allowed");
    }

    assert (blk_total_cnt >= 1);
    if (blk_cnt > blk_total_cnt-1) {
        throw std::runtime_error((F()
               << "free of "
               << blk_cnt
               << " blocks is not allowed because at most "
               << blk_total_cnt-1
               << " blocks can be freed."
               ).str());
    }

    uint64_t sz = (blk_cnt << gp.blk_sz_order);

    // Update the stats but do not truncate the file
    // (do that on close())
    phy_repo_end_pos -= sz;
    blk_total_cnt -= blk_cnt;
}


Repository::~Repository() {
    close();
}

std::ostream& Repository::print_stats(std::ostream& out) const {
    out << "XOZ Repository\n"
            "File: '" << fpath << "' "
            "[start pos: " << phy_repo_start_pos
            << ", end pos: " << phy_repo_end_pos << "]\n"
            "File status: ";

    if (closed) {
        out << "closed\n";
    } else {
       out << "open [fail: " << fp.fail()
           << ", bad: " << fp.bad()
           << ", eof: " << fp.eof()
           << ", good: " << fp.good() << "]\n";
    }

    out << "\nRepository size: " << (blk_total_cnt << gp.blk_sz_order)
        << " bytes, " << blk_total_cnt << " blocks\n"
        << "\nBlock size: " << gp.blk_sz
        << " bytes (order: " << (uint32_t)gp.blk_sz_order << ")\n"
        << "\nTrailer size: " << trailer_sz << " bytes\n";

    return out;

}

const std::stringstream& Repository::expose_mem_fp() const {
    if (std::addressof(fp) == std::addressof(disk_fp)) {
        throw std::runtime_error("The repository is not memory backed.");
    }

    return mem_fp;
}

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
Repository Repository::create(const char* fpath, bool fail_if_exists, uint64_t phy_repo_start_pos, const GlobalParameters& gp) {
    std::fstream test(fpath, std::fstream::in | std::fstream::binary);
    if (test) {
        // File already exists: ...
        if (fail_if_exists) {
            // ... bad we don't want to corrupt a file
            // by mistake. Abort.
            throw OpenXOZError(fpath, "the file already exist and Repository::create is configured to not override it.");
        } else {
            // ... ok, try to open (the constructor will fail
            // if it cannot open it)
            return Repository(fpath, phy_repo_start_pos);
        }
    } else {
        // File does not exist: create a new one and the open it
        std::fstream fp = _truncate_disk_file(fpath);
        _init_new_repository_into(fp, phy_repo_start_pos, gp);
        fp.close(); // flush the content make the constructor to open it back
        return Repository(fpath, phy_repo_start_pos);
    }
}

Repository Repository::create_mem_based(uint64_t phy_repo_start_pos, const GlobalParameters& gp) {
    std::stringstream fp;
    _init_new_repository_into(fp, phy_repo_start_pos, gp);
    return Repository(std::move(fp), phy_repo_start_pos);
}


void Repository::seek_read_and_check_header() {
    assert (phy_repo_start_pos <= fp_end);

    seek_read_phy(fp, phy_repo_start_pos);

    struct repo_header_t hdr;
    fp.read((char*)&hdr, sizeof(hdr));

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'XOZ' not found in the header.");
    }

    gp.blk_sz_order = u8_from_le(hdr.blk_sz_order);
    gp.blk_sz = (1 << hdr.blk_sz_order);

    if (gp.blk_sz_order < 6 or gp.blk_sz_order > 16) {
        throw InconsistentXOZ(*this, F()
                << "block size order "
                << gp.blk_sz_order
                << " is out of range [6 to 16] (block sizes of 64 to 64K)."
                );
    }

    blk_total_cnt = u32_from_le(hdr.blk_total_cnt);
    if (blk_total_cnt == 0) {
        throw InconsistentXOZ(*this, "the repository has a declared block total count of zero.");
    }

    // Calculate the repository size based on the block count.
    repo_sz = blk_total_cnt << gp.blk_sz_order;

    // Read the declared repository size from the header and
    // check that it matches with what we calculated
    uint64_t repo_sz_read = u64_from_le(hdr.repo_sz);
    if (repo_sz != repo_sz_read) {
        throw InconsistentXOZ(*this, F()
                << "the repository declared a size of "
                << repo_sz_read
                << " bytes but it is expected to have "
                << repo_sz
                << " bytes based on the block total count "
                << blk_total_cnt
                << " and block size "
                << gp.blk_sz
                << "."
                );
    }

    // Calculate the repository end position
    phy_repo_end_pos = phy_repo_start_pos + repo_sz;

    // This could happen only on overflow
    if (phy_repo_end_pos < phy_repo_start_pos) {
        throw InconsistentXOZ(*this, F()
                << "the repository starts at the physical file position "
                << phy_repo_start_pos
                << " and has a size of "
                << repo_sz
                << " bytes, which added together goes beyond the allowed limit."
                );
    }

    if (phy_repo_end_pos > fp_end) {
        throw InconsistentXOZ(*this, F()
                << "the repository has a declared size ("
                << repo_sz
                << ") starting at "
                << phy_repo_start_pos
                << " offset this gives an expected end of "
                << phy_repo_end_pos
                << " which goes beyond the physical file end at "
                << fp_end
                << "."
                );
    }

    if (fp_end > phy_repo_end_pos) {
        // More real bytes than the ones in the repo
        // Perhaps an incomplete shrink/truncate?
    }

    assert(fp_end >= phy_repo_end_pos);

    gp.blk_init_cnt = u32_from_le(hdr.blk_init_cnt);
    if (gp.blk_init_cnt == 0) {
        throw InconsistentXOZ(*this, "the repository has a declared initial block count of zero.");
    }

    trailer_sz = u64_from_le(hdr.trailer_sz);
}

void Repository::seek_read_and_check_trailer() {
    assert (phy_repo_end_pos > 0);
    assert (phy_repo_end_pos > phy_repo_start_pos);

    if (trailer_sz < sizeof(struct repo_trailer_t)) {
        throw InconsistentXOZ(*this, F()
                << "the declared trailer size ("
                << trailer_sz
                << ") is too small, required at least "
                << sizeof(struct repo_trailer_t)
                << " bytes."
                );
    }

    fp.seekg(phy_repo_start_pos + repo_sz);

    struct repo_trailer_t eof;
    fp.read((char*)&eof, sizeof(eof));

    if (strncmp((char*)&eof.magic, "EOF", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'EOF' not found in the trailer.");
    }
}

std::streampos Repository::_seek_and_write_header(std::ostream& fp, uint64_t phy_repo_start_pos, uint64_t trailer_sz, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    may_grow_and_seek_write_phy(fp, phy_repo_start_pos);
    struct repo_header_t hdr = {
        .magic = {'X', 'O', 'Z', 0},
        .blk_sz_order = u8_to_le(gp.blk_sz_order),
        .repo_sz = u64_to_le(blk_total_cnt << gp.blk_sz_order),
        .trailer_sz = u64_to_le(trailer_sz),
        .blk_total_cnt = u32_to_le(blk_total_cnt),
        .blk_init_cnt = u32_to_le(gp.blk_init_cnt),
    };

    fp.write((const char*)&hdr, sizeof(hdr));

    std::streampos streampos_after_hdr = fp.tellp();
    return streampos_after_hdr;
}

std::streampos Repository::_seek_and_write_trailer(std::ostream& fp, uint64_t phy_repo_start_pos, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    // Go to the end of the repository.
    // If this goes beyond the current file size, this will
    // "reserve" space for the "ghost" blocks.
    may_grow_and_seek_write_phy(fp, phy_repo_start_pos + (blk_total_cnt << gp.blk_sz_order));

    struct repo_trailer_t eof = {
        .magic = {'E', 'O', 'F', 0 }
    };
    fp.write((const char*)&eof, sizeof(eof));

    std::streampos streampos_after_trailer = fp.tellp();
    return streampos_after_trailer;
}

std::fstream Repository::_truncate_disk_file(const char* fpath) {
    std::fstream fp(
            fpath,
            // in/out binary file stream
            std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc
            );

    if (!fp) {
        throw OpenXOZError(fpath, "Repository::(truncate and create) could not truncate+create the file. May not have permissions.");
    }

    return fp;
}

void Repository::_init_new_repository_into(std::iostream& fp, uint64_t phy_repo_start_pos, const GlobalParameters& gp) {
    // Fail with an exception on any I/O error
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    if (gp.blk_init_cnt == 0) {
        throw std::runtime_error("invalid initial blocks count of zero");
    }

    // TODO check minimum blk_sz order
    if (gp.blk_sz_order == 0) {
        throw std::runtime_error("invalid block size order");
    }

    uint64_t trailer_sz = sizeof(struct repo_trailer_t);
    _seek_and_write_header(fp, phy_repo_start_pos, trailer_sz, gp.blk_init_cnt, gp);
    _seek_and_write_trailer(fp, phy_repo_start_pos, gp.blk_init_cnt, gp);

    fp.seekg(0, std::ios_base::beg);
    fp.seekp(0, std::ios_base::beg);
}

void Repository::may_grow_file_due_seek_phy(std::ostream& fp, std::streamoff offset, std::ios_base::seekdir way) {
    if ((way == std::ios_base::cur and offset > 0) or way == std::ios_base::beg) {
        const auto cur_pos = fp.tellp();

        fp.seekp(0, std::ios_base::end);
        const auto end_pos = fp.tellp();
        const auto ref_pos = way == std::ios_base::beg ? std::streampos(0) : cur_pos;

        // Note: for physical disk-based files we could use truncate/ftruncate
        // or C++ fs::resize_file *but* that will require to close the file and reopen
        // it again.
        // This is an unhappy thing.
        // Also, it does not work for memory-based files.
        if ((ref_pos + offset) > end_pos) {
            const auto hole = (ref_pos + offset) - end_pos;
            const char zeros[16] = {0};
            for(unsigned batch = 0; batch < hole/sizeof(zeros); ++batch) {
                fp.write(zeros, sizeof(zeros));
            }
            fp.write(zeros, hole % sizeof(zeros));
        }

        // restore the pointer
        fp.seekp(cur_pos, std::ios_base::beg);
    }
}

uint32_t Repository::read_extent(const Extent& ext, std::vector<char>& data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t usable_sz = calc_usable_space_size(ext, gp.blk_sz_order);
    const uint32_t reserve_sz = MIN(usable_sz, max_data_sz);
    data.resize(reserve_sz);

    const uint32_t read_ok = read_extent(ext, data.data(), reserve_sz, start);
    data.resize(read_ok);
    return read_ok;
}

uint32_t Repository::read_extent(const Extent& ext, char* data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t to_read_sz = chk_extent_for_rw(true, ext, max_data_sz, start);
    if (to_read_sz == 0) {
        return 0;
    }

    if (ext.is_suballoc()) {
        return rw_suballocated_extent(true, ext, data, to_read_sz, start);
    } else {
        return rw_fully_allocated_extent(true, ext, data, to_read_sz, start);
    }
}

uint32_t Repository::rw_suballocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz, uint32_t start) {
    const uint32_t subblk_sz = 1 << Extent::SUBBLK_SIZE_ORDER;
    const uint32_t subblk_cnt_per_blk = Extent::SUBBLK_CNT_PER_BLK;

    // Seek to the single block of the extent
    // and load it into a temporal buffer (full block)
    // not matter if is_read_op is true or false
    seek_read_blk(ext.blk_nr());
    std::vector<char> scratch(gp.blk_sz);
    fp.read(scratch.data(), gp.blk_sz);

    const uint16_t bitmap = ext.blk_bitmap();

    // If reading (is_read_op is true), copy from
    // the scratch block slices (subblocks) into
    // caller's data buffer reading the bitmap
    // from the highest to the lowest significant bit.
    //
    // If writing (is_read_op is false), do the same but
    // in the opposite direction: copy from data into
    // the scratch block.
    unsigned pscratch = 0, pdata = 0;
    uint32_t skip_offset = start;

    // note: to_rw_sz already takes into account
    // the start/skip_offset
    uint32_t remain_to_copy = to_rw_sz;

    for (unsigned i = 0; i < subblk_cnt_per_blk && remain_to_copy > 0; ++i) {
        const auto bit_selection = (1 << (subblk_cnt_per_blk - i - 1));

        if (bitmap & bit_selection) {
            if (skip_offset >= subblk_sz) {
                // skip the subblock entirely
                skip_offset -= subblk_sz;
            } else {
                const uint32_t copy_sz = MIN(subblk_sz - skip_offset, remain_to_copy);
                if (is_read_op) {
                    memcpy(
                            data + pdata,
                            scratch.data() + pscratch + skip_offset,
                            copy_sz
                          );
                } else {
                    memcpy(
                            scratch.data() + pscratch + skip_offset,
                            data + pdata,
                            copy_sz
                          );
                }

                // advance the data pointer
                pdata += copy_sz;

                // consume
                remain_to_copy -= copy_sz;

                // next iterations will copy full subblocks
                skip_offset = 0;
            }
        }

        // unconditionally advance the scratch pointer
        pscratch += subblk_sz;

    }

    // We didn't forget to read anything
    assert (remain_to_copy == 0);

    // Eventually at least 1 subblock was really copied
    // (otherwise we couldn't never decrement remain_to_copy to 0)
    assert (skip_offset == 0);

    if (is_read_op) {
        // do nothing else
    } else {
        // write back the updated scratch block
        seek_write_blk(ext.blk_nr());
        fp.write(scratch.data(), gp.blk_sz);
    }

    return to_rw_sz;
}

uint32_t Repository::rw_fully_allocated_extent(bool is_read_op, const Extent& ext, char* data, uint32_t to_rw_sz, uint32_t start) {
    // this should never happen
    assert (ext.blk_cnt() > 0);

    if (ext.blk_nr() + ext.blk_cnt() > blk_total_cnt) {
        // TODO
        throw std::runtime_error((F()
               << "The extent of blocks from block number "
               << ext.blk_nr()
               << " to block number "
               << ext.blk_nr() + ext.blk_cnt() - 1
               << " (both inclusive) partially falls out of bounds. "
               << "The repository only has "
               << blk_total_cnt
               << " blocks in total"
               ).str());
    }

    // Seek to the begin of the extent and advance as many
    // bytes as the caller said
    if (is_read_op) {
        seek_read_blk(ext.blk_nr(), start);
    } else {
        seek_write_blk(ext.blk_nr(), start);
    }

    assert (to_rw_sz > 0);
    if (is_read_op) {
        fp.read(data, to_rw_sz);
    } else {
        fp.write(data, to_rw_sz);
    }

    return to_rw_sz;
}

uint32_t Repository::chk_extent_for_rw(bool is_read_op, const Extent& ext, uint32_t max_data_sz, uint32_t start) {
    if (ext.is_unallocated()) {
        throw NullBlockAccess(F()
                << "The block 0x00 cannot be "
                << (is_read_op ? "read" : "written")
                );
    }

    const uint32_t usable_sz = calc_usable_space_size(ext, gp.blk_sz_order);

    // If the caller wants to read/write beyond the usable space, return EOF
    if (usable_sz <= start) {
        return 0; // EOF
    }

    // How much is readable/writeable and how much the caller is willing to read/write?
    const uint32_t read_writeable_sz = usable_sz - start;
    const uint32_t to_read_write_sz = MIN(read_writeable_sz, max_data_sz);

    if (to_read_write_sz == 0) {
        // This could happen because the 'start' is at the
        // end of the usable space so there is no readable/writeable bytes
        // (aka read_writeable_sz == 0) which translates to EOF
        //
        // Or it could happen because max_data_sz is 0.
        // We return EOF and the caller should distinguish this
        // from a real EOF (this is how POSIX read() and write() works)
        return 0;
    }

    assert (ext.blk_nr() != 0x0);
    if (ext.blk_nr() > blk_total_cnt) {
        // TODO
        throw std::runtime_error((F()
               << "The block number "
               << ext.blk_nr()
               << " is out of bounds. "
               << "The repository only has "
               << blk_total_cnt
               << " blocks in total"
               ).str());
    }

    return to_read_write_sz;
}

uint32_t Repository::write_extent(const Extent& ext, const std::vector<char>& data, uint32_t max_data_sz, uint32_t start) {
    static_assert(sizeof(uint32_t) <= sizeof(size_t));
    if (data.size() > uint32_t(-1)) {
        throw std::runtime_error("");
    }

    return write_extent(ext, data.data(), uint32_t(MIN(data.size(), size_t(max_data_sz))), start);
}

uint32_t Repository::write_extent(const Extent& ext, const char* data, uint32_t max_data_sz, uint32_t start) {
    const uint32_t to_write_sz = chk_extent_for_rw(false, ext, max_data_sz, start);
    if (to_write_sz == 0) {
        return 0;
    }

    if (ext.is_suballoc()) {
        return rw_suballocated_extent(false, ext, (char*)data, to_write_sz, start);
    } else {
        return rw_fully_allocated_extent(false, ext, (char*)data, to_write_sz, start);
    }
}

