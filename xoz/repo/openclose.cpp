#include <cstdint>
#include <filesystem>

#include "xoz/err/exceptions.h"
#include "xoz/repo/repository.h"

void Repository::close() {
    if (closed)
        return;

    const auto root_sg_bytes = update_and_encode_root_segment_and_loc();

    // note: we declare that the repository has the same block count
    // than the file block array *plus* its begin blk number to count
    // for the array's header (where the repo's header will be written into)
    //
    // one comment on this: the file block array *may* have more blocks than
    // the blk_cnt() says because it may be keeping some unused blocks for
    // future allocations (this is the fblkarr.capacity()).
    //
    // the call to fblkarr.close() *should* release those blocks and resize
    // the file to the correct size.
    // the caveat is that it feels fragile to store something without being
    // 100% sure that it is true -- TODO store fblkarr.capacity() ? may be
    // store more details of fblkarr?
    _write_header(trailer_sz, fblkarr.blk_cnt() + fblkarr.begin_blk_nr(), gp, root_sg_bytes);
    _write_trailer();

    fblkarr.close();
    closed = true;
}
