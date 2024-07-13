// Welcome to a XOZ demo/tutorial!
//
// This is the implementation of a very simple tar-like archiver using XOZ lib.
// It is missing a lot of features of a classic tar archiver but it shows all the basics
// things to use XOZ. That's the point!
//
// The code is fully documented *and* annotated with numbers like "(1)". You can read
// the source code from top to down or you can go from one section to another following
// the numbers.
//
// Enjoy!

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>

#include "xoz/dsc/descriptor.h"
#include "xoz/dsc/descriptor_set.h"
#include "xoz/repo/repository.h"


#if 1

// A Descriptor is the minimum unit of storage. You will have to implement yours
// based on your needs.
// In our case, we want to store "files" in the archiver so we create a descriptor
// File inheriting from Descriptor class
//
// Any descriptor *must*:
//  - say how to read/write the descriptor from/to the file. (1)
//  - keep the descriptor header updated. (2)
//  - call notify_descriptor_changed() on a change (10)
//  - implement a create() method to construct a descriptor object following
//    the signature required by XOZ (3)
//
// Any descriptor *may*:
//  - implement a create() method to construct a descriptor object from scratch
//    with an user-defined signature. (4)
//  - implement a custom way to destroy a descriptor. See (5)
//  - delay writes to disk until flush_writes is called. (no example)
//  - delay release of free space until release_free_space is called. (no example)
//
// The File descriptor also offers a way to get/set the file name and to extract
// the file content. (7), (8), (9)
//
class File: public Descriptor {

public:
    File(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr, const std::string& fname, uint16_t fname_sz):
            Descriptor(hdr, ed_blkarr), fname_cache(fname), fname_sz(fname_sz) {}

    // (4) create() method with user-defined signature.
    //
    // It creates a descriptor object from scratch: it does a lot of things, stores a lot
    // of data and creates the header (hdr).
    //
    // Because it is meant to be called by the user, it is likely to be used/modified
    // immediately so we return a pointer to File and  not to Descriptor.
    static std::unique_ptr<File> create(const std::string& fpath, BlockArray& ed_blkarr) {
        // Let's open the file that we want to save in XOZ
        std::ifstream file(fpath, std::ios_base::in | std::ios_base::binary);
        if (!file) {
            throw std::runtime_error("File could not be open");
        }

        // As an example we will store the size of file name in the "data"
        // The store will happen in write_struct_specifics_into(), not here.
        //
        // One minor caveat, the "data" must have a size multiple of 2 so we need to take that
        // into account when checking the size.
        std::string fname = std::filesystem::path(fpath).filename().string();
        uint32_t fname_sz = assert_u32(fname.size());  // cppcheck-suppress shadowVariable

        // We want to store the content of the file in XOZ so we need to measure how much
        // space do we need.
        auto begin = file.tellg();
        file.seekg(0, std::ios_base::end);
        auto file_sz = assert_u32(file.tellg() - begin);

        file.seekg(0);  // rewind

        // Each descriptor has two areas to store data: the "data" section embedded in the
        // descriptor structure and the "external" section.
        //
        // The "data" section is for small things that are frequently updated; the "external"
        // section is for very large, less frequently updated data.
        //
        // We're going to store the file name and the file content in the "external" section.
        // First, let's see how much do we need:
        auto total_alloc_sz = fname_sz + file_sz;

        // Strictly speaking the maximum size of data that we could save in XOZ is 1GB (minus 1 byte)
        // but in practice depends of the block size (smaller block sizes will force to reduce the maximum)
        // and how much fragmentation we tolerate (how large the Segment will become).
        // In practice, the upper limit may be below this 1GB limit. This precise check is still missing in XOZ
        // and it may happen during the allocation.
        //
        // The is_esize_greater_than_allowed only fails if it is larger than 1GB.
        if (Descriptor::is_esize_greater_than_allowed(total_alloc_sz)) {
            throw std::runtime_error("File is too large");
        }

        // We take the "external" data block array's allocator and we allocate the required amount
        // of bytes. XOZ will do all the necessary things to find enough space without fragmenting
        // to much the xoz file or spreading the content too much.
        // If the xoz file is too small, it will grow automatically.
        //
        // The alloc() method will return a "segment". A segment is a series of blocks numbers
        // that points to the allocated space.
        Segment segm = ed_blkarr.allocator().alloc(total_alloc_sz);

        // The Descriptor API says that the segment stored in the header must be
        // inline-data ended.
        //
        // By default when we call alloc() we get inline-data ended segments
        // (in particular, part of the content that we wrote in the writeall() call above
        // was stored in this inline-data section).
        //
        // Nevertheless, it does not hurt to ensure that the segment ends with a inline-data
        // section.
        segm.add_end_of_segment();

        // The blocks pointed by a segment are not necessary contiguous but we can see them
        // as such using an IOSegment (or 'io' object for short).
        IOSegment io(ed_blkarr, segm);

        // Let's write the file name (no null will be stored)
        io.writeall(fname.c_str(), fname_sz);

        // Now, copy the file content to the io object. Writing to the io will
        // write directly to xoz file.
        io.writeall(file);

        // We are storing the file name and file content in the same place,
        // one after the other. To know where one ends and the other starts we
        // manually tracking the file name size.
        // This will be stored in write_struct_specifics_into() (see (1))
        // in a uint16_t (2 bytes, this is the dsize in hdr)
        //
        // Note: This is "ok" for File, which it is a very simple descriptor, but it is
        // not ok for more complex ones that work with variable-size fields.
        // Tracking manually where each field starts and forcing us to do a single alloc
        // at the begin is not user-friendly.
        //
        // This is a limitation of current xoz implementation (of IOBase and the allocator)
        // and this is why the rename() functionality of tarlike is disabled
        // (see (8) and (11))
        // Sorry.
        //
        // For fixed-size fields, there is no problem.

        struct header_t hdr = {
                // The descriptor owns external data so we set this to True
                .own_edata = true,

                // Each descriptor has a type that denotes its nature. File Descriptors
                // have the 0xab type. Of course this was chosen by me for this demo.
                // The type should be very well documented in a RFC or like.
                .type = 0xab,

                // Each descriptor has an id, either a temporal id or a persistent id.
                // If we put 0x0, XOZ will assign an id for us when the descriptor is added
                // to the descriptor set (6)
                .id = 0x0,

                .dsize = assert_u8(sizeof(uint16_t)),
                .esize = assert_u32(total_alloc_sz),

                .segm = segm,  // the location of the external data (our file)
        };

        // Call the Descriptor constructor and build a File object.
        return std::make_unique<File>(hdr, ed_blkarr, fname, assert_u16(fname_sz));
    }

    // (3)
    //
    // This is the create function for when the descriptor is loaded from the xoz file
    // Notice how it is very simple and just creates the correct File object: it does
    // not read/write anything and the header (hdr) is already created by the XOZ lib.
    //
    // The signature of this method is defined and required by XOZ. It cannot be changed.
    // If you do, it will not compile when we add this function to the initialization
    // of the descriptor mapping (see main()).
    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& ed_blkarr,
                                              [[maybe_unused]] RuntimeContext& rctx) {
        return std::make_unique<File>(hdr, ed_blkarr, "", 0);
    }

public:
    // (7)
    //
    // TODO We cannot make get_fname a const method.
    //
    // This is a limitation of the IOSegment API: it expects a non-const
    // reference to the segment, even if we are going to read and only read.
    // Because we are in a const method (get_fname), hdr.segm is a const reference
    // so we are forced to do a local non-const copy to work with IOSegment.
    // The same happens with the blkarray reference that it must be non-const
    // even we are going to only read.
    std::string get_fname() {
        if (fname_cache == "") {
            std::vector<char> buf(fname_sz);

            IOSegment io(external_blkarr(), hdr.segm);
            io.readall(buf);

            fname_cache.assign(buf.data(), fname_sz);
        }

        return fname_cache;
    }

    // (8)
    /*
    void set_fname(std::string new_fname) {
        // Not supported for now
    }
    */

    // (9)
    void extract() {
        std::fstream f;
        auto fname = get_fname();
        f.open(fname, std::ios_base::in);
        if (f) {
            // Do not extract the file if such exists already in the user's working directory
            return;
        }

        f.open(fname, std::ios_base::out | std::ios_base::binary);
        if (!f) {
            std::cerr << "Error trying to extract '" << fname << "'\n";
            throw std::runtime_error("File could not be open");
        }

        // Create an IO object to read from the xoz file and write
        // the file contents. We use hdr.segm that contains the Segment (aka blocks)
        // that hold the data.
        // We use the "external" data block array, the same that we used to store the file
        // in the create() method (4).
        IOSegment io(external_blkarr(), hdr.segm);
        io.seek_rd(fname_sz);

        // Read all and dump it into f
        io.readall(f);
    }

protected:
    // (1)
    //
    // These two methods are in charge to read from the io or write into the io object
    // the "data" of the descriptor. This happens when the descriptor is being read from
    // or write to the xoz file.
    //
    // Here "data" means the small private data section that every descriptor has
    // (this is not the "external" data).
    //
    // As we did in (4), we expect to read or write the size of the file name size.
    // Again, more complex structures could be read/written here (if they fit).
    //
    // Note: the content of the "data" section *must* by a multiple of 2 and it must
    // be below up to a maximum of 127 bytes.
    void read_struct_specifics_from(IOBase& io) override { fname_sz = io.read_u16_from_le(); }

    void write_struct_specifics_into(IOBase& io) override { io.write_u16_to_le(fname_sz); }

    // (2) Update this->hdr
    //
    // This is *mandatory*: you need to keep updated
    // the header's attributes because a descriptor may be written to disk
    // in any moment and the header needs to reflect the latest state.
    void update_header() override {
        // No change can happen on File so nothing needs to be updated
    }

    // (5)
    //
    // This method will be called when the descriptor is erased from a descriptor set
    // It should be called once by xoz.
    // By this moment, the descriptor is "effectively" being removed from the file.
    //
    // Subclasses may override this to do any clean up like, typically, deallocating
    // the segment.
    void destroy() override {
        // Dealloc/free the segment that contains the file so the space
        // can be used by other descriptors
        //
        // xoz considers any space not explicitly owned by a descriptor
        // as free so if for some reason we "forget" to dealloc our segment,
        // the space will be considered free on the next reopen of the xoz file.
        // It is a kind of garbage collection.
        if (hdr.segm.length() > 0) {
            external_blkarr().allocator().dealloc(hdr.segm);
        }
    }

private:
    std::string fname_cache;
    uint16_t fname_sz;
};


void add_file(Repository& repo, const std::string& fname) {
    // We want to create a new File descriptor. Error handling is up to you;
    // in my case I will just print the error and skip the addition of the
    // descriptor to the set.
    //
    // Note how the "external" data block array passed to File::create
    // is the same than the std::shared_ptr<DescriptorSet> dset has. This is important
    // because both the descriptors of the set and the set itself must
    // agree on which block array to use.
    std::shared_ptr<DescriptorSet> dset = repo.root();
    std::unique_ptr<File> f;
    try {
        f = File::create(fname, repo.expose_block_array());

        // Check that we can add it to the set. This is meant to catch
        // mostly null ptrs and reused ids (basically, bugs).
        // It is a good practice however.
        dset->fail_if_not_allowed_to_add(f.get());
    } catch (const std::exception& err) {
        std::cout << "[err] File " << fname << " add failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    // (6)
    //
    // Add the descriptor to the set. The flag 'true' is for assign_persistent_id = true.
    // This means that if the descriptor does not have an id (aka id = 0x0) or it has
    // a temporal id, it will replace this by a persistent id.
    //
    // Such id is returned and we print it to the user so he/she can delete/extract
    // the file by referencing it by id (this is why also needs to be persistent,
    // the id must survive reopening of the xoz file).
    uint32_t id = dset->add(std::move(f), true);
    std::cout << "[ID " << id << "] File " << fname << " added.\n";
}

void del_file(std::shared_ptr<DescriptorSet>& dset, int id_arg) {
    uint32_t id = assert_u32(id_arg);
    // Get the file, print its name and then erase it from the xoz file.
    //
    // The get<File>() method will throw if the id does not exist or if
    // the descriptor exists but it cannot be downcasted to File.
    //
    // Error handling is up to you; in my case I will just print the error
    // and move on.
    std::shared_ptr<File> f;
    try {
        f = dset->get<File>(id);
    } catch (const std::exception& err) {
        std::cout << "[err] File id " << id << " get failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    // Removing a descriptor from a set deletes automatically the "external"
    // data (aka, the stored file)
    dset->erase(id);
    std::cout << "[ID " << id << "] File " << f->get_fname() << " removed.\n";
}

void extract_file(std::shared_ptr<DescriptorSet>& dset, int id_arg) {
    uint32_t id = assert_u32(id_arg);
    std::shared_ptr<File> f;
    try {
        // Nothing new here: get the descriptor and extract the stored file.
        f = dset->get<File>(id);
        f->extract();
    } catch (const std::exception& err) {
        std::cout << "[err] File id " << id << " get/extract failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    std::cout << "[ID " << id << "] File " << f->get_fname() << " extracted\n";
}

/*
 (11)
void rename_file(std::shared_ptr<DescriptorSet>& dset, int id_arg, const std::string& new_name) {
    uint32_t id = assert_u32(id_arg);
    std::shared_ptr<File> f;
    try {
        f = dset->get<File>(id);
        f->set_fname(new_name);
    } catch (const std::exception& err) {
        std::cout << "[err] File id " << id << " rename failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    // Because we changed the descriptor we need to mark it as "modified"
    // so the set can know that it should schedule a write to disk with this update
    // However, if the descriptor was coded correctly, this should not be needed
    // because any descriptor that it is changed it should notify about the change
    // without requiring the caller (us) to do an explicit call to mark_as_modified().
    //
    // If you need to do this call, 99% is probably that the descriptor is wrong.
    // Putting this here just for documentation and demo.
    dset->mark_as_modified(id);
    std::cout << "[ID " << id << "] File " << f->get_fname() << " renamed.\n";
}
*/

void list_files(std::shared_ptr<DescriptorSet>& dset) {
    // The descriptor set supports a C++ classic iteration.
    // The iterators has the method as<> to downcast descriptors
    // as get<> does.
    for (auto it = dset->begin(); it != dset->end(); ++it) {
        auto f = (*it)->cast<File>();
        std::cout << "[ID " << f->id() << "] File " << f->get_fname() << "\n";
    }
}

void stats(const Repository& repo) { std::cout << repo << "\n"; }

void print_usage() {
    std::cerr << "Missing/Bad arguments\n";
    std::cerr << "Usage:\n";
    std::cerr << "  add files:      tarlike <file.xoz> a <file name> [<file name>...]\n";
    std::cerr << "  delete files:   tarlike <file.xoz> d <file id> [<file id>...]\n";
    std::cerr << "  extract files:  tarlike <file.xoz> x <file id> [<file id>...]\n";
    // std::cerr << "  rename a file:  tarlike <file.xoz> r <file id> <new file name>\n";
    std::cerr << "  list files:     tarlike <file.xoz> l\n";
    std::cerr << "  show stats:     tarlike <file.xoz> s\n";
}

int main(int argc, char* argv[]) {
    int ret = -1;

    if (argc < 3 or argv[2][0] == 0 or argv[2][1] != 0) {
        print_usage();
        return ret;
    }

    DescriptorMapping dmap({{0xab, File::create}});

    Repository repo = Repository::create(dmap, argv[1]);

    auto dset = repo.root();

    ret = 0;
    try {
        switch (argv[2][0]) {
            case 'a':
                // add
                if (argc == 3) {
                    std::cerr << "Missing the name of the file(s) to add\n";
                    ret = -2;
                    break;
                }
                for (int i = 3; i < argc; ++i) {
                    add_file(repo, argv[i]);
                }
                break;
            case 'd':
                // delete
                if (argc == 3) {
                    std::cerr << "Missing the id(s) of the file(s) to remove\n";
                    ret = -2;
                    break;
                }
                for (int i = 3; i < argc; ++i) {
                    del_file(dset, atoi(argv[i]));
                }
                break;
            case 'x':
                // extract
                if (argc == 3) {
                    std::cerr << "Missing the id(s) of the file(s) to extract\n";
                    ret = -2;
                    break;
                }
                for (int i = 3; i < argc; ++i) {
                    extract_file(dset, atoi(argv[i]));
                }
                break;
            case 'r':
                // rename
                if (argc == 3) {
                    std::cerr << "Missing the id of the file to rename\n";
                    ret = -2;
                    break;
                } else if (argc == 4) {
                    std::cerr << "Missing the new file name of the file to rename\n";
                    ret = -2;
                    break;
                } else if (argc > 5) {
                    std::cerr << "Too many arguments\n";
                    ret = -2;
                    break;
                }

                rename_file(dset, atoi(argv[3]), argv[4]);
                break;
            case 'l':
                // list
                if (argc != 3) {
                    std::cerr << "Too many arguments\n";
                    ret = -2;
                    break;
                }
                list_files(dset);
                break;
            case 's':
                // stats
                if (argc != 3) {
                    std::cerr << "Too many arguments\n";
                    ret = -2;
                    break;
                }
                stats(repo);
                break;
            default:
                std::cerr << "Unknown command.\n";
                print_usage();
                ret = -2;
                break;
        }
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        ret = -3;
    }

    try {
        // Ensure everything is written to disk
        repo.close();
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        ret = -4;

        // This is bad: it is very likely that there is a bug in either the xoz lib
        // or in one of the descriptors.
        // The only thing we can do is to try to close the file. At this moment
        // we may end up with a corrupted file.
        repo.panic_close();
    }

    return ret;
}
#else

int main() { return 0; }

#endif
