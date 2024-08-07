#include <iostream>
#include <map>

#include "xoz/alloc/segment_allocator.h"
#include "xoz/blk/file_block_array.h"
#include "xoz/err/exceptions.h"
#include "xoz/ext/extent.h"
#include "xoz/log/trace.h"

//#define TRACE_BEGIN do
//#define TRACE_END while(0)

#define TRACE_BEGIN \
    do {            \
        if (0)
#define TRACE_END \
    }             \
    while (0)

using namespace xoz;  // NOLINT

class Demo {
private:
    BlockArray& blkarr;
    SegmentAllocator& sg_alloc;
    const SegmentAllocator::req_t& req;

    uint32_t next_segm_id = 1;

    struct TSegment {
        Segment sg;

        TSegment(): sg(0) {}
        explicit TSegment(Segment sg): sg(sg) {}
    };

    std::map<uint32_t, TSegment> segm_by_id;

public:
    Demo(BlockArray& blkarr, SegmentAllocator& sg_alloc, const SegmentAllocator::req_t& req):
            blkarr(blkarr), sg_alloc(sg_alloc), req(req) {}

    void alloc() {
        uint32_t sz;
        std::cin >> sz;

        TRACE_BEGIN { std::cerr << "A " << sz << " bytes...\n"; }
        TRACE_END;

        Segment segm = sg_alloc.alloc(sz, req);
        segm_by_id[next_segm_id] = TSegment(segm);

        TRACE_BEGIN {
            std::cerr << "Ret: blocks in blkarr " << blkarr.blk_cnt() << "; ";
            std::cerr << "segment assigned " << next_segm_id << ", ";
            std::cerr << segm.ext_cnt() << " exts: ";
        }
        TRACE_END;


        std::cout << next_segm_id << " ";
        ++next_segm_id;

        std::cout << blkarr.blk_cnt() << " " << segm.ext_cnt() << " ";
        for (auto const& ext: segm.exts()) {
            std::cout << ext.is_suballoc() << " " << ext.blk_nr() << " ";
            if (ext.is_suballoc()) {
                std::cout << ext.blk_bitmap() << " ";
            } else {
                std::cout << ext.blk_cnt() << " ";
            }

            TRACE_BEGIN {
                PrintTo(ext, &std::cerr);
                std::cerr << ' ';
            }
            TRACE_END;
        }

        TRACE_BEGIN { std::cerr << '\n'; }
        TRACE_END;

        // format:
        // segm_id file_data_blk_cnt ext_cnt (is_suballoc blk_nr blk_cnt/bitmap)* \n
        std::cout << std::endl;
    }

    void dealloc() {
        uint32_t tmp_segm_id;
        std::cin >> tmp_segm_id;

        TRACE_BEGIN { std::cerr << "D segment " << tmp_segm_id << " "; }
        TRACE_END;

        auto it = segm_by_id.find(tmp_segm_id);
        if (it == segm_by_id.end()) {
            assert(0);
            throw std::runtime_error("impossible condition");
        }

        const Segment& segm = it->second.sg;

        TRACE_BEGIN {
            for (auto const& ext: segm.exts()) {
                PrintTo(ext, &std::cerr);
                std::cerr << ' ';
            }
            std::cerr << "...\n";
        }
        TRACE_END;


        sg_alloc.dealloc(segm);
        segm_by_id.erase(it);

        std::cout << blkarr.blk_cnt() << " ";

        TRACE_BEGIN { std::cerr << "Ret: blocks in blkarr: " << blkarr.blk_cnt() << "\n"; }
        TRACE_END;

        // format:
        // file_data_blk_cnt
        std::cout << std::endl;
    }

    void release() {
        TRACE_BEGIN { std::cerr << "R...\n"; }
        TRACE_END;

        sg_alloc.release();

        std::cout << blkarr.blk_cnt() << " ";

        TRACE_BEGIN { std::cerr << "Ret: blocks in blkarr: " << blkarr.blk_cnt() << "\n"; }
        TRACE_END;

        // format:
        // file_data_blk_cnt
        std::cout << std::endl;
    }

    void stats() {
        // format:
        // <pretty print>
        // EOF
        TRACE_BEGIN { std::cerr << "S...\n"; }
        TRACE_END;

        PrintTo(sg_alloc, &std::cout);
        std::cout << "\n"
                  << "EOF\n";

        TRACE_BEGIN { std::cerr << "Ret: done\n"; }
        TRACE_END;

        std::cout << std::endl;
    }
};


int main(int argc, char* argv[]) {
    const uint32_t blk_sz = 512;  // you can change this

    if (argc != 7)
        return -1;

    xoz::log::set_trace_mask_from_env();

    bool coalescing_enabled = argv[1][0] == '1';
    uint16_t split_above_threshold = assert_u16(atoi(argv[2]));
    uint16_t segm_frag_threshold = assert_u16(atoi(argv[3]));
    bool allow_suballoc = argv[4][0] == '1';
    bool allow_inline = argv[5][0] == '1';
    uint8_t inline_sz = assert_u8(atoi(argv[6]));

    if (std::cin.fail() or std::cin.eof()) {
        return -1;
    }

    const SegmentAllocator::req_t req = {.segm_frag_threshold = segm_frag_threshold,
                                         .max_inline_sz = allow_inline ? inline_sz : uint8_t(0),
                                         .allow_suballoc = allow_suballoc,
                                         .single_extent = false};

    auto fblkarr_ptr = FileBlockArray::create_mem_based(blk_sz);
    FileBlockArray& fblkarr = *fblkarr_ptr.get();
    SegmentAllocator sg_alloc(coalescing_enabled, split_above_threshold);
    sg_alloc.manage_block_array(fblkarr);

    Demo demo(fblkarr, sg_alloc, req);

    int cmd = 0;
    while (1) {
        std::cin >> cmd;

        if (std::cin.fail() or std::cin.eof()) {
            break;
        }

        switch (cmd) {
            case 0:  // alloc
                demo.alloc();
                break;
            case 1:  // dealloc
                demo.dealloc();
                break;
            case 2:  // release
                demo.release();
                break;
            case 3:  // stats
                demo.stats();
                break;
            case 4:  // end
                break;
            default:
                assert(0);
        }
    }
    return 0;
}
