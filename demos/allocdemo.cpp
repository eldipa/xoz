#include <iostream>
#include "xoz/repo/repository.h"
#include "xoz/ext/extent.h"
#include "xoz/exceptions.h"
#include "xoz/alloc/segm_allocator.h"
#include "xoz/trace.h"

#include <map>

//#define TRACE_BEGIN do
//#define TRACE_END while(0)

#define TRACE_BEGIN do { if(0)
#define TRACE_END } while(0)

class Demo {
    private:
    Repository& repo;
    SegmentAllocator& sg_alloc;
    const SegmentAllocator::req_t& req;

    int next_segm_id = 1;
    std::map<int, Segment> segm_by_id;

    public:
        Demo(Repository& repo, SegmentAllocator& sg_alloc, const SegmentAllocator::req_t& req) : repo(repo), sg_alloc(sg_alloc), req(req) {}

        void alloc() {
            uint32_t sz;
            std::cin >> sz;

            TRACE_BEGIN {
                std::cerr << "A " << sz << " bytes...\n";
            } TRACE_END;

            Segment segm = sg_alloc.alloc(sz, req);
            segm_by_id[next_segm_id] = segm;

            TRACE_BEGIN {
                std::cerr << "Ret: blocks in repo " << repo.blk_cnt() << "; ";
                std::cerr << "segment assigned " << next_segm_id << ", ";
                std::cerr << segm.ext_cnt() << " exts: ";
            } TRACE_END;


            std::cout << next_segm_id << " ";
            ++next_segm_id;

            std::cout << repo.blk_cnt() << " " << segm.ext_cnt() << " ";
            for (auto const& ext : segm.exts()) {
                std::cout << ext.is_suballoc() << " " << ext.blk_nr() << " ";
                if (ext.is_suballoc()) {
                    std::cout << ext.blk_bitmap() << " ";
                } else {
                    std::cout << ext.blk_cnt() << " ";
                }

                TRACE_BEGIN {
                    PrintTo(ext, &std::cerr);
                    std::cerr << ' ';
                } TRACE_END;
            }

            TRACE_BEGIN {
                std::cerr << '\n';
            } TRACE_END;

            // format:
            // segm_id repo_data_blk_cnt ext_cnt (is_suballoc blk_nr blk_cnt/bitmap)* \n
            std::cout << std::endl;
        }

        void dealloc() {
            uint32_t tmp_segm_id;
            std::cin >> tmp_segm_id;

            TRACE_BEGIN {
                std::cerr << "D segment " << tmp_segm_id << " ";
            } TRACE_END;

            auto it = segm_by_id.find(tmp_segm_id);
            if (it == segm_by_id.end()) {
                assert(0);
            }

            const Segment& segm = it->second;

            TRACE_BEGIN {
                for (auto const& ext : segm.exts()) {
                    PrintTo(ext, &std::cerr);
                    std::cerr << ' ';
                }
                std::cerr << "...\n";
            } TRACE_END;


            sg_alloc.dealloc(segm);
            segm_by_id.erase(it);

            std::cout << repo.blk_cnt() << " ";

            TRACE_BEGIN {
                std::cerr << "Ret: blocks in repo: " << repo.blk_cnt() << "\n";
            } TRACE_END;

            // format:
            // repo_data_blk_cnt
            std::cout << std::endl;
        }

        void release() {
            TRACE_BEGIN {
                std::cerr << "R...\n";
            } TRACE_END;

            sg_alloc.release();

            std::cout << repo.blk_cnt() << " ";

            TRACE_BEGIN {
                std::cerr << "Ret: blocks in repo: " << repo.blk_cnt() << "\n";
            } TRACE_END;

            // format:
            // repo_data_blk_cnt
            std::cout << std::endl;
        }

        void stats() {
            // format:
            // <pretty print>
            // EOF
            TRACE_BEGIN {
                std::cerr << "S...\n";
            } TRACE_END;

            PrintTo(sg_alloc, &std::cout);
            std::cout << "\n" << "EOF\n";

            TRACE_BEGIN {
                std::cerr << "Ret: done\n";
            } TRACE_END;

            std::cout << std::endl;
        }

};


int main(int argc, char* argv[]) {
    const GlobalParameters gp = {
        .blk_sz = 512,
        .blk_sz_order = 9,
        .blk_init_cnt = 1
    };

    if (argc != 7)
        return -1;

    set_trace_mask_from_env();

    bool coalescing_enabled = argv[1][0] == '1';
    uint16_t split_above_threshold = (uint16_t)atoi(argv[2]);
    uint16_t segm_frag_threshold = (uint16_t)atoi(argv[3]);
    bool allow_suballoc = argv[4][0] == '1';
    bool allow_inline = argv[5][0] == '1';
    uint8_t inline_sz = (uint8_t)atoi(argv[6]);

    if (std::cin.fail() or std::cin.eof()) {
        return -1;
    }

    const SegmentAllocator::req_t req = {
        .segm_frag_threshold = segm_frag_threshold,
        .max_inline_sz = allow_inline ? inline_sz : (uint8_t)0,
        .allow_suballoc = allow_suballoc
    };

    Repository repo = Repository::create_mem_based(0, gp);
    SegmentAllocator sg_alloc(repo, coalescing_enabled, split_above_threshold);

    Demo demo(repo, sg_alloc, req);

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
