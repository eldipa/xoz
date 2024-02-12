## Compile and test

You need a `gcc` and/or `clang` compiler with C++20 support. You should be able to
compile the code with other compilers but it was not tested.

You will need also [tup v0.7.11](https://gittup.org/tup/index.html) and `make`
for compiling; `valgrind` for run some tests and `pre-commit` to run
some checkers and linters.

- `make` and `make test`: compile all the build variants of XOZ and run the tests for each variant
- `make compile`: compile all the build variants of XOZ
- `make valgrind`: compile and run the tests as above but run the test
    under Valgrind
- `pre-commit run --all-files`: run all the hooks:
    - `cppcheck`: static verifier
    - `cpplint`: linter
    - `clang-format`: source code formatter

### Full example

Install compilers and toolchain (Debian version)

```
$ sudo apt-get install build-essential clang valgrind
```

Download and install `tup v0.7.11`:

```
$ cd ~/
$ git clone https://github.com/gittup/tup.git

$ cd tup/
$ ./build.sh

$ sudo install build/tup /usb/bin/tup
```

Download and compile XOZ

```
$ cd ~/
$ git clone https://github.com/eldipa/xoz.git

$ cd xoz/
$ make
```

By default XOZ is compiled with both `gcc` and `clang`. If you want to
compile with only one of those run:

```
$ buildvariant=build-debug-gcc    make  # to use gcc only
$ buildvariant=build-debug-clang  make  # to use clang only
```

## How to read the source code? How XOZ is organized?

### Extent and Segments -  `xoz/ext/` and `xoz/segm/` folders

`extent.*` and `segment.*` define the two building-block
structures in XOZ, `Extent` and `Segment`.

Those two deal with all the encoding and low level details of the
[RFC Specification of Segment, Extent and Block Size](https://github.com/eldipa/xoz/wiki/RFC:-Specification-of-Segment,-Extent-and-Block-Size):
they calculate the size of an `Extent` and a `Segment` and how they are read from
and written to a C++ file.

### Block Array - `xoz/blk/` folder

A `BlockArray` is an abstraction that defines a space divided into
fixed-size blocks. Like any array, this can grow and shrink as needed
as well implements a way to read and write blocks (aka `Extent`).

Subclasses of `BlockArray` handle the implementation details:

 - `VectorBlockArray` (`vector_block_array.cpp`) is the simplest and
   easier to understand how a block array works.
 - `SegmentBlockArray` (`segment_block_array.cpp`) is the most complex
   as it implements a block array in terms of a `io` stream (it is
   a code to read once a `BlockArray` and  `IOSegment` are well understood).
 - `Repository` (in the `xoz/repo/` folder) uses a file as a backed;
   more details below.

### Repository - `xoz/repo/` folder

The `Repository` (in `repository.h`) is an abstraction of the underlying
file which may be a disk-based file or an in-memory file.

`openclose.cpp` deals how to create, open and close a repository
and how to handle disk and in-memory based files.

`fpresize.cpp` knows how to shrink and grow a file in a cross-platform
way.

`hdrtrailer.cpp` is an auxiliary file to read and write the XOZ header
and trailer structures.

### SegmentAllocator - `xoz/alloc/` folder

While a `BlockArray` defines the space in terms of *blocks*, the `SegmentAllocator`
is in charge to fulfill requests from the caller in term of *bytes*
searching for *free blocks* and returning a `Segment`.

The allocation is assisted by

 - `FreeMap` (`free_map.*`): tracks full blocks `Extent` that are
     free.
 - `SubBlockFreeMap` (`subblock_free_map.*`): tracks suballocate blocks that are
     free.
 - `TailAllocator` (`taill_allocator.*`): manages the grow and shrink of
     the underlying `BlockArray`.

These two maps handle all the allocation/deallocation of the
blocks/subblocks including coalescing, best/first allocation strategy,
and fragmentation handling of the free space.

The goal of the free maps is to allocate an `Extent` reducing the
external and internal fragmentation (which bloats the repository size).

The `SegmentAllocator` then fulfills a request getting one or more
`Extent` from the free maps and assembling them into a single `Segment`.

If no free space is found in the maps, the allocator delegates to
a low-level allocator, the `TailAllocator`, and gets more space. This
is then obtained from a `Repository` growing and making the file larger.

IMO, `xoz/alloc/` is the most juicy source code to learn about blocks
allocators (and to some extent about memory allocators) but also one
of the most complex pieces of code in XOZ.

### IO - `xoz/io/` folder

`IOSegment` (`iosegment.*`) is a byte-stream contiguous view of a `Segment` than handles
all the details for reading/writing bytes.

`IOBase` (`iobase.*`) is the superclass that generalizes all the common
behaviours; `IOSpan` (`iospan.*`) is a in-memory implementation, mostly
for testing.

### Descriptors - `xoz/dsc/` folder

`Descriptor` (`descriptor.*`) is the representation of a data object:
subclasses are in charge to give meaningful interpretation of the bytes
stored in XOZ. `DefaultDescriptor` (`default_descriptor.*`) is the
simplest of these subclasses (and the only one).

A group of descriptors is a `DescriptorSet` (`descriptor_set.*`). A set
does all the management for adding and removing descriptors relaying on
`SegmentBlockArray` and `SegmentAllocator` to do all the allocations.

Those two deal with all the encoding and low level details of the
[RFC Specification of Descriptor and Descriptor Set](https://github.com/eldipa/xoz/wiki/RFC:-Specification-of-Descriptor-and-Descriptor-Set.md)

## Git repository overview

- `xoz`: root folder for the XOZ lib source code
- `test`: all the unit tests live here
- `build-*`: where the build-specific configure and settings lives and
    where the binaries are stored

### Others

- `dataset-xopp`: a bunch of scripts for simulation and experimentation.
- `demos`: simple and dirty demos of parts of XOZ
- `docs`: a work-in-progress documentation
- `googletest`: submodule of `googletest`
