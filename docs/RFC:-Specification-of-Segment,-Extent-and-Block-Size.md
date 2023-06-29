# RFC: Specification of Segment, Extent and Block Size

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 3

### Summary of changes

Changed in version 3:

 - the array of extents is named a *segment*
 - bit `more` removed: the count of extents in the segment must be
   found somewhere else (not specified in this RFC)
 - bit `near` added: if 0, the `blk_nr` is built as in version 2; if 1,
   the `blk_nr` is built as an offset with respect the previous extent in the
   segment.
 - `hi_blk_nr` renamed to `jmp_blk_nr`
 - point to block 0 is reserved.
 - removed *unallocated extents*

Changed in version 2:

 - reduced the maximum size of inline data from 512 bytes to 64 bytes.
 - the inline data no longer must be a multiple
of 2 and this saves 1 byte of metadata when the data size is an
odd number.

# Abstract

The `.xoz` file is divided in blocks of a fixed size. In these blocks
the data of *strokes*, *images* and other *objects* is stored.

An *extent* is a sequence of contiguous blocks belonging to the same object.

A *segment* is a sequence of *extents* belonging to the same object.

An *object descriptor* then points to one or more blocks which may
or may not be contiguous indexed by one or more *extents*.

This RFC specifies the `struct extent_t`, how blocks are
indexed by an *extent*, how they assemble a *segment*
and the possible sizes that the blocks and *extent*
should/may have.

# Extents

An *extent* defines a contiguous range of blocks:

```cpp
struct extent_t {
    uint16_t {
        uint suballoc    : 1;    // mask 0x8000
        uint smallcnt    : 4:    // mask 0x7800
        uint near        : 1;    // mask 0x0400
        uint jmp_blk_nr  : 10;   // mask 0x03ff
    };

    /* present if:
        - not (suballoc == 1 && inline == 1)
        - and near == 0
    */
    uint16_t {
        uint lo_blk_nr  : 16;   // mask 0xffff
    }

    /* present if smallcnt == 0 */
    uint16_t blk_cnt;
};
```

`suballoc` and `smallcnt` control how the rest of the fields
may be interpreted:

 - `suballoc = 0` and `smallcnt = 0`: `uint16_t blk_cnt` **is** present;
the *extent* has `blk_cnt` blocks and `blk_nr` is the number of the first.
The structure's size is 6 bytes.

 - `suballoc = 0` and `smallcnt > 0`: `uint16_t blk_cnt` is **not** present;
the *extent* has `smallcnt` blocks and `blk_nr` is the number of the first.
The structure's size is 4 bytes.

 - `suballoc = 1` and `smallcnt = 0`: `uint16_t blk_cnt` **is** present;
the *extent* has 1 block only pointed by `blk_nr`. This block is
subdivided into 16 sub-blocks: `blk_cnt` acts as a *bitmap* that
indicates which sub-blocks are allocated.
The structure's size is 6 bytes.

The case with `suballoc = 1` and `smallcnt > 0` is special:
it turns the `struct extent_t` into an *inline data buffer*
and the extent does **not** point to any block.

In all of other cases, the extent points either to a single block
or to the first block of the extent.

`blk_nr` is this 26 bits block pointer constructed from `jmp_blk_nr`
and, if `near = 0`, from `lo_blk_nr` as explained below.

## Block number

If `near = 0`, the `lo_blk_nr` **is** present and the `blk_nr` is formed
as:

 - `jmp_blk_nr` (unsigned) are the 10 MSB of `blk_nr`
 - `lo_blk_nr` (unsigned)  are the 16 LSB of `blk_nr`

If `near = 1`, the `lo_blk_nr` is **not** present and `jmp_blk_nr`
is interpreted as *offset with direction* respect the previous extent
in the *segment*:

 - the MSB of `jmp_blk_nr` is the direction `jmp_dir`: `0` means *forward*,
   `1` means *backward*.
 - the 9 remaining bits are the unsigned offset `jmp_offset`.

Let `prev` and `cur` be the previous and current extents
and let `len` be the length
in blocks of the given extent (length of 1 if the extent is
suballoc'd).

Then `cur.blk_nr` is defined as:

 - if `cur.jmp_dir` is `0`, the `cur.blk_nr` is
   `prev.blk_nr + prev.len + cur.jmp_offset`

   In other words, `jmp_offset` counts *forward* how many blocks
   *after the end* of the previous `prev` extent
   the current `cur` extent *begins* (and `cur.len` blocks follows).

   `jmp_dir = 0 and jmp_offset = 0` means the current extent immediately follows
   the previous. (bits: `0 000000000`)

 - if `cur.jmp_dir` is `1`, the `cur.blk_nr` is
   `prev.blk_nr - cur.jmp_offset - cur.len`

   In other words, `jmp_offset` counts *backward* how many blocks
   *from the begin* of the previous `prev` extent the current `cur`
   extents *ends* (and `-cur.len` marks the begin).

   `jmp_dir = 1 and jmp_offset = 0` means the current extent immediately precedes
   the previous. (bits: `1 000000000`)

In either case computing `cur.blk_nr` must not wraparound, neither overflow
nor underflow. If such happen it should be considered a corruption or
a bad state.

If the current extent is the first in the segment, `prev.blk_nr` and
`prev.len` should be assumed to be zero.

The `blk_nr = 0` is **not** valid block numbers and it is reserved.
In other words, no extent can point to the block 0.

### Rationale - `near` bit

With 26 bits long, `blk_nr` requires at least 4 bytes.

But most of the extents are close each other to improve locality so it
is expected to find extents with very similar `blk_nr`.

Encoding a *relative* `blk_nr` for extents that are *near* each other
should require less bits.

Taking into a count the direction (forward / backward), the 9 LSB of
`jmp_blk_nr` allows to encode `blk_nr` within
the range of +/- 2^9 = 512 blocks using only 2 bytes.

The expected most common case is of an object which data size is
larger than a block size and not multiple of it.

In this scenario 2 extents (at least) will be allocated: the first to
store an integer number of blocks, the second to point to a single block
for suballocation.

Without the `near` bit, the second extent would require 6 bytes. But if
the block for suballocation is near, with `near = 1` it would require 4
bytes.


# Extent as Inline Data

The combination `suballoc = 1` and `smallcnt > 0` turns
the *extent* into a plain array of 2-bytes words
for *inline data*.

This applies **only** to the **last** `struct extent_t` of the
*segment*.

The `struct extent_t` can then be seen as:

```cpp
struct extent_t /* inline data mode */ {
    uint16_t {
        uint suballoc   : 1;    // mask 0x8000 (MUST BE SET TO 1)
        uint inline     : 1;    // mask 0x4000 (MUST BE SET TO 1)
        uint size       : 6;    // mask 0x3f00
        uint end        : 8;    // mask 0x00ff
    };

    uint8_t raw[/* size or size - 1 */];
};
```

The bits `suballoc` and `inline` **must** be set to 1 (`inline` set to 1
ensures that `smallcnt > 0`).

`size` is the count of bytes of user data inline'd which may be zero.

If `size` is an even number (including zero), `raw` has `size` bytes;
of `size` is an odd number, `raw` has `size - 1` bytes.

In both cases, the byte `end` contains the *last byte*
of the user data and `raw` the rest (with `raw[0]` being the first byte
of the user data).

If `size` is zero, `raw` is empty (zero bytes length) and
the value of `last` is zero.

These rules guarantees that the size of `struct extent_t` is
always a multiple of 2:

 - `2 + size` bytes if `size` is even
 - `2 + size - 1` bytes if `size` is odd

Note that the maximum *inline data* size may be capped by the maximum
size of the *object descriptor* that has this *extent* (if applies).

### Rationale - Inline Data

Small *strokes* and *texts* require little space and even
a single full block is too much.

Allowing to store the data within the *object descriptor*
should reduce the internal fragmentation.

Because *descriptors* mostly live in *streams* and these
are mostly append-only, it is not expected to be copying/moving
the descriptors frequently so storing a lot of data should
not introduce a significant performance problem.

The maximum size of the *inline data* is 64 bytes but it is expected
to keep much less and larger data should be stored in
a fully dedicated *data block* to keep the *object descriptor*
small.

<!--![stream_ext_frag](https://github.com/eldipa/xoz/assets/2665522/e3601aa5-6011-4bba-bfc1-b58adef18541)-->

# Invariants of the `struct extent_t`

The `struct extent_t` has some invariants:

 - it has always a size multiple of 2:
    - if it is not *inline data* then its size can be 2, 4 or 6 bytes
    - otherwise its size is 2, 4 or any multiple of 2.
 - the first 2 bytes (`uint16_t`) are always enough to decode the rest
   of the structure.
 - `blk_nr` are 26 bits long.

# Sub-Block Allocation

A block may be marked as for sub-allocation: it is split into 16
parts (or sub-blocks) that can be allocated for different *objects*.

An `struct extent_t` may have `suballoc = 1` and `smallcnt = 0`
to indicate that its `blk_nr` points to a block for sub-allocation.

The `uint16_t blk_cnt` acts as a *bitmap* that
indicates which sub-blocks are allocated for the *extent*.

### Rationale - Sub-Block Allocation

The *objects* are not expected to have a size multiple of
the block size so the last allocated block will certainly
have unused space ( *internal fragmentation* ).

There will be *half block size* per object on average.

Using small block sizes (~256 bytes) reduce the
*internal fragmentation* but require larger
extents to index large data like *images* and *fonts* (~1mb).

To keep using large block sizes (~1kb), the last part
of the data should be stored **not** in a
*fully dedicated block* but instead in a **shared** one.

Hence the idea of divide a block into 16 sub-blocks
that can be allocated independently.

![subblk_share](https://github.com/eldipa/xoz/assets/2665522/a149d909-b5e7-422f-b50d-2f6dade66a7c)

The internal fragmentation will *half of sub-block size*
per object on average.

The shared/divided block may still have more unused space
in the form of unallocated sub-blocks ( *external fragmentation* )

![subblk_frag](https://github.com/eldipa/xoz/assets/2665522/74c2b137-b5a9-4cf9-a590-fcb9d7292688)

With more objects stored in a `.xoz`,
the author expects the *external fragmentation*
due *sub-block allocation* is much lesser
that the *internal fragmentation* when only full blocks
are used.

But the trade off between *internal* and *external fragmentation*
exists.

# Segment

A *segment* is an ordered sequence of *extents* belonging to the same
object.

```cpp
struct segment_t {
    struct extent_t exts[/* specified somewhere else */];
}
```

The count of *extents* is specified somewhere else and it can be zero
( *empty segment* ).

If for some reason the *segment* cannot be empty but
there is not block allocated, then the *segment* should
have a single *inline data* with `size` and `end` set
to 0.

This effectively makes the *segment* "no" empty.

If the length of the *segment* cannot be specified somewhere else,
an *inline data* with `size` and `end` set to 0 can be used
as end-of-segment marker as an inline is always the last element
of a *segment*.

# Proposed block size and inline data limit

The author proposes a *block size* of 512 or 1024 bytes with
*sub-block allocation* enabled.

With 512 bytes blocks:

 - an single *extent* can then describe *objects* of size 8KB
   (using `smallcnt` - 4 bits) and 32MB (using `blk_cnt` - 16 bits).
 - a single *sub-block* (1/16 th of a block) can store up to 32 bytes.
 - the `.xoz` has a theoretical limit of 2^26 = 64M blocks, *32GB in total*.

With 1024 bytes blocks:

 - an single *extent* can then describe *objects* of size 16KB
   (using `smallcnt` - 4 bits) and 64MB (using `blk_cnt` - 16 bits).
 - a single *sub-block* (1/16 th of a block) can store up to 64 bytes.
 - the `.xoz` has a theoretical limit of 2^26 = 64M blocks, *64GB in total*.

It is expected that most of the objects' data can be stored in two
*extents*: one for storing a integer number of blocks, the other storing
the fractional part in an extent for suballocation.

The (soft) limit for the *inline data* is between 32 and 64 bytes.
Probably a limit larger than the 1/16 th of a block is not worth.
The author does not have a strong opinion on this.

The author proposes these numbers ( *block size* and
*inline data limit* ) based on some simulation and statistics obtained
from real `.xopp` files.

The simulation shows that the overhead ( *descriptors* ), unused space
( *internal* and *external fragmentation* ) is between 10% and 15%
and objects inlined are between 1% and 5%.

Note: The simulation was made for the version 1 of this RFC where the limit
size of the *inline data* was 512 bytes. Version 2 reduced this to 64
bytes.

![file_overhead_tradeoffs](https://github.com/eldipa/xoz/assets/2665522/fb8695df-523f-4d64-ae03-cfecf87b79f4)

The figure shows on the x-axis the ratio between the estimated `.xoz` file
and the raw-uncompressed user data (extracted from a `.xopp` file).

Values closer to 1 means zero overhead.

The figure does **not** compare `.xoz` with `.xopp` files.

Different block sizes (`blk_sz`) and
different inline data limit (with 0 meaning that no-inline was made)
(`max_inline_allowed`) where tried.

In all the cases, the sub-block allocation was enabled.

![obj_inlined](https://github.com/eldipa/xoz/assets/2665522/d4ef7bf4-a43b-48aa-b60a-7ade8de5b37a)

The figure shows on the x-axis the ratio between the inlined objects
and total count of objects.

For the simulation, objects smaller than the `max_inline_allowed` were
fully inlined, while objects larger were not inlined.

The figure shows that only a small fraction of objects can be inlined
for *inline data limit* of 32 and 64 bytes. Larger limits allow
more objects to be inlined but the first figure shows that this
generates more fragmentation and larger `.xoz` files in general.
<!--
![blk_sz_frag](https://github.com/eldipa/xoz/assets/2665522/c419ec5a-8a8a-46ad-ae1b-6ec8913b331f)
-->

# Copyright/License

This document is placed in the public domain or under the
CC0-1.0-Universal license, whichever is more permissive.
