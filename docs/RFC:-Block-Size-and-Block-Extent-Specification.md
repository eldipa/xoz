# RFC: Block Size and Block Extent Specification

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 1

### Summary of changes

None. No changes respect to the previous draft version.

# Abstract

The `.xoz` file is divided in blocks of a fixed size. In these blocks
the data of *strokes*, *images* and other *objects* is stored.

An *object descriptor* then points to one or more blocks which may
or may not be contiguous indexed by one or more *extents*.

This RFC specifies the `struct extent_t`, how blocks are
indexed by an *extent*, how the data is stored in the blocks
and the possible size that the blocks should have.

# Extents

An *extent* defines a contiguous range of blocks:

```cpp
struct extent_t {
    uint16_t {
        uint suballoc   : 1;    // mask 0x8000
        uint smallcnt   : 4:    // mask 0x7800
        uint more       : 1;    // mask 0x0400
        uint hi_blk_nr  : 10;   // mask 0x03ff
    };

    /* present if !(suballoc == 1 && inline == 1) */
    uint16_t {
        uint lo_blk_nr  : 16;   // mask 0xffff
    }

    /* present if smallcnt == 0 */
    uint16_t blk_cnt;
};
```

The block number `blk_nr` is composed by two parts: the high bits
`hi_blk_nr` and the low bits `lo_blk_nr`.

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

In all the above cases, `more` indicates if another
`struct extent_t` follows the current.

The last case with `suballoc = 1` and `smallcnt > 0` is special:
it turns the `struct extent_t` into an *inline data buffer*
and no `more` flag exists in that case.

# Extent as Inline Data

The combination `suballoc = 1` and `smallcnt > 0` turns
the *extent* into a plain array of 2-bytes words
for *inline data*.

This applies **only** to the **last** `struct extent_t` of the array
as the `more` flag is **not** defined in this mode.

The `struct extent_t` can then be seen as:

```cpp
struct extent_t /* inline data mode */ {
    uint16_t {
        uint suballoc   : 1;    // mask 0x8000 (MUST BE SET TO 1)
        uint inline     : 1;    // mask 0x4000 (MUST BE SET TO 1)
        uint reserved   : 6;    // mask 0x3f00
        uint size       : 8;    // mask 0x00ff
    };

    uint8_t raw[/* size * 2 */];
};
```

The bits `suballoc` and `inline` **must** be set to 1 (`inline` set to 1
ensures that `smallcnt > 0`).

Then `raw` is the 2-bytes words array of the given `size`.

Note that the maximum *inline data* size may be capped by the maximum
size of the *object descriptor* that has this *extent* (if applies).

The `struct extent_t` still has a size multiple of 2: `2 + (2 * size)`
bytes

### Rationale - Inline Data

Small *strokes* and *texts* require little space and even
a single full block is too much.

Allowing to store the data within the *object descriptor*
should reduce the internal fragmentation.

Because *descriptors* mostly live in *streams* and these
are mostly append-only, it is not expected to be copying/moving
the descriptors frequently so storing a lot of data should
not introduce a significant performance problem.

The maximum size of the *inline data* is 512 bytes but it is expected
to keep much less and larger data should be stored in
a fully dedicated *data block* to keep the *object descriptor*
small.

While *inline data* reduces the internal fragmentation avoiding
using a *block* or *subblocks*, it may increases the external
fragmentation.

This is because the *object descriptor* will increase in size,
the *descriptors* in a *stream* must be stored such they don't
span more than one *block* of the *stream*.

![stream_ext_frag](https://github.com/eldipa/xoz/assets/2665522/e3601aa5-6011-4bba-bfc1-b58adef18541)

# Unallocated Extents

An *extent* may be marked as *unallocated*. This can be achieved
setting the `blk_nr` to zero in-place without modifying the
`struct` layout.

In this way any `struct extent_t` in the *extent array* can be
unallocated without removing it from the array.

An *inline data* can be *unallocated* too:

 - the `raw` is fulfilled with *unallocated extents* (4 or 6 bytes)
   and padded with a single *empty extent array* (2 bytes) at the end.
 - the bits `inline` is set to 0.

The library might reuse the space left by *unallocated extents* to
allocate new *extents* without requiring a change in the layout.

# Empty Extent Array

If an *object* does not require any *data blocks* or *inline data*
may encode an *empty extent array* by setting the **first**
`struct extent_t` as:

 - `suballoc` and `inline` set to 1
 - `size` set to 0

In this way the `struct extent_t` looks like an *inline data*
of zero bytes, effectively, the *extent array* only occupies 2 bytes.

# Invariants of the `struct extent_t`

The `struct extent_t` has two invariants:

 - it has always a size multiple of 2.
 - the first 2 bytes (`uint16_t`) are always enough to decode the rest
   of the structure and if more `struct extent_t` follows.
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

The (soft) limit for the *inline data* is between 32 and 64 bytes.
Probably a limit larger than the 1/16 th of a block is not worth.
The author does not have a strong opinion on this.

The author proposes these numbers ( *block size* and
*inline data limit* ) based on some simulation and statistics obtained
from real `.xopp` files.

Simulations show that the overhead ( *descriptors* ), unused space
( *internal* and *external fragmentation* ) is between 10% and 15%
and objects inlined are between 1% and 5%.

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
