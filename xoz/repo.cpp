#include "xoz/repo.h"
#include "xoz/arch.h"
#include "xoz/exceptions.h"
#include <cstring>

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

Repository::Repository(const char* fpath, uint32_t repo_start_pos) : fpath(fpath), closed(true) {
    open(fpath, repo_start_pos);
    assert(not closed);
}

void Repository::close() {
    if (closed)
        return;

    _seek_and_write_header(fp, repo_start_pos, repo_sz, blk_total_cnt, gp);
    _seek_and_write_trailer(fp, repo_start_pos, blk_total_cnt, gp);
    fp.close();
    closed = true;

    // TODO
    // We should shrink/truncate the physical file
    // if its size is larger than repo_end_pos
}

void Repository::open(const char* fpath, std::streampos repo_start_pos) {
    if (not closed) {
        throw std::runtime_error("The current repository is not closed. You need to close it before opening a new one");
    }

    // Try to avoid raising an exception on the opening so we can
    // raise better exceptions.
    //
    // Enable the exception mask after the open
    fp.exceptions(std::ifstream::goodbit);
    fp.clear();

    fp.open(
            fpath,
            // in/out binary file stream
            std::fstream::in | std::fstream::out | std::fstream::binary
            );

    if (!fp) {
        throw OpenXOZError(fpath, "Repository::open could not open the file. May not exist or may not have permissions.");
    }

    // Renable the exception mask
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // TODO check??
    seek_read_and_check_header();
    seek_read_and_check_trailer();

    closed = false;
}

uint32_t Repository::alloc_blocks(uint16_t blk_cnt) {
    assert (blk_cnt > 0);
    assert (blk_total_cnt + blk_cnt >= blk_cnt); // prevent overflow

    expand_phy_bytes(blk_cnt << gp.blk_sz_order);
    blk_total_cnt += blk_cnt;

    return blk_total_cnt - blk_cnt;
}

void Repository::free_blocks(uint16_t blk_cnt) {
    assert (blk_cnt > 0);
    assert (blk_total_cnt >= blk_cnt); // prevent underflow

    shrink_phy_bytes(blk_cnt << gp.blk_sz_order);
    blk_total_cnt -= blk_cnt;
}


Repository::~Repository() {
    close();
}

std::ostream& Repository::print_stats(std::ostream& out) const {
    out << "XOZ Repository\n"
            "File: '" << fpath << "' "
            "[start pos: " << repo_start_pos
            << ", end pos: " << repo_end_pos << "]\n"
            "File status: ";

    if (closed) {
        out << "closed\n";
    } else {
       out << "open [fail: " << fp.fail()
           << ", bad: " << fp.bad()
           << ", eof: " << fp.eof()
           << ", good: " << fp.good() << "]\n";
    }

    out << "\nRepository size: " << repo_sz
        << " bytes, " << blk_total_cnt << " blocks\n"
        << "\nBlock size: " << gp.blk_sz
        << " bytes (order: " << (uint32_t)gp.blk_sz_order << ")\n";

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
Repository Repository::create(const char* fpath, bool fail_if_exists, uint32_t repo_start_pos, const GlobalParameters& gp) {
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
            return Repository(fpath, repo_start_pos);
        }
    } else {
        // File does not exist: create a new one and the open it
        _truncate_and_create_new_repository(fpath, repo_start_pos, gp);
        return Repository(fpath, repo_start_pos);
    }
}


void Repository::expand_phy_bytes(uint32_t sz) {
    assert (sz);
    repo_sz += sz;
    repo_end_pos += sz;

    // We do not do anything else:
    // The expansion will take place on Repository::close
    // where the trailer will be written at the end
    // of the repository (marking on stone the new file boundary)
}

void Repository::shrink_phy_bytes(uint32_t sz) {
    assert (sz);
    repo_sz -= sz;
    repo_end_pos -= sz;

    // Defer real shrink / truncate of the physical file
    // at the moment of the close.
    seek_write_phy(repo_end_pos);

    // For safety, move the read pointer to the begin
    // of the file if it is out of bounds
    if (fp.tellg() >= repo_end_pos)
        seek_read_phy(repo_start_pos);
}


void Repository::seek_read_and_check_header() {
    seek_read_phy(repo_start_pos);

    struct repo_header_t hdr;
    fp.read((char*)&hdr, sizeof(hdr));

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'XOZ' not found in the header.");
    }

    // Check that the physical file really has those bytes
    // claimed in the repo_sz
    seek_read_phy(0, std::ios_base::end);
    std::streampos phy_end_pos = fp.tellg();

    if (repo_start_pos > phy_end_pos) {
        // This should never happen
        throw InconsistentXOZ(*this, F()
                << "the repository started at an offset ("
                << repo_start_pos
                << ") beyond the file physical size ("
                << phy_end_pos
                << "."
                );
    }

    gp.repo_start_pos = repo_start_pos;

    // TODO check minimum blk sz / order
    gp.blk_sz_order = u8_from_le(hdr.blk_sz_order);
    gp.blk_sz = (1 << hdr.blk_sz_order);

    repo_sz = u64_from_le(hdr.repo_sz);
    repo_end_pos = repo_start_pos + std::streamoff(repo_sz);

    // Signed to unsigned conversion "ok": we know that the left
    // operand is greater than the right operand
    assert(phy_end_pos >= repo_start_pos);
    uint64_t phy_repo_size = uint64_t(phy_end_pos - repo_start_pos);
    if (phy_repo_size > repo_sz) {
        // More real bytes than the ones in the repo
        // Perhaps an incomplete shrink/truncate?
    } else if (phy_repo_size < repo_sz) {
        throw InconsistentXOZ(*this, F()
                << "the repository has a declared size ("
                << repo_sz
                << ") greater than the file physical size ("
                << phy_repo_size
                << ")."
                );
    }

    assert(phy_repo_size >= repo_sz);

    blk_total_cnt = u32_from_le(hdr.blk_total_cnt);
    if (blk_total_cnt == 0) {
        throw InconsistentXOZ(*this, "the repository has a declared block total count of zero.");
    }

    if (blk_total_cnt != repo_sz / gp.blk_sz) {
        throw InconsistentXOZ(*this, F()
                << "the repository has a declared block total count ("
                << blk_total_cnt
                << ") that does not match with the expected repo-size/blk-size ratio ("
                << repo_sz << "/" << gp.blk_sz
                << ")."
                );
    }

    gp.blk_init_cnt = u32_from_le(hdr.blk_init_cnt);
    if (gp.blk_init_cnt == 0) {
        throw InconsistentXOZ(*this, "the repository has a declared initial block count of zero.");
    }
}

// Note: repo_end_pos **must** be initialized first
// So seek_read_and_check_trailer **should** be called *after*
// calling seek_read_and_check_header
void Repository::seek_read_and_check_trailer() {
    assert (repo_end_pos > 0);
    assert (repo_end_pos > repo_start_pos);
    fp.seekg(repo_end_pos - (std::streamoff)sizeof(struct repo_trailer_t));

    struct repo_trailer_t eof;
    fp.read((char*)&eof, sizeof(eof));

    if (strncmp((char*)&eof.magic, "EOF", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'EOF' not found in the trailer.");
    }
}

void Repository::_seek_and_write_header(std::fstream& fp, std::streampos repo_start_pos, uint64_t repo_sz, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    fp.seekp(repo_start_pos);
    struct repo_header_t hdr = {
        .magic = {'X', 'O', 'Z', 0},
        .blk_sz_order = u8_to_le(gp.blk_sz_order),
        .repo_sz = u64_to_le(repo_sz),
        .blk_total_cnt = u32_to_le(blk_total_cnt),
        .blk_init_cnt = u32_to_le(gp.blk_init_cnt),
    };

    fp.write((const char*)&hdr, sizeof(hdr));
}

void Repository::_seek_and_write_trailer(std::fstream& fp, std::streampos repo_start_pos, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    // Go to the end of the repository.
    // If this goes beyond the current file size, this will
    // "reserve" space for the "ghost" blocks.
    fp.seekp((uint64_t)repo_start_pos + sizeof(struct repo_header_t) + (blk_total_cnt << gp.blk_sz_order));

    struct repo_trailer_t eof = {
        .magic = {'E', 'O', 'F', 0 }
    };
    fp.write((const char*)&eof, sizeof(eof));
}

void Repository::_truncate_and_create_new_repository(const char* fpath, std::streampos repo_start_pos, const GlobalParameters& gp) {
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

    uint64_t repo_sz = sizeof(struct repo_header_t) + \
                       (gp.blk_init_cnt << gp.blk_sz_order) + \
                       sizeof(struct repo_trailer_t);

    _seek_and_write_header(fp, repo_start_pos, repo_sz, gp.blk_init_cnt, gp);
    _seek_and_write_trailer(fp, repo_start_pos, gp.blk_init_cnt, gp);
}
