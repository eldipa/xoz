## Compile and test

You need a GCC compiler with C++17 support. You should be able to
compile the code with other non-GCC compiler but it was not tested.

You will need also [tup](https://gittup.org/tup/index.html) and `make`
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

## How to read the source code? How XOZ is organized?

### Extent and Segments -  `xoz/ext/` folder

`extent.*` and `segment.*` define the two building-block
structures in XOZ, `Extent` and `Segment`. These are very simple,
The complexity lives in `calcsz.*` and `rw_segm.*`.

Those two deal with all the encoding and low level details of the
[RFC](https://github.com/eldipa/xoz/wiki/RFC:-Specification-of-Segment,-Extent-and-Block-Size):
they calculate the size of an `Extent` and a `Segment` and how they are read from
and written to a C++ file.

### Repository - `xoz/repo/` folder

The `Repository` (in `repo.h`) is an abstraction of the underlying
file which may be a disk-based file or an in-memory file.

`openclose.cpp` deals how to create, open and close a repository
and how to handle disk and in-memory based files.

`fpresize.cpp` knows how to shrink and grow a file in a cross-platform
way.

`rw_extent.cpp` knows how to read and write an `Extent` of either *full
blocks* or *suballocated* blocks.

`hdrtrailer.cpp` is an auxiliary file to read and write the XOZ header
and trailer structures.

### SegmentAllocator - `xoz/alloc/` folder

The `SegmentAllocator` is in charge to fulfill space requests from the
caller. It does a partition and issues a request of free blocks to the
`FreeMap` and a request of free sub-blocks to the `SubBlockFreeMap`.

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
allocators (and to some extent about memory allocators).

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
