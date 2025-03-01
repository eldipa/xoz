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
// Descriptors by themselves and therefore a set can belong to another set.
using xoz::DescriptorSet;

// This is the xoz file. It contains a single "root" DescriptorSet where
// descriptors and sets live.
// This induces a tree-like structure: the File contains a single "root" DescriptorSet
// which may have zero or more sets and so on. Cycles are not allowed.
//
// In this demo, the "root" will only have FileMember descriptors so we will not
// have subsets inside.
using xoz::File;

// On loading a file from disk it is necessary to know which Descriptor subclass
// correspond to each part of data.
// DescriptorMapping makes that link.
//
// It is not magic: DescriptorMapping knows *only* about the xoz descriptor subclasses
// and has *no* idea of yours (like FileMember). We will have to teach it about that!
using xoz::DescriptorMapping;

// Each Descriptor may own a Segment: a ordered list of Extents, each being
// a contiguous blocks of space within the xoz file.
//
// A BlockArray is the space where those blocks live. You can think it as
// the "usable" space of the xoz file (it is a little more than that but it is ok)
//
// You are developer of *your* file format (like tarlike) using xoz as a library
// should not have to deal with neither Segment, Extent not BlockArray but you
// may see it in the documentation.
//
// Here need to have a reference to BlockArray but that's all.
using xoz::BlockArray;

// Reading and writing blocks is cumbersome. Instead, we can use an IOSegment
// to see the blocks pointed by a Segment as a contiguous stream of bytes, very much
// like the API that std::fstream offers you. IOBase is its parent class.
//
// These are the high-level classes that you should be working with. (instead
// of Segment, Extent or BlockArray).
using xoz::IOBase;
using xoz::IOSegment;

// Opaque object. Not used in this demo but used internally by xoz.
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
// Important!: there are two ways to think and use Descriptors.
//
// One is to use the descriptors to hold and maintain the state of the application.
// In tarlike we have FileMember which it is a Descriptor that handles the load and store
// of the state to disk but *also* maintains the state. For example, the file name is stored
// in memory in FileMember and the rest of the application (functions add_file and others)
// interacts directly with FileMember (for example, with set_fname).
//
// We call this "stateful descriptors"
//
// The other way to think and use Descriptors is to separate (decouple) the state.
// Hypothetically you would have "two" FileMember classes (FileMemberObject, FileMemberDescriptor):
// one has the state (and methods) specific for the application (the "object") and
// the other handles the load and store (the "descriptor").
// To link both the object need to "notify" the descriptor that the object changed
// and the descriptor then can opt read the object's state and store it in disk
// (or it may keep a flag and delay the write for later).
// This way is slightly more complex than the former so it is not covered in this tarlike demo.
//
// We call this "stateless descriptors"
//
// Which "ways" of using descriptors you should choose? It depends.
// For tarlike which has little logic the "stateful" way is simpler and works perfectly.
//
// Imagine now that you want to display on screen the content of each FileMember
// on user request, like being a file browser. In the "stateful" way you then will store
// anything required to display in the FileMember class. This coples your file format
// with your application.
//
// It's fine but if you don't want to? The "stateless" way puts the application related
// state (lke the display) in its own class leaving decoupling it from the descriptor.
//
// The important thing is: xoz is not forcing you to do one or the other. It is your choice.
//
class FileMember: public Descriptor {

public:
    // Each descriptor has a type that denotes its nature. FileMember Descriptors
    // have the 0xab type. Of course this was chosen by me for this demo.
    // The type should be very well documented in a RFC or like.
    //
    // Note: some are reserved and cannot be used for you.
    constexpr static uint16_t TYPE = 0x00ab;


    // (4) create() method with user-defined signature.
    //
    // It creates a descriptor object from scratch: it does a lot of things, stores a lot
    // of data and creates the header (hdr).
    //
    // Because it is meant to be called by the user, it is likely to be used/modified
    // immediately so we return a pointer to FileMember and  not to Descriptor.
    //
    // In fact you can create more than one "create" method and even named different
    // (the name "create" is only a suggestion)
    static std::unique_ptr<FileMember> create(const std::string& fpath, BlockArray& cblkarr) {
        // Call the Descriptor constructor and build a FileMember descriptor.
        return std::unique_ptr<FileMember>(new FileMember(cblkarr, fpath));
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
    //
    // Technically you could rename it to something else but by convention we call it create()
    static std::unique_ptr<Descriptor> create(const struct Descriptor::header_t& hdr, BlockArray& cblkarr,
                                              [[maybe_unused]] RuntimeContext& rctx) {
        return std::unique_ptr<FileMember>(new FileMember(hdr, cblkarr));
    }

public:
    //
    // We could have been implemented this method in different ways with different tradeoffs
    //
    //  - cold: we could make get_fname() to go to disk every time to read it
    //  (like calling get_content_io() and then read from it)
    //  - warm-on-demand: we could make get_fname() to go to disk only the first time and then
    //  keep a copy in memory
    //  - warm: the file name is loaded in complete_load() and it remains in memory
    //  (this is how *our* get_fname() is implemented).
    //
    std::string get_fname() const { return fname; }

    //
    // We could have been implemented this method in different ways with different tradeoffs
    //
    //  - immediate: we could make set_fname() to go to disk every time to write it
    //  (like calling get_content_io() and then write into)
    //  - deferred: the file name is written by flush_writes() when the descriptor set
    //  owned considers that it is time to sync and flush any pending write.
    //  (this is how *our* set_fname() is implemented).
    //
    void set_fname(const std::string& new_fname) {
        fname_sz = xoz::assert_u16(new_fname.size());
        fname = new_fname;

        // (10)
        // We let know the descriptor set owner of us that we changed.
        // We must call notify_descriptor_changed() if we have some pending
        // writes and/or the size of the descriptor changed.
        //
        // In our case we have both conditions:
        //  - the fname is pending to be written (see flush_writes())
        //  - the content size changed (the fname_sz changed)
        //
        notify_descriptor_changed();
    }

    //
    // Contrary to get_fname(), in extract() we chosen the "cold" implementation:
    // we read from the disk (from get_content_io()) *every time* that the caller
    // calls extract().
    //
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

        // Read the file content and dump it into f
        // The file is at the begin of the content section so we don't need to do a seek_rd
        auto io = get_content_io();
        io.readall(f, file_sz);
    }

    uint64_t get_total_size() const { return fname_sz + file_sz; }

protected:
    //
    // Complete the load of the descriptor.
    // In this method is where we should read the content (from get_content_io())
    // if we want.
    //
    // In our case, it was our decision to load "cold" the file content
    // and "warm" the file name.
    //
    void complete_load() override {
        auto content_io = get_content_io();
        content_io.turn_read_only();  // not necessary, just a safe check for us

        // The file name is stored immediately after the file content
        // so we seek to that position
        content_io.seek_rd(file_sz);

        // Read the file name
        std::vector<char> buf(fname_sz);

        content_io.readall(buf);
        fname.assign(buf.data(), fname_sz);
    }

    //
    // Called every time the descriptor set (our owner) is notified that
    // we have pending writes (either the descriptor calling notify_descriptor_changed()
    // or someone else calling on the descriptor set mark_as_modified()).
    //
    // Note: currently, it is not possible in xoz to "mark" what things are pending
    // to write and which don't.
    // Moreover, how the content data section works, resize_content() almost implies
    // that flush_writes() must rewrite everything.
    void flush_writes() override {
        // Do a resize of the content. Note that this will destroy the
        // information at the end of the content section if the content
        // is being shrinking.
        //
        // Thankfully, the file name is at the end anyways and it is what
        // may change only.
        resize_content(file_sz + fname_sz);

        auto io = get_content_io();
        io.seek_wr(file_sz);
        io.writeall(fname.c_str(), fname_sz);
    }

protected:
    // (1)
    //
    // These two methods are in charge to read from the io or write into the io object
    // the "internal" of the descriptor. This happens when the descriptor is being read from
    // or write to the xoz file.
    //
    // A Descriptor has two areas for storage: the "internal" and the "content".
    //
    // Here "internal" means the small private data section that every descriptor has
    // (this is not the "content" data).
    //
    // As we did in (4), we expect to read or write the size of the file and file name sizes.
    // Again, more complex structures could be read/written here (if they fit).
    //
    // Note 1: the content of the "internal" section *must* by a multiple of 2 and it must
    // be below up to a maximum of 127 bytes.
    //
    // Failing to do this will trigger an exception.
    //
    // Note 2: while you can read the content section,
    // you *must not write* it or resize it.
    // This is because de descriptors dimensions are still under construction during
    // the read_struct_specifics_from() and write_struct_specifics_into()
    //
    // Failing to do this may not trigger any exception but it could leave data corrupted.
    // Avoid reading from "content" here and move the reads to complete_load().
    void read_struct_specifics_from(IOBase& io) override {
        file_sz = io.read_u32_from_le();
        fname_sz = io.read_u16_from_le();
    }

    void write_struct_specifics_into(IOBase& io) override {
        io.write_u32_to_le(file_sz);
        io.write_u16_to_le(fname_sz);
    }

    // (2)
    //
    // Update the size of the internal and content areas. The method receives
    // the current sizes by reference so we can change them in-place.
    //
    // The sizes are uint64_t so we don't need to worry about
    // a casual overflow or wrap. The xoz library however, will
    // perform the checks for us.
    void update_sizes(uint64_t& isize, uint64_t& csize) override {
        isize = sizeof(uint32_t) + sizeof(uint16_t);
        csize = file_sz + fname_sz;
    }

    // (5)
    //
    // This method will be called when the descriptor is erased from a descriptor set
    // It should be called once by xoz.
    // By this moment, the descriptor is "effectively" being removed from the file.
    //
    // Subclasses may override this to do any custom clean up.
    // By default, Descriptor::destroy() deallocates the content (the segment).
    //
    // xoz considers any space not explicitly owned by a descriptor
    // as free so if for some reason we "forget" to dealloc our segment,
    // the space will be considered free on the next reopen of the xoz file.
    // It is a kind of garbage collection.
    void destroy() override { Descriptor::destroy(); }

private:
    std::string fname;
    uint32_t file_sz;
    uint16_t fname_sz;

    std::ifstream init_attributes(const std::string& fpath) {
        // Let's open the file that we want to save in XOZ
        std::ifstream file(fpath, std::ios_base::in | std::ios_base::binary);
        if (!file) {
            throw std::runtime_error("File could not be open");
        }

        // We want to store the content of the file in XOZ so we need to measure how much
        // space do we need.
        auto begin = file.tellg();
        file.seekg(0, std::ios_base::end);

        this->file_sz = xoz::assert_u32(file.tellg() - begin);

        file.seekg(0);  // rewind

        // Also, we would like to store the name of the file
        this->fname = std::filesystem::path(fpath).filename().string();
        this->fname_sz = xoz::assert_u16(fname.size());

        return file;
    }

    // friend std::unique_ptr<Descriptor> FileMember::create(const struct Descriptor::header_t& hdr, BlockArray&
    // cblkarr, RuntimeContext& rctx);


    // This C++ class constructor is mandatory so FileMember can be instantiated by xoz
    // This is called by the create() method above.
    FileMember(const struct Descriptor::header_t& hdr, BlockArray& cblkarr):
            Descriptor(hdr, cblkarr), fname(""), file_sz(0), fname_sz(0) {}

    // This C++ class constructor can have any signature (except that you need to provide
    // the BlockArray). It is called by the create() method.
    FileMember(BlockArray& cblkarr, const std::string& fpath): Descriptor(TYPE, cblkarr) {
        // Get from the path the filename (fname) and
        // calculate its size (fname_sz) and the size of the file (file_sz).
        // Leave the file open, we will use it soon.
        std::ifstream file = init_attributes(fpath);

        // We're going to store the file content and file name in the "content" section.
        // First, let's see how much do we need:
        auto content_alloc_sz = fname_sz + file_sz;

        // Check that we can actually store all the data calling does_present_csize_fit.
        //
        // Strictly speaking the maximum size of data that we could save in XOZ is 1GB (minus 1 byte)
        // but in practice depends of the block size (smaller block sizes will force to reduce the maximum)
        // and how much fragmentation we tolerate (how large the Segment will become).
        // In practice, the upper limit may be below this 1GB limit. This precise check is still missing in XOZ
        // and it may happen during the allocation.
        if (not does_present_csize_fit(content_alloc_sz)) {
            throw std::runtime_error("File + file name is too large");
        }

        // We allocate the required amount of bytes to hold the content.
        //
        // XOZ will do all the necessary things to find enough space without fragmenting
        // to much the xoz file or spreading the content too much.
        // If the xoz file is too small, it will grow automatically.
        resize_content(content_alloc_sz);

        // The xoz allocator (called in the resize_content above) works in terms
        // of Segments, a discontinuous series of extents (blocks)
        // Working with the allocator, the BlockArray and the Segments is hard.
        //
        // XOZ offers a better way: an 'io' object (IOSegment in this case)
        // to see the entire space (the content) as a contiguous byte string
        // very similar to a C++ file.
        auto io = get_content_io();

        // Now, copy the file content to the io object. Writing to the io will
        // write directly to xoz file.
        io.writeall(file);

        // Then, write the file name (no null will be stored)
        // Note how the 'io' works as a C++ file: we did a write and that automatically
        // moved the 'write pointer' so the second write (this one) took place
        // immediately after the former.
        io.writeall(fname.c_str(), fname_sz);
    }
};

// TODO add a example of DescriptorSet subclass


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

        // Note: we are creating a FileMember object that does not belong to any
        // set and then, (see below), we do dset->add(move(f)...)
        // In that moment the FileMember object is owned by the set.
        // We can merge both operations in a single call
        // dset->create_and_add<FileMember>(fname, std::ref(xfile.expose_block_array()))
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
    // the file by referencing it by id (this is why also needs to be persistented,
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
    // The iterators has the method deref_cast<> to downcast descriptors
    // as get<> does.
    //
    // Note: the descriptors also have a cast<> method so you could write instead
    // (*it)->cast<FileMember> **but** such casting operates on and returns raw
    // pointers and **not** shared pointers so you may end up having memory corruptions
    // Don't do that.
    for (auto it = dset->begin(); it != dset->end(); ++it) {
        auto f = it.deref_cast<FileMember>();
        std::cout << "[ID " << f->id() << "] File " << f->get_fname() << "\n";
    }
}

void stats(const File& xfile, std::shared_ptr<DescriptorSet>& dset) {
    // Writing a xfile to stdout will pretty print the statistics of the xoz file
    // Check the documentation in the source code of File, BlockArray and SegmentAllocator.
    std::cout << xfile << "\n";

    // Now we print tarlike-specific metrics
    uint64_t data_sz = 0;
    uint64_t fcount = 0;
    for (auto it = dset->begin(); it != dset->end(); ++it) {
        auto f = it.deref_cast<FileMember>();
        data_sz += f->get_total_size();
        ++fcount;
    }

    std::cout << "Tarlike:\n"
              << "- file count: " << fcount << "\n"
              << "- data size:  " << std::fixed << std::setprecision(2) << (double(data_sz) / (1024.0)) << " kb\n"
              << "\n";
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
                stats(xfile, dset);
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
