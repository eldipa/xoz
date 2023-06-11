#include "xoz/repo.h"
#include "xoz/arch.h"
#include "xoz/exceptions.h"
#include <cstring>

#define MAX_SIGNED_INT64 int64_t((~((uint64_t)0)) >> 1)

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
}

Repository::Repository(const char* fpath, uint64_t phy_repo_start_pos) : fpath(fpath), fp(disk_fp), closed(true) {
    open(fpath, phy_repo_start_pos);
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
    _seek_and_write_trailer(fp, phy_repo_start_pos, blk_total_cnt, gp);

    if (std::addressof(fp) == std::addressof(disk_fp)) {
        disk_fp.close();
    }

    closed = true;

    // TODO
    // We should shrink/truncate the physical file
    // if its size is larger than phy_repo_end_pos
}

void Repository::open(const char* fpath, uint64_t phy_repo_start_pos) {
    if (not closed) {
        throw std::runtime_error("The current repository is not closed. You need to close it before opening a new one");
    }

    if (std::addressof(fp) != std::addressof(disk_fp)) {
        throw std::runtime_error("The current repository is memory based. You cannot open a disk based file.");
    }

    // Try to avoid raising an exception on the opening so we can
    // raise better exceptions.
    //
    // Enable the exception mask after the open
    fp.exceptions(std::ifstream::goodbit);
    fp.clear();

    // note: iostream does not have open() so we must access disk_fp directly
    // but the check above ensure that fp *is* disk_fp
    disk_fp.open(
            fpath,
            // in/out binary file stream
            std::fstream::in | std::fstream::out | std::fstream::binary
            );

    if (!fp) {
        throw OpenXOZError(fpath, "Repository::open could not open the file. May not exist or may not have permissions.");
    }

    // Renable the exception mask
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // Calculate the end of the file
    // If it cannot be represented by uint64_t, fail.
    seek_read_phy(0, std::ios_base::end);
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

uint32_t Repository::alloc_blocks(uint16_t blk_cnt) {
    if (blk_cnt == 0)
        throw std::runtime_error("alloc of 0 blocks is not allowed");

    // prevent overflow
    assert (blk_total_cnt + blk_cnt >= blk_cnt);

    uint64_t sz = (blk_cnt << gp.blk_sz_order);

    phy_repo_end_pos += sz;

    blk_total_cnt += blk_cnt;

    return blk_total_cnt - blk_cnt;
}

void Repository::free_blocks(uint16_t blk_cnt) {
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
        _truncate_and_create_new_repository(fpath, phy_repo_start_pos, gp);
        return Repository(fpath, phy_repo_start_pos);
    }
}



void Repository::seek_read_and_check_header() {
    assert (phy_repo_start_pos >= 0);
    assert (phy_repo_start_pos <= fp_end);

    seek_read_phy(phy_repo_start_pos);

    struct repo_header_t hdr;
    fp.read((char*)&hdr, sizeof(hdr));

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'XOZ' not found in the header.");
    }

    gp.blk_sz_order = u8_from_le(hdr.blk_sz_order);
    gp.blk_sz = (1 << hdr.blk_sz_order);

    if (gp.blk_sz_order < 10 or gp.blk_sz_order > 16) {
        throw InconsistentXOZ(*this, F()
                << "block size order "
                << gp.blk_sz_order
                << " is out of range [10 to 16] (block sizes of 1K to 64K)."
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

void Repository::_seek_and_write_header(std::ostream& fp, uint64_t phy_repo_start_pos, uint64_t trailer_sz, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    fp.seekp(phy_repo_start_pos);
    struct repo_header_t hdr = {
        .magic = {'X', 'O', 'Z', 0},
        .blk_sz_order = u8_to_le(gp.blk_sz_order),
        .repo_sz = u64_to_le(blk_total_cnt << gp.blk_sz_order),
        .trailer_sz = u64_to_le(trailer_sz),
        .blk_total_cnt = u32_to_le(blk_total_cnt),
        .blk_init_cnt = u32_to_le(gp.blk_init_cnt),
    };

    fp.write((const char*)&hdr, sizeof(hdr));
}

void Repository::_seek_and_write_trailer(std::ostream& fp, uint64_t phy_repo_start_pos, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    // Go to the end of the repository.
    // If this goes beyond the current file size, this will
    // "reserve" space for the "ghost" blocks.
    fp.seekp(phy_repo_start_pos + (blk_total_cnt << gp.blk_sz_order));

    struct repo_trailer_t eof = {
        .magic = {'E', 'O', 'F', 0 }
    };
    fp.write((const char*)&eof, sizeof(eof));
}

void Repository::_truncate_and_create_new_repository(const char* fpath, uint64_t phy_repo_start_pos, const GlobalParameters& gp) {
    std::fstream fp(
            fpath,
            // in/out binary file stream
            std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc
            );

    if (!fp) {
        throw OpenXOZError(fpath, "Repository::(truncate and create) could not truncate+create the file. May not have permissions.");
    }

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
}
