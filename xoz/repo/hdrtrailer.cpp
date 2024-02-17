#include <cstring>

#include "xoz/err/exceptions.h"
#include "xoz/io/iospan.h"
#include "xoz/mem/endianness.h"
#include "xoz/repo/repository.h"

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

    // For more future metadata
    uint8_t reserved[7];

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

    // Segment that points to the blocks that hold the root or main descriptor set
    // See seek_read_and_check_header for the complete interpretation of this.
    uint8_t root_sg[12];

    uint32_t hdr_checksum;
} __attribute__((packed));

// In-disk repository's trailer
struct repo_trailer_t {
    // It should be "EOF" followed by a NUL
    uint8_t magic[4];
} __attribute__((packed));

static_assert(sizeof(struct repo_header_t) == 64);
}  // namespace

void Repository::seek_read_and_check_header() {
    assert(phy_repo_start_pos <= fp_end);

    seek_read_phy(fp, phy_repo_start_pos);

    struct repo_header_t hdr;
    fp.read((char*)&hdr, sizeof(hdr));

    if (strncmp((char*)&hdr.magic, "XOZ", 4) != 0) {
        throw InconsistentXOZ(*this, "magic string 'XOZ' not found in the header.");
    }

    // TODO
    // check checksum of (char*)(&hdr) against hdr.hdr_checksum

    if (hdr.feature_flags_incompat) {
        throw InconsistentXOZ(*this, "the repository has incompatible features.");
    }

    if (hdr.feature_flags_ro_compat) {
        // TODO implement read-only mode
        throw InconsistentXOZ(
                *this,
                "the repository has read-only compatible features and the repository was not open in read-only mode.");
    }

    gp.blk_sz_order = u8_from_le(hdr.blk_sz_order);
    gp.blk_sz = (1 << hdr.blk_sz_order);

    if (gp.blk_sz_order < 6 or gp.blk_sz_order > 16) {
        throw InconsistentXOZ(*this, F() << "block size order " << gp.blk_sz_order
                                         << " is out of range [6 to 16] (block sizes of 64 to 64K).");
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
        throw InconsistentXOZ(*this, F() << "the repository declared a size of " << repo_sz_read
                                         << " bytes but it is expected to have " << repo_sz
                                         << " bytes based on the block total count " << blk_total_cnt
                                         << " and block size " << gp.blk_sz << ".");
    }


    // This could happen only on overflow
    if (u64_add_will_overflow(phy_repo_start_pos, repo_sz)) {
        throw InconsistentXOZ(*this, F() << "the repository starts at the physical file position " << phy_repo_start_pos
                                         << " and has a size of " << repo_sz
                                         << " bytes, which added together goes beyond the allowed limit.");
    }

    // Calculate the repository end position
    phy_repo_end_pos = phy_repo_start_pos + repo_sz;

    if (phy_repo_end_pos > fp_end) {
        throw InconsistentXOZ(*this, F() << "the repository has a declared size (" << repo_sz << ") starting at "
                                         << phy_repo_start_pos << " offset this gives an expected end of "
                                         << phy_repo_end_pos << " which goes beyond the physical file end at " << fp_end
                                         << ".");
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

    // load root set's segment:
    static_assert(sizeof(hdr.root_sg) >= 12);
    IOSpan io(hdr.root_sg, sizeof(hdr.root_sg));
    root_sg = Segment::load_struct_from(io);

    // the root_sg is located in the header so we mark this with an empty extent
    external_root_sg_loc = Segment::EmptySegment();

    // However, if the particular setting is set, assume that the recently loaded root_sg is
    // not the root segment but a single extent that points a block(s) with the real
    // root segment.
    // This additional indirection allow us to encode large root segments outside the header.
    if (root_sg.inline_data_sz() == 4 and root_sg.ext_cnt() == 1) {
        external_root_sg_loc.add_extent(root_sg.exts()[0]);

        IOSegment io2(*this, external_root_sg_loc);
        root_sg = Segment::load_struct_from(io2);

        // In the inline data we have the checksum of the root segment.
        // We don't have such checksum when the root segment is written in the header
        // because there is a checksum for the entire header.
        // But when the root segment is outside the header, we need this extra protection.
        IOSpan io3(root_sg.inline_data());

        [[maybe_unused]] uint32_t root_sg_chksum = io3.read_u32_from_le();

        // TODO check assert(checksum(io2) == root_sg_chksum);

    } else if (root_sg.inline_data_sz() != 0) {
        throw InconsistentXOZ(*this, "the repository header contains a root segment with an unexpected format.");
    }

    // Load the descriptor set.
    root_set = std::make_unique<DescriptorSet>(this->root_sg, *this, *this, idmgr);
    root_set->load_set();
}

void Repository::seek_read_and_check_trailer(bool clear_trailer) {
    assert(phy_repo_end_pos > 0);
    assert(phy_repo_end_pos > phy_repo_start_pos);

    if (trailer_sz < sizeof(struct repo_trailer_t)) {
        throw InconsistentXOZ(*this, F() << "the declared trailer size (" << trailer_sz
                                         << ") is too small, required at least " << sizeof(struct repo_trailer_t)
                                         << " bytes.");
    }

    assert(not u64_add_will_overflow(phy_repo_start_pos, repo_sz));
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

std::streampos Repository::_seek_and_write_header(std::ostream& fp, uint64_t phy_repo_start_pos, uint64_t trailer_sz,
                                                  uint32_t blk_total_cnt, const GlobalParameters& gp,
                                                  const std::vector<uint8_t>& root_sg_bytes) {
    // Note: currently the trailer size is fixed but we may decide
    // to make it variable later.
    //
    // The header will store the trailer size so we may decide
    // here to change it because at the moment of calling close()
    // we should have all the info needed.
    assert(trailer_sz == sizeof(struct repo_trailer_t));

    may_grow_and_seek_write_phy(fp, phy_repo_start_pos);
    struct repo_header_t hdr = {
            .magic = {'X', 'O', 'Z', 0},
            .repo_sz = u64_to_le(blk_total_cnt << gp.blk_sz_order),
            .trailer_sz = u64_to_le(trailer_sz),
            .blk_total_cnt = u32_to_le(blk_total_cnt),
            .blk_init_cnt = u32_to_le(gp.blk_init_cnt),
            .blk_sz_order = u8_to_le(gp.blk_sz_order),
            .reserved = {0, 0, 0, 0, 0, 0, 0},
            .feature_flags_compat = u32_to_le(0),
            .feature_flags_incompat = u32_to_le(0),
            .feature_flags_ro_compat = u32_to_le(0),
            .root_sg = {0},
            .hdr_checksum = u32_to_le(0),
    };

    assert(sizeof(hdr.root_sg) == root_sg_bytes.size());
    memcpy(hdr.root_sg, root_sg_bytes.data(), sizeof(hdr.root_sg));

    fp.write((const char*)&hdr, sizeof(hdr));

    std::streampos streampos_after_hdr = fp.tellp();
    return streampos_after_hdr;
}

std::streampos Repository::_seek_and_write_trailer(std::ostream& fp, uint64_t phy_repo_start_pos,
                                                   uint32_t blk_total_cnt, const GlobalParameters& gp) {
    // Go to the end of the repository.
    // If this goes beyond the current file size, this will
    // "reserve" space for the "ghost" blocks.
    assert(not u64_add_will_overflow(phy_repo_start_pos, (blk_total_cnt << gp.blk_sz_order)));
    may_grow_and_seek_write_phy(fp, phy_repo_start_pos + (blk_total_cnt << gp.blk_sz_order));

    struct repo_trailer_t eof = {.magic = {'E', 'O', 'F', 0}};
    fp.write((const char*)&eof, sizeof(eof));

    std::streampos streampos_after_trailer = fp.tellp();
    return streampos_after_trailer;
}

std::vector<uint8_t> Repository::_encode_empty_root_segment() {
    const auto hdr_capacity = sizeof(((struct repo_header_t*)nullptr)->root_sg);
    std::vector<uint8_t> root_sg_bytes(hdr_capacity);

    Segment root_sg_empty = Segment::EmptySegment();
    root_sg_empty.add_end_of_segment();

    IOSpan io(root_sg_bytes.data(), (uint32_t)root_sg_bytes.size());
    root_sg_empty.write_struct_into(io);

    assert(root_sg_bytes.size() == hdr_capacity);
    return root_sg_bytes;
}

std::vector<uint8_t> Repository::update_and_encode_root_segment_and_loc() {
    const auto hdr_capacity = sizeof(((struct repo_header_t*)nullptr)->root_sg);
    std::vector<uint8_t> root_sg_bytes(hdr_capacity);

    root_set->write_set();
    assert(root_sg.inline_data_sz() == 0);
    assert(external_root_sg_loc.inline_data_sz() == 0);

    auto root_sg_sz = root_sg.calc_struct_footprint_size();
    if (root_sg_sz == hdr_capacity or root_sg_sz + Segment::EndOfSegmentSize <= hdr_capacity) {

        // If it does not fit perfectly we need to mark somehow the end of the segment
        // otherwise the reader will overflow and read garbage after the segment.
        if (root_sg_sz != hdr_capacity) {
            root_sg.add_end_of_segment();
            root_sg_sz = root_sg.calc_struct_footprint_size();
        }

        assert(root_sg_sz <= hdr_capacity);

        // the root segment is small enough to be stored in the header;
        // release any allocated space
        if (not external_root_sg_loc.is_empty_space()) {
            this->allocator().dealloc(external_root_sg_loc);
            external_root_sg_loc = Segment::EmptySegment();
        }

        IOSpan io(root_sg_bytes);
        root_sg.write_struct_into(io);

        // remove the end of segment
        root_sg.remove_inline_data();
    } else {
        // Either the root_sg is too large to fit in the header or it is small
        // enough but we cannot put there because with the addition of the end-of-segment
        // it will not fit.

        // root segment is too large to fit in the header, try to use
        // the space allocated in external_root_sg_loc first, allocate more
        // if required
        auto external_capacity = external_root_sg_loc.calc_data_space_size(blk_sz_order());
        if ((external_capacity >> 2) > root_sg_sz) {
            // the space needed is less than 25% of the current capacity: dealloc + (re)alloc
            // to shrink and save some space
            this->allocator().dealloc(external_root_sg_loc);
            external_root_sg_loc = this->allocator().alloc(root_sg_sz);
        } else if (external_capacity >= root_sg_sz) {
            // the current capacity can hold the root segm: do nothing
        } else {
            this->allocator().dealloc(external_root_sg_loc);
            external_root_sg_loc = this->allocator().alloc(root_sg_sz);
        }

        // Write the root segment in an external location
        IOSegment io(*this, external_root_sg_loc);
        root_sg.write_struct_into(io);

        uint32_t root_sg_chksum = 0;  // TODO
        external_root_sg_loc.reserve_inline_data(sizeof(root_sg_chksum));
        IOSpan io2(external_root_sg_loc.inline_data());
        io2.write_u32_to_le(root_sg_chksum);

        IOSpan io3(root_sg_bytes);
        external_root_sg_loc.write_struct_into(io3);

        // We don't really want to store the checksum, it was put here just for
        // the write_struct_into call above. Remove the inline then.
        external_root_sg_loc.remove_inline_data();
    }

    assert(root_sg_bytes.size() == hdr_capacity);
    return root_sg_bytes;
}

void Repository::_init_new_repository_into(std::iostream& fp, uint64_t phy_repo_start_pos, const GlobalParameters& gp) {
    // Fail with an exception on any I/O error
    fp.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    if (gp.blk_init_cnt == 0) {
        throw std::runtime_error("invalid initial blocks count of zero");
    }

    if (gp.blk_sz_order < 6) {  // minimum block size is 64 bytes, hence order 6
        throw std::runtime_error("invalid block size order");
    }


    uint64_t trailer_sz = sizeof(struct repo_trailer_t);
    const auto root_sg_bytes = _encode_empty_root_segment();
    _seek_and_write_header(fp, phy_repo_start_pos, trailer_sz, gp.blk_init_cnt, gp, root_sg_bytes);
    _seek_and_write_trailer(fp, phy_repo_start_pos, gp.blk_init_cnt, gp);

    fp.seekg(0, std::ios_base::beg);
    fp.seekp(0, std::ios_base::beg);
}
