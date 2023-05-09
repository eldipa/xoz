#include "xoz/repo.h"
#include "xoz/arch.h"
#include <cstring>

Repository::Repository(const char* fpath, uint32_t repo_start_pos) : fpath(fpath), closed(true) {
    fp.open(
            fpath,
            // in/out binary file stream
            std::fstream::in | std::fstream::out | std::fstream::binary
            );

    if (!fp) {
        throw "1";
    }

    // TODO check??
    load_global_parameters();
    closed = false;
}

void Repository::close() {
    if (closed)
        return;

    // TODO
    // We should shrink/truncate the physical file
    // if its size is larger than repo_end_pos
    fp.close();
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
            throw "1";
        } else {
            // ... ok, try to open (the constructor will fail
            // if it cannot open it)
            return Repository(fpath, repo_start_pos);
        }
    } else {
        // File does not exist: create a new one and the open it
        _create_new_repository(fpath, repo_start_pos, gp);
        return Repository(fpath, repo_start_pos);
    }
}


void Repository::expand_phy_bytes(uint32_t sz) {
    repo_sz += sz;
    repo_end_pos += sz;

    // TODO
    // Should we write at least one byte
    // to signal that we really want to expand the file?
    seek_write_phy(repo_end_pos);
}

void Repository::shrink_phy_bytes(uint32_t sz) {
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


void Repository::load_global_parameters() {
    seek_read_phy(repo_start_pos);

    struct repo_header_t hdr;
    fp.read((char*)&hdr, sizeof(hdr));

    if (!fp) {
        throw "1";
    }

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw "1";
    }

    gp.repo_start_pos = repo_start_pos;

    gp.blk_sz_order = u8_from_le(hdr.blk_sz_order);
    gp.blk_sz = (1 << hdr.blk_sz_order);

    repo_sz = u64_from_le(hdr.repo_sz);
    repo_end_pos = repo_start_pos + std::streamoff(repo_sz);

    // Check that the physical file really has those bytes
    // claimed in the repo_sz
    seek_read_phy(0, std::ios_base::end);
    std::streampos phy_end_pos = fp.tellg();

    if (repo_start_pos > phy_end_pos) {
        // This should never happen
        throw "1";
    }

    // Signed to unsigned conversion "ok": we know that the left
    // operand is greater than the right operand
    uint64_t phy_repo_size = uint64_t(phy_end_pos - repo_start_pos);
    if (phy_repo_size > repo_sz) {
        // More real bytes than the ones in the repo
        // Perhaps an incomplete shrink/truncate?
    } else if (phy_repo_size < repo_sz) {
        throw "1";
    }

    assert(phy_repo_size == repo_sz);

    blk_total_cnt = u32_from_le(hdr.blk_total_cnt);

    assert(blk_total_cnt <= repo_sz / gp.blk_sz);

    // For safety, set the reading pointer to the begin
    // of the repo
    seek_read_phy(repo_start_pos);
}

void Repository::store_global_parameters() {
    seek_write_phy(repo_start_pos);
    _store_global_parameters(fp, repo_sz, blk_total_cnt, gp);
}

void Repository::_store_global_parameters(std::fstream& fp, uint64_t repo_sz, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    struct repo_header_t hdr = {
        .magic = {'X', 'O', 'Z', 0},
        .blk_sz_order = u8_to_le(gp.blk_sz_order),
        .repo_sz = u64_to_le(repo_sz),
        .blk_total_cnt = u32_to_le(blk_total_cnt),
    };

    fp.write((const char*)&hdr, sizeof(hdr));

    if (!fp) {
        throw "1";
    }
}

void Repository::_create_new_repository(const char* fpath, uint32_t repo_start_pos, const GlobalParameters& gp) {
    std::fstream fp(
            fpath,
            // in/out binary file stream
            std::fstream::in | std::fstream::out | std::fstream::binary | std::fstream::trunc
            );

    if (!fp) {
        throw "1";
    }

    if (gp.blk_init_cnt == 0) {
        throw "1";
    }

    uint64_t repo_sz = sizeof(struct repo_header_t) + \
                       (gp.blk_init_cnt << gp.blk_sz_order) + \
                       sizeof(struct repo_eof_t);

    fp.seekp(repo_start_pos);
    _store_global_parameters(fp, repo_sz, gp.blk_init_cnt, gp);

    // Reserve space for the initial blocks
    fp.seekp(repo_start_pos + sizeof(struct repo_header_t) + (gp.blk_init_cnt << gp.blk_sz_order));

    struct repo_eof_t eof = {
        .magic = {'E', 'O', 'F', 0 }
    };
    fp.write((const char*)&eof, sizeof(eof));
}
