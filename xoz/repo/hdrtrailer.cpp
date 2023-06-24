#include "xoz/repo/repo.h"
#include "xoz/arch.h"
#include "xoz/exceptions.h"
#include <cstring>


namespace {

    // In-disk repository's header
    struct repo_header_t {
        // It should be "XOZ" followed by a NUL
        uint8_t magic[4];

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

        // Log base 2 of the block size in bytes
        // Order of 10 means block size of 1KB,
        // order of 11 means block size of 2KB, and so on
        uint8_t blk_sz_order;

    } __attribute__ ((packed));

    // In-disk repository's trailer
    struct repo_trailer_t {
        // It should be "EOF" followed by a NUL
        uint8_t magic[4];
    } __attribute__ ((packed));

    static_assert(sizeof(struct repo_header_t) <= 64);
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

void Repository::seek_read_and_check_trailer(bool clear_trailer) {
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

    if (clear_trailer) {
        memset(&eof, 0, sizeof(eof));
        fp.seekp(phy_repo_start_pos + repo_sz);
        fp.write((const char*)&eof, sizeof(eof));
    }
}

std::streampos Repository::_seek_and_write_header(std::ostream& fp, uint64_t phy_repo_start_pos, uint64_t trailer_sz, uint32_t blk_total_cnt, const GlobalParameters& gp) {
    // Note: currently the trailer size is fixed but we may decide
    // to make it variable later.
    //
    // The header will store the trailer size so we may decide
    // here to change it because at the moment of calling close()
    // we should have all the info needed.
    assert (trailer_sz == sizeof(struct repo_trailer_t));

    may_grow_and_seek_write_phy(fp, phy_repo_start_pos);
    struct repo_header_t hdr = {
        .magic = {'X', 'O', 'Z', 0},
        .repo_sz = u64_to_le(blk_total_cnt << gp.blk_sz_order),
        .trailer_sz = u64_to_le(trailer_sz),
        .blk_total_cnt = u32_to_le(blk_total_cnt),
        .blk_init_cnt = u32_to_le(gp.blk_init_cnt),
        .blk_sz_order = u8_to_le(gp.blk_sz_order),
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

