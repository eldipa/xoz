#include "xoz/repo/repo.h"
#include "xoz/arch.h"
#include "xoz/exceptions.h"
#include <filesystem>
#include <cstdint>


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
    if (tmp_fp_end >= INT64_MAX) { // TODO signed or unsigned check?
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
    seek_read_and_check_trailer(true /* clear_trailer */);

    closed = false;
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


void Repository::close() {
    if (closed)
        return;

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
