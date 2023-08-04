#include <iostream>
#include "xoz/repo/repo.h"
#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/segm_allocator.h"

#include <map>

class Demo {
    private:
    SegmentAllocator& sg_alloc;
    const SegmentAllocator::req_t& req;

    int next_segm_id = 1;
    std::map<int, Segment> segm_by_id;

    public:
        Demo(SegmentAllocator& sg_alloc, const SegmentAllocator::req_t& req) : sg_alloc(sg_alloc), req(req) {}

        void alloc() {
            uint32_t sz;
            std::cin >> sz;

            Segment segm = sg_alloc.alloc(sz, req);
            segm_by_id[next_segm_id] = segm;
            std::cout << next_segm_id << " ";
            ++next_segm_id;

            std::cout << segm.ext_cnt() << " ";
            for (auto const& ext : segm.exts()) {
                std::cout << ext.is_suballoc() << " " << ext.blk_nr() << " ";
                if (ext.is_suballoc()) {
                    std::cout << ext.blk_bitmap() << " ";
                } else {
                    std::cout << ext.blk_cnt() << " ";
                }
            }

            std::cout << std::endl;
        }

        void dealloc() {
            uint32_t tmp_segm_id;
            std::cin >> tmp_segm_id;

            auto it = segm_by_id.find(tmp_segm_id);
            if (it == segm_by_id.end()) {
                assert(0);
            }

            sg_alloc.dealloc(it->second);
            segm_by_id.erase(it);

            std::cout << std::endl;
        }

        void release() {
            sg_alloc.release();
            std::cout << std::endl;
        }

        void stats() {
            PrintTo(sg_alloc, &std::cout);
            std::cout << std::endl;
        }

};


int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    const GlobalParameters gp = {
        .blk_sz = 512,
        .blk_sz_order = 9,
        .blk_init_cnt = 1
    };

    bool coalescing_enabled = false;
    uint16_t split_above_threshold = 0;
    uint16_t segm_frag_threshold = 0;
    bool allow_suballoc = false;

    std::cin >> coalescing_enabled >> split_above_threshold >> segm_frag_threshold >> allow_suballoc;

    if (std::cin.fail() or std::cin.eof()) {
        return -1;
    }

    const SegmentAllocator::req_t req = {
        .segm_frag_threshold = segm_frag_threshold,
        .max_inline_sz = 0,
        .allow_suballoc = allow_suballoc
    };

    Repository repo = Repository::create_mem_based(0, gp);
    SegmentAllocator sg_alloc(repo, coalescing_enabled, split_above_threshold);

    Demo demo(sg_alloc, req);

    int cmd = 0;
    while (1) {
        std::cin >> cmd;

        if (std::cin.fail() or std::cin.eof()) {
            break;
        }

        switch (cmd) {
            case 0: // alloc
                demo.alloc();
                break;
            case 1: // dealloc
                demo.dealloc();
                break;
            case 2: // release
                demo.release();
                break;
            case 3: // stats
                demo.stats();
                break;
            case 4: // end
                break;
            default:
                assert(0);
        }
    }

    return 0;
}
