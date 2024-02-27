#include "xoz/err/exceptions.h"
#include "xoz/mem/bits.h"
#include "xoz/repo/repository.h"

void Repository::may_grow_file_due_seek_phy(std::ostream& fp, std::streamoff offset, std::ios_base::seekdir way) {
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
            const char zeros[16] = {0};
            for (unsigned batch = 0; batch < hole / sizeof(zeros); ++batch) {
                fp.write(zeros, sizeof(zeros));
            }
            fp.write(zeros, hole % sizeof(zeros));
        }

        // restore the pointer
        fp.seekp(cur_pos, std::ios_base::beg);
    }
}

std::tuple<uint32_t, uint16_t> Repository::impl_grow_by_blocks(uint16_t blk_cnt) {
    assert(not u32_add_will_overflow(blk_total_cnt, blk_cnt));

    // TODO impl_grow_by_blocks will disappear soon anyways may_grow_file_due_seek_phy(fp, phy_repo_end_pos + sz);

    // Update the stats
    blk_total_cnt += blk_cnt;

    return {blk_total_cnt - blk_cnt, blk_cnt};
}

uint32_t Repository::impl_shrink_by_blocks(uint32_t blk_cnt) {

    assert(blk_total_cnt >= 1);
    assert(blk_total_cnt > blk_cnt);

    // Update the stats but do not truncate the file
    // (do that on close())
    // phy_repo_end_pos -= sz;
    blk_total_cnt -= blk_cnt;

    return blk_cnt;
}

uint32_t Repository::impl_release_blocks() {
    // Repository::impl_shrink_by_blocks always shrink as many blocks as requested
    // so there is no pending block to be released.
    return 0;
}
