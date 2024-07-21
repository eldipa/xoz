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
#include "xoz/file/file.h"

// A Descriptor is the minimum unit of storage. It is a base class from
// which the developer will subclass to create its own. In this demo,
// we will code a FileMember subclass
using xoz::Descriptor;

// Descriptors live in one and only one DescriptorSet; DescriptorSet are
// Descriptors by themselves.
using xoz::DescriptorSet;

// This is the xoz file. It contains a single "root" DescriptorSet where
// descriptors and sets live.
// In this demo, the "root" will only have FileMember descriptors.
using xoz::File;

// On loading a file from disk it is necessary to know which Descriptor subclass
// correspond to each part of data.
// DescriptorMapping makes that link.
using xoz::DescriptorMapping;

// Each Descriptor may own a Segment: a ordered list of Extents, each being
// a contiguous blocks of space within the xoz file.
using xoz::Segment;

// A BlockArray is the space where those blocks live. You can think it as
// the "usable" space of the xoz file (it is a little more than that but it is ok)
using xoz::BlockArray;

// Reading and writing blocks is cumbersome. Instead, we can use an IOSegment
// to see the blocks pointed by a Segment as a contiguous stream of bytes, very much
// like the API that std::fstream offers you. IOBase is its parent class.
using xoz::IOBase;
using xoz::IOSegment;

// Opaque object. Not used in this demo.
using xoz::RuntimeContext;


// A Descriptor is the minimum unit of storage. You will have to implement yours
// based on your needs.
// In our case, we want to store "files" in the archiver so we create a descriptor
// FileMember inheriting from Descriptor class
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
// The FileMember descriptor also offers a way to get/set the file name and to extract
// the file content. (7), (8), (9)
//
class FileMember: public Descriptor {

public:
    // Each descriptor has a type that denotes its nature. FileMember Descriptors
    // have the 0xab type. Of course this was chosen by me for this demo.
    // The type should be very well documented in a RFC or like.
    constexpr static uint16_t TYPE = 0x00ab;

    FileMember(const struct Descriptor::header_t& hdr, BlockArray& cblkarr, const std::string& fname, uint32_t file_sz,
               uint16_t fname_sz):
            Descriptor(hdr, cblkarr), fname(fname), file_sz(file_sz), fname_sz(fname_sz) {}

    // (4) create() method with user-defined signature.
    //
    // It creates a descriptor object from scratch: it does a lot of things, stores a lot
    // of data and creates the header (hdr).
    //
    // Because it is meant to be called by the user, it is likely to be used/modified
    // immediately so we return a pointer to FileMember and  not to Descriptor.
    static std::unique_ptr<FileMember> create(const std::string& fpath, BlockArray& cblkarr) {
        // Let's open the file that we want to save in XOZ
        std::ifstream file(fpath, std::ios_base::in | std::ios_base::binary);
        if (!file) {
            throw std::runtime_error("File could not be open");
        }

        // We want to store the content of the file in XOZ so we need to measure how much
        // space do we need.
        auto begin = file.tellg();
        file.seekg(0, std::ios_base::end);

        // cppcheck-suppress shadowVariable
        uint32_t file_sz = xoz::assert_u32(file.tellg() - begin);

        file.seekg(0);  // rewind

        // Also, we would like to store the name of the file
        // cppcheck-suppress shadowVariable
        std::string fname = std::filesystem::path(fpath).filename().string();
        uint16_t fname_sz = xoz::assert_u16(fname.size());  // cppcheck-suppress shadowVariable

        // Each descriptor has two areas to store data: the "internal" section embedded in the
        // descriptor structure and the "content" section.
        //
        // The "internal" section is for small things that are frequently updated; the "content"
        // section is for very large, less frequently updated data.
        //
        // We're going to store the file content and file name in the "content" section.
        // First, let's see how much do we need:
        auto total_alloc_sz = fname_sz + file_sz;

        // Strictly speaking the maximum size of data that we could save in XOZ is 1GB (minus 1 byte)
        // but in practice depends of the block size (smaller block sizes will force to reduce the maximum)
        // and how much fragmentation we tolerate (how large the Segment will become).
        // In practice, the upper limit may be below this 1GB limit. This precise check is still missing in XOZ
        // and it may happen during the allocation.
        //
        // The is_csize_greater_than_allowed only fails if it is larger than 1GB.
        if (Descriptor::is_csize_greater_than_allowed(total_alloc_sz)) {
            throw std::runtime_error("File + file name is too large");
        }

        // We take the "content" data block array's allocator and we allocate the required amount
        // of bytes. XOZ will do all the necessary things to find enough space without fragmenting
        // to much the xoz file or spreading the content too much.
        // If the xoz file is too small, it will grow automatically.
        //
        // The alloc() method will return a Segment. A segment is a series of blocks numbers
        // that points to the allocated space.
        Segment segm = cblkarr.allocator().alloc(total_alloc_sz);

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
        IOSegment io(cblkarr, segm);

        // Now, copy the file content to the io object. Writing to the io will
        // write directly to xoz file.
        io.writeall(file);

        // Then, write the file name (no null will be stored)
        io.writeall(fname.c_str(), fname_sz);

        // We said earlier than there are two store areas: the "internal" and the "content"
        // sections.
        //
        // We plan to store in the "internal" section the sizes of the file as uint32_t
        // and its name as uint16_t (6 bytes in total).
        //
        // The write is made in write_struct_specifics_into, no here, but we need
        // to declare the total size in the 'isize' field.

        struct header_t hdr = {
                // The descriptor owns content data so we set this to True
                .own_content = true,
                .type = TYPE,

                // Each descriptor has an id, either a temporal id or a persistent id.
                // If we put 0x0, XOZ will assign an id for us when the descriptor is added
                // to the descriptor set (6)
                .id = 0x0,

                .isize = xoz::assert_u8(sizeof(uint32_t) + sizeof(uint16_t)),
                .csize = xoz::assert_u32(total_alloc_sz),

                .segm = segm,  // the location of the content data (our file and file name)
        };

        // Call the Descriptor constructor and build a FileMember object.
        return std::make_unique<FileMember>(hdr, cblkarr, fname, file_sz, fname_sz);
    }

    // (3)
    //
    // This is the create function for when the descriptor is loaded from the xoz file
    // Notice how it is very simple and just creates the correct FileMember object: it does
    // not read/write anything and the header (hdr) is already created by the XOZ lib.
    //
    // The signature of this method is defined and required by XOZ. It cannot be changed.
    // If you do, it will not compile when we add this function to the initialization
    // of the descriptor mapping (see main()).
    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              [[maybe_unused]] RuntimeContext& rctx) {
        return std::make_unique<FileMember>(hdr, cblkarr, "", 0, 0);
    }

public:
    // (7)
    std::string get_fname() const { return fname; }

    // (8)
    void set_fname(const std::string& new_fname) {
        uint16_t new_fname_sz = xoz::assert_u16(new_fname.size());

        external_blkarr().allocator().realloc(hdr.segm, file_sz + new_fname_sz);
        hdr.segm.add_end_of_segment();

        IOSegment io(external_blkarr(), hdr.segm);
        io.seek_wr(file_sz);
        io.writeall(new_fname.c_str(), new_fname_sz);

        fname = new_fname;
        fname_sz = new_fname_sz;

        // (10)
        notify_descriptor_changed();
    }

    // (9)
    void extract() {
        std::fstream f;
        f.open(fname, std::ios_base::in);
        if (f) {
            // Do not extract the file if such exists already in the user's working directory
            std::cerr << "The file '" << fname << "' already exist. Extraction aborted.\n";
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
        // We use the "content" data block array, the same that we used to store the file
        // in the create() method (4).
        IOSegment io(external_blkarr(), hdr.segm);

        // Read the file content and dump it into f
        io.readall(f, file_sz);
    }

protected:
    // (1)
    //
    // These two methods are in charge to read from the io or write into the io object
    // the "internal" of the descriptor. This happens when the descriptor is being read from
    // or write to the xoz file.
    //
    // Here "internal" means the small private data section that every descriptor has
    // (this is not the "content" data).
    //
    // As we did in (4), we expect to read or write the size of the file and file name sizes.
    // Again, more complex structures could be read/written here (if they fit).
    //
    // Note: the content of the "internal" section *must* by a multiple of 2 and it must
    // be below up to a maximum of 127 bytes.
    void read_struct_specifics_from(IOBase& io) override {
        file_sz = io.read_u32_from_le();
        fname_sz = io.read_u16_from_le();

        // We *can* read from the "content" section if we want
        // Here, we read and put in memory the file name but we
        // left the file content unread in disk.
        //
        // To read from the "content" section, we create another IO
        // with the header's segment
        IOSegment ioe(external_blkarr(), hdr.segm);

        // The file name is stored immediately after the file content
        // so we seek to that position
        ioe.seek_rd(file_sz);

        // Read the string
        std::vector<char> buf(fname_sz);
        ioe.readall(buf);
        fname.assign(buf.data(), fname_sz);
    }

    void write_struct_specifics_into(IOBase& io) override {
        io.write_u32_to_le(file_sz);
        io.write_u16_to_le(fname_sz);
    }

    // (2) Update this->hdr
    //
    // This is *mandatory*: you need to keep updated
    // the header's attributes because a descriptor may be written to disk
    // in any moment and the header needs to reflect the latest state.
    void update_header() override {
        hdr.segm.add_end_of_segment();
        hdr.csize = hdr.segm.calc_data_space_size();
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
    std::string fname;
    uint32_t file_sz;
    uint16_t fname_sz;
};


void add_file(File& xfile, const std::string& fname) {
    // We want to create a new FileMember descriptor. Error handling is up to you;
    // in my case I will just print the error and skip the addition of the
    // descriptor to the set.
    //
    // Note how the "content" data block array passed to FileMember::create
    // is the same than the std::shared_ptr<DescriptorSet> dset has. This is important
    // because both the descriptors of the set and the set itself must
    // agree on which block array to use.
    std::shared_ptr<DescriptorSet> dset = xfile.root();
    std::unique_ptr<FileMember> f;
    try {
        f = FileMember::create(fname, xfile.expose_block_array());

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
    uint32_t id = xoz::assert_u32(id_arg);
    // Get the file, print its name and then erase it from the xoz file.
    //
    // The get<FileMember>() method will throw if the id does not exist or if
    // the descriptor exists but it cannot be downcasted to FileMember.
    //
    // Error handling is up to you; in my case I will just print the error
    // and move on.
    std::shared_ptr<FileMember> f;
    try {
        f = dset->get<FileMember>(id);
    } catch (const std::exception& err) {
        std::cout << "[err] File id " << id << " get failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    // Removing a descriptor from a set deletes automatically the "content"
    // data (aka, the stored file)
    dset->erase(id);
    std::cout << "[ID " << id << "] File " << f->get_fname() << " removed.\n";
}

void extract_file(std::shared_ptr<DescriptorSet>& dset, int id_arg) {
    uint32_t id = xoz::assert_u32(id_arg);
    std::shared_ptr<FileMember> f;
    try {
        // Nothing new here: get the descriptor and extract the stored file.
        f = dset->get<FileMember>(id);
        f->extract();
    } catch (const std::exception& err) {
        std::cout << "[err] File id " << id << " get/extract failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    std::cout << "[ID " << id << "] File " << f->get_fname() << " extracted\n";
}

// (11)
void rename_file(std::shared_ptr<DescriptorSet>& dset, int id_arg, const std::string& new_name) {
    uint32_t id = xoz::assert_u32(id_arg);
    std::shared_ptr<FileMember> f;
    try {
        f = dset->get<FileMember>(id);
        f->set_fname(new_name);
    } catch (const std::exception& err) {
        std::cout << "[err] File id " << id << " rename failed:\n";
        std::cout << err.what() << "\n";
        return;
    }

    // Because we changed the descriptor we need to mark it as "modified"
    // so the set can know that it should schedule a write to disk with this update
    //
    // In theory, all the descriptors should call notify_descriptor_changed() internally.
    // However, and just to be sure, you can call mark_as_modified() on the set:
    // this should not be necessary but it does not do any harm
    // (in fact the FileMember descriptor calls notify_descriptor_changed() so it is *not* necessary)
    dset->mark_as_modified(id);
    std::cout << "[ID " << id << "] File " << f->get_fname() << " renamed.\n";
}

void list_files(std::shared_ptr<DescriptorSet>& dset) {
    // The descriptor set supports a C++ classic iteration.
    // The iterators has the method cast<> to downcast descriptors
    // as get<> does.
    for (auto it = dset->begin(); it != dset->end(); ++it) {
        auto f = (*it)->cast<FileMember>();
        std::cout << "[ID " << f->id() << "] File " << f->get_fname() << "\n";
    }
}

void stats(const File& xfile) {
    // Writing a xfile to stdout will pretty print the statistics of the xoz file
    // Check the documentation in the source code of File, BlockArray and SegmentAllocator.
    std::cout << xfile << "\n";
}

void print_usage() {
    std::cerr << "Missing/Bad arguments\n";
    std::cerr << "Usage:\n";
    std::cerr << "  add files:      tarlike <file.xoz> a <file name> [<file name>...]\n";
    std::cerr << "  delete files:   tarlike <file.xoz> d <file id> [<file id>...]\n";
    std::cerr << "  extract files:  tarlike <file.xoz> x <file id> [<file id>...]\n";
    std::cerr << "  rename a file:  tarlike <file.xoz> r <file id> <new file name>\n";
    std::cerr << "  list files:     tarlike <file.xoz> l\n";
    std::cerr << "  show stats:     tarlike <file.xoz> s\n";
}

int main(int argc, char* argv[]) {
    int ret = -1;

    if (argc < 3 or argv[2][0] == 0 or argv[2][1] != 0) {
        print_usage();
        return ret;
    }

    // xoz library needs to know how to map descriptor types (integers)
    // to C++ classes, in particular, to factory methods (create())
    DescriptorMapping dmap({{FileMember::TYPE, FileMember::create}});

    // Create a xfile. Create a fresh new xoz file or open it if already exists.
    File xfile = File::create(dmap, argv[1]);

    // Each xfile has one root descriptor set. A set can then have more sets within (subsets)
    // but in this demo we are not exploring that.
    auto dset = xfile.root();

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
                    add_file(xfile, argv[i]);
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
                stats(xfile);
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
        xfile.close();
    } catch (const std::exception& err) {
        std::cerr << err.what() << "\n";
        ret = -4;

        // This is bad: it is very likely that there is a bug in either the xoz lib
        // or in one of the descriptors.
        // The only thing we can do is to try to close the file. At this moment
        // we may end up with a corrupted file.
        xfile.panic_close();
    }

    return ret;
}
