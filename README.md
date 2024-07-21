## Compile and test

You need a `gcc` and/or `clang` compiler with C++20 support. You should be able to
compile the code with other compilers but it was not tested.

You will need also `cmake` and `make`
for compiling; `valgrind` for run some tests and `pre-commit` to run
some checkers and linters.

There are three flavors of compiled code defined by `cmake`:
- `debug`: no optimized code, asserts enabled
- `release`: optimized for speed, asserts disabled, no debug info
- `relwithdebinfo`: like `release` but with debug info

The following targets for `make` are allowed:
- `make compile-<flavour>`: compile the given flavour with the default
  compiler for the current enviroment (typically `gcc` for Linux,
  `clang` for MacOS)
- `make test-<flavour>`: compile and run the tests
- `make valgrind-<flavour>`: compile and run the tests with `valgrind`

If `make` is not available, you can run the `cmake` commands by hand.
Check the content of `Makefile` file.

To run the checkers and linters run `pre-commit run --all-files`. This will call:
 - `cppcheck`: static verifier
 - `cpplint`: linter
 - `clang-format`: source code formatter

Run to run the linter `clang-tidy` run `pre-commit run --all-files --hook-stage manual`.
Note: currently `clang-tidy` is giving too minor or non-existing issues,
read it with caution.

### Example

Install compilers and toolchain (Debian version)

```
$ sudo apt-get install build-essential clang valgrind cmake
```
Download and compile XOZ

```
$ cd ~/
$ git clone https://github.com/eldipa/xoz.git

$ cd xoz/
$ make test-debug  # for testing/debugging
$ make test-relwithdebinfo  # for production
```

If you have more than one compiler in your environment, you can force
the use of one over the other running:

```
$ make test-relwithdebinfo  EXTRA_GENERATE=-DCMAKE_CXX_COMPILER=gcc
$ make test-relwithdebinfo  EXTRA_GENERATE=-DCMAKE_CXX_COMPILER=clang
```

Keep in mind that if you do that, `cmake` will keep using *that*
compiler in subsequent calls. You may want to delete the file
`build-<flavour>/CMakeCache.txt` to allow `cmake` to pick the default
compiler again.

## How to use `xoz`? Demo time!

After compiling the project (like doing `make test-debug`), in the
`build-<flavour>` folder it should be a program called `tarfile`.

`tarfile` is an achiver, similar to `tar` or `zip` that stores files.
Run it without arguments to see what you can do with it:

```shell
$ make test-debug
$ ./build-debug/tarlike
Missing/Bad arguments
Usage:
  add files:      tarlike <file.xoz> a <file name> [<file name>...]
  delete files:   tarlike <file.xoz> d <file id> [<file id>...]
  extract files:  tarlike <file.xoz> x <file id> [<file id>...]
  rename a file:  tarlike <file.xoz> r <file id> <new file name>
  list files:     tarlike <file.xoz> l
  show stats:     tarlike <file.xoz> s
```

Of course, `tarlike` is implementing using `xoz`. Its source code
(`demos/tarlike.cpp`) is fully documented and it is a complete example
of how to use `xoz`.

## How to use it in your project with cmake?

```
include(FetchContent)
FetchContent_Declare(
    xoz
    GIT_REPOSITORY https://github.com/eldipa/xoz.git
    GIT_TAG        <hash here>
)

set(XOZ_TESTS OFF)
set(XOZ_DEMOS OFF)
set(XOZ_MAKE_WARNINGS_AS_ERRORS OFF)
FetchContent_MakeAvailable(xoz)

add_dependencies(<your target> xoz)

target_include_directories(<your target>
    PUBLIC
    ${xoz_SOURCE_DIR}
    )

target_link_libraries(<your target> xoz)
```

## How to read the source code? How XOZ is organized?

### Extent and Segments -  `xoz/ext/` and `xoz/segm/` folders

`extent.*` and `segment.*` define the two building-block
structures in XOZ, `Extent` and `Segment`.

An `Extent` is a range of consecutive blocks; a `Segment` is a list
of `Extent`. The details are in
[RFC Specification of Segment, Extent and Block Size](https://github.com/eldipa/xoz/wiki/RFC:-Specification-of-Segment,-Extent-and-Block-Size)

### Block Array - `xoz/blk/` folder

A `BlockArray` is an abstraction that defines a space divided into
fixed-size blocks. Like any array, this can grow and shrink as needed
as well implements a way to read and write blocks (aka `Extent`).

Subclasses of `BlockArray` handle the implementation details:

 - `VectorBlockArray` (`vector_block_array.cpp`) is the simplest and
   easier to understand how a block array works, fully in-memory (mostly
   for testing).
 - `SegmentBlockArray` (`segment_block_array.cpp`) is the most complex
   as it implements a block array in terms of a `io` stream (it is
   a code to read once a `BlockArray` and  `IOSegment` are well understood).
 - `FileBlockArray` (`file_block_array.cpp`) uses a file as a backed;
   supports an additional allocation of a small space at the end of the
   block array and has two implementation modes: in memory and in disk.

### SegmentAllocator - `xoz/alloc/` folder

While a `BlockArray` defines the space in terms of *blocks*, the `SegmentAllocator`
is in charge to fulfill requests from the caller in term of *bytes*
searching for *free blocks* and returning a `Segment`.

The allocation is assisted by

 - `FreeMap` (`free_map.cpp`): tracks full blocks `Extent` that are
     free.
 - `SubBlockFreeMap` (`subblock_free_map.cpp`): tracks suballocate blocks that are
     free.
 - `TailAllocator` (`tail_allocator.cpp`): manages the grow and shrink of
     the underlying `BlockArray`.

These two maps handle all the allocation/deallocation of the
blocks/subblocks including coalescing, best/first allocation strategy,
and fragmentation handling of the free space.

The goal of the free maps is to allocate an `Extent` reducing the
external and internal fragmentation (which bloats the xoz file size).

The `SegmentAllocator` then fulfills a request getting one or more
`Extent` from the free maps and assembling them into a single `Segment`.

If no free space is found in the maps, the allocator delegates to
a low-level allocator, the `TailAllocator`, and gets more space. This
is then obtained from a `BlockArray` growing and making the file larger.

IMO, `xoz/alloc/` is the most juicy source code to learn about blocks
allocators (and to some extent about memory allocators) but also one
of the most complex pieces of code in XOZ.

### IO - `xoz/io/` folder

`IOSegment` (`iosegment.cpp`) is a byte-stream contiguous view of a `Segment` than handles
all the details for reading/writing bytes.

`IOBase` (`iobase.cpp`) is the superclass that generalizes all the common
behaviours; `IOSpan` (`iospan.cpp`) is a in-memory implementation, mostly
for testing.

### Descriptors and Sets - `xoz/dsc/` folder

`Descriptor` (`descriptor.cpp`) is the representation of a data object:
subclasses are in charge to give meaningful interpretation of the bytes
stored in XOZ.

XOZ defines 2 subclasses:
 - `DefaultDescriptor` (`default_descriptor.cpp`) is the
 simplest and it is used as a generic holder (fallback).
 - `DescriptorSet` (`descriptor_set.cpp`) is a group of `Descriptor`;
 it does all the management for adding and removing descriptors and it
 works as the first batch level.

See the full details in [RFC Specification of Descriptor and Descriptor Set](https://github.com/eldipa/xoz/wiki/RFC:-Specification-of-Descriptor-and-Descriptor-Set.md)

### File - `xoz/file/` folder

`File` (`file.cpp`) is an abstraction of the underlying
xoz file.

A `File` contains a single `root` descriptor set `DescriptorSet`
from where zero o more `Descriptor` lives, including more descriptor
sets. The `File` forms a tree-like structure of descriptors and
sets.


### Misc - `xoz/mem/`, `xox/log`, `xoz/err` folders

Some extra code:

 - `xoz/mem` folder: helper functions to deal with endianness, casting
   and checksum.
 - `xoz/log` folder: basic module for logging and tracing.
 - `xoz/err` folder: home of XOZ specific exception classess.

## Git repository overview

- `xoz`: root folder for the XOZ lib source code
- `test`: all the unit tests live here
- `build-*`: where the build-specific configure and settings lives and
    where the binaries are stored
- `dataset-xopp`: a bunch of scripts for simulation and experimentation.
- `demos`: simple and dirty demos of parts of XOZ
- `docs`: a work-in-progress documentation
