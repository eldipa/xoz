#include "xoz/repo/repo.h"

#include <utility>

#include "xoz/exceptions.h"

Repository::Repository(const char* fpath, uint64_t phy_repo_start_pos): fpath(fpath), fp(disk_fp), closed(true) {
    open(fpath, phy_repo_start_pos);
    assert(not closed);
}

Repository::Repository(std::stringstream&& mem, uint64_t phy_repo_start_pos): fp(mem_fp), closed(true) {
    open_internal(Repository::IN_MEMORY_FPATH, std::move(mem), phy_repo_start_pos);
    assert(not closed);
}

Repository::~Repository() { close(); }

std::ostream& Repository::print_stats(std::ostream& out) const {
    out << "XOZ Repository\n"
           "File: '"
        << fpath
        << "' "
           "[start pos: "
        << phy_repo_start_pos << ", end pos: " << phy_repo_end_pos
        << "]\n"
           "File status: ";

    if (closed) {
        out << "closed\n";
    } else {
        out << "open [fail: " << fp.fail() << ", bad: " << fp.bad() << ", eof: " << fp.eof() << ", good: " << fp.good()
            << "]\n";
    }

    out << "\nRepository size: " << (blk_total_cnt << gp.blk_sz_order) << " bytes, " << blk_total_cnt << " blocks\n"
        << "\nBlock size: " << gp.blk_sz << " bytes (order: " << (uint32_t)gp.blk_sz_order << ")\n"
        << "\nTrailer size: " << trailer_sz << " bytes\n";

    return out;
}

const std::stringstream& Repository::expose_mem_fp() const {
    if (std::addressof(fp) == std::addressof(disk_fp)) {
        throw std::runtime_error("The repository is not memory backed.");
    }

    return mem_fp;
}
