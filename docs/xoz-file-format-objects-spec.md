# XOZ File Format - Objects Specification

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 1

### Summary of changes

None. No changes respect to the previous draft version.

# Abstract

A new binary format `.xoz` is proposed to support Xournal++ specific
needs:

 - Random read and write access to data associated with a document's page
including strokes, images and other embedded files without requiring
reading or writing the entire file.
 - More efficient data representation.
 - Embed arbitrary files, including but not limited to, PDF, images, audio
and fonts.

This RFC describes the *objects specification* for the `.xoz` file format,
which modifications to
Xournal++ source code are required to integrate it
and which may be delayed for a future version.

It describes the *what* and the *why* but it leaves out (most of) the *how*
and *when*.

# Motivation

Xournal++ has `.xopp` as its unique
file format: a text-based XML file compressed with GZip.

The XML format does not support neither random access for reading nor modifications
in-place which forces to read and write the entire file on each access.

And due its text-based nature, it requires an explicit encoding to
store binary data making large files.

`.xopp` is compressed with GZip:
While it reduces the file size, it trades space by CPU time.

Therefore Xournal++ requires to:

 - uncompress and read the entire file to load a single page.
 - compress and write the entire file even if only a single page is changed.

Users had reported in the past that `.xopp` files were *"too large"*
or it takes *"too much time"* to load them.

While read performance can be amortized in time (we don't expect a
typical user to open several files every minute), write performance is
more critical.

Xournal++ writes a backup file every 5 minutes or so for crash
recovery so even if the user does not modify the document, he/she will
pay the cost.

This translates to *disk degradation* (specially for "cheap" SSDs found in
phones / tables) and *battery drain*.

On top of those limitations, there is interest in adding even more
data into a `.xopp` like PDF and fonts, making Xournal++ files *"self-contained"*
but also larger due the impossibility to write binary.


# Overview

Xournal++ has the concept of *strokes*, *text* and *images*. These are
specified by this RFC as *objects*.

*objects* are grouped together in a ordered sequence called *stream*.

A *page* does not exist as a single entity in `.xoz`. Instead, the
parts of a *page* are represented in different places called *tiles*.

A *tile* has a *stream of objects* associated with and represents
a part of a *page*.

Each *tile* belongs to a single *layer* and it has an unique location
inside.

A *layer* is defined as a *stream of tiles* organized in
a 2 dimensional matrix or *mosaic* such each *tile* occupies a
single position but not necessary every location is occupied.

The *stack of layers* composes the whole *document*, a for each
particular location, the *stack of tiles*
composes a single *page* for that location.

![image](https://github.com/eldipa/xoz/assets/2665522/7fee7e8c-220d-48d9-8b3c-4aa5eb5d066b)

`.xoz` does not define a structure neither for a *page* nor a
*stack of tiles*. These are convenient abstractions for the purpose of
documentation and enables a parallelism with the `.xopp` concepts of
page and page's layer.


## Object Descriptors, Continuations and Data Blocks

Strokes, images, texts as well embedded files and others
are *objects*.

An *object* has three parts:

 - a *object descriptor*
 - zero or more *continuations*
 - zero or more *data blocks*

![image](https://github.com/eldipa/xoz/assets/2665522/18dbb904-0589-4d9f-802e-4ef2512293eb)

The *object descriptor* is the only required piece of information that
describes the object.

All the *object descriptors* belongs to one and only one *stream*:
the *object* is owned by that *stream*.

There are cases where a *descriptor* is not enough:

 - more space is required
 - optional attributes are set
 - tags and attachments are linked

For these cases the *descriptor* can be
continued or extended with a *continuation*.

*Continuations* always live in the same *stream*
where its *object descriptor* lives.

One exception: for performance reasons the *continuations of tiles* live
inside the *stream of layers*, outside of the *stream* where
the *tile descriptors* live (out of band).

An *object* may require much more space that its *descriptor* or a
*continuation* can offer: This is stored in *data blocks*.

### Rationale - Descriptors

The reason behind having separated the *descriptor* from the *object*'s
main body is because we can store there attributes
that are likely to be changed in the *descriptor* and leave the rest
outside.

To mention a few examples:

 - strokes' and texts' color
 - strokes', texts' and images' position (x and y)

A attribute modification can the be **in-place** inside the
*descriptor*.

Grouping all the related *descriptors* in a single *stream* (like all
the *objects* of a *page*) makes possible to update several of them
**in-place** with only the *stream* loaded in memory.

If these attributes where in the *objects*' *data blocks*, each
attribute modification would require an additional
lookup per *object* changed (from the *descriptor* to the *data block*).

### Rationale - Continuations

An *object* may have optional attributes:

 - storing them in the *descriptor* would require reserve space for
   these optional attributes for **all** the *objects*, not matter
   if they are used or not.
 - storing them in the *data blocks* would require an additional
   lookup to read/write them.

A *continuation* is an optional extension of the *object descriptor*:

 - only the *objects* that really need these optional attributes
   will have a concrete *continuation* allocated.
 - being in the same *stream* that its *descriptor*, a *continuation*
   can be read/written/updated **in-place** and without an
   additional lookup.

A *continuation* also enables to add more *data blocks* to an *object*
(`struct cont_more_extents_t`) and attach or link *objects*
(`struct cont_audio_attach_t`).

Future `.xoz` versions may expand *objects* in different ways
without breaking their structure layout (maintaining compatibility,
in principle)

### Rationale - Out of Band Tile Continuations

All the *continuations* leave in the same *stream* where its
*descriptor* leaves except for *tiles*.

Because the *tile descriptors* has a fixed structure
(`struct tile_desc_t`) and a *stream of tiles* is entirely made
of these structures, it is possible to optimize the access
using efficient simple indexes.

This RFC does not mandate the use of indexes but such may be required
as fast arbitrary-random *tile* access is a critical
part to compose a specific *page*.

If *continuations* (or other *objects*) would live there, indexing
may be more complex.

These *continuations* then live in the *stream of layers*.

## Document, Layers and Tiles

*Layers* define a 2-dimensional *mosaic* where a *tile* can be set and
hold *objects*.

A *document* stacks one layer on top of each other: for a given
location, the stacked *tiles* in that location compose a *page*.

Xournal++ should render the *objects* inside each *tile* from the bottom
of the stack to the top.

The lowest *layer* (at the bottom of the stack) is the *background layer*
and its *tiles* are *background tiles* grouped in
a *stream of background tiles*.

This 2-dimensional mosaic defines how the tiles are located each other,
**not** how Xournal++ renders them in the UI.

Currently Xournal++ has a way to render multiple pages on the same
display, a *layout*. The concept of the *mosaic* is **not** related.

### Rationale - No Pages but First Citizen Layers

The `.xopp` file format represents each *page* as a series of layers.
This is a handy representation to quickly read all the components of a
*page* cross all the layers.

But this prevents to add/remove/modify a single layer, requiring
to modify **all** the pages of `.xopp`.

The `.xoz` file format drops the *page* concept and makes *layers* a
first citizen *object*.

The render a single *page*, the library will have to read the multiple
*tiles* in a particular location across all the *layers*.

While this may sound more expensive, most of the `.xopp` documents from
the community has very few *layers* so the cost should be minimal.

Having the *layers* separately and independent each of the other may
allow to implement operations between `.xoz` files like *merging*.

This RFC however does not go further.

### Rationale - Mosaic of Tiles

Xournal++ traditionally modeled a document as a ordered linear sequence
of pages.

If the user wanted to add a new page between, it was possible but always
preserving the same linear sequence.

If the user wanted to add more space on a side of a page or wanted pages
without limits (aka *infinite pages*), this was not possible.

The *layers* in `.xoz` model the document as a 2-dimensional *mosaic of tiles*.

A traditional document can still be modeled as a ordered linear
sequence: the *tiles* just need to be located at a fixed column (like
`col = 0`)

But no hard limit is imposed: if the user wanted to add more space on a
side of a page or wanted pages without limits, more
*tiles* can be added to the *layer* in the direction that the user wanted.

# Specification

All the present structures in this RFC are the *structures in-disk* that form the
`.xoz` file.

Unless noted, by default they are:

 - aligned to 1 byte
 - with no padding
 - with integer numbers in little endian unsigned

This RFC uses `uint8_t` and `uint16_t` to denote unsigned integers of 8
and 16 bits.

When bit specification is required the RFC uses pseudo-C notation with
the bit count on the right.

Such specification is always backed by native integer aligned to 1 byte.
and the first entry are the most significant bits (MSB).

```cpp
struct some_t {
    uint32_t {
        uint a  : 10;   // 10 most significant bits
        uint b  : 22;   // next 22 bits
    };

    uint16_t c;
};
```

The structure above has 3 fields:

 - `some_t.a` an unsigned integer stored in the 10 MSB of a 32 bits integer
 - `some_t.b` an unsigned integer stored in the next 22 bits of the same 32 bits integer
 - `some_t.c` an unsigned integer of 16 bits.

**Note:** a library implementing this RFC should not use C/C++ bit fields.
They are not portable. Instead bit masks and shifts should be used.

## Data Blocks

*Data blocks* are the basic unit of allocated space: everything
is stored in one or more *data blocks* of a power-of-2 fixed size.

Each *data block* allocated and in use is assigned to an *object*,
*stream* or other structure.

How their are allocated and in which structures metadata for them
exist is out of the scope for this RFC.

### Block Extents

An *extent* defines a contiguous range of blocks:

```cpp
struct extent_t {
    uint32_t blk_nr;
    uint16_t blk_cnt;
};
```

A single *extent* may not be enough, most likely because it cannot be
found a single contiguous range of blocks.

To handle these cases most of the *objects*, *streams* and similar have
an *array of extents*:

```cpp
struct ext_arr_t {
    uint8_t ext_cnt;
    struct extent_t exts[/* ext_cnt */];
}
```

The `ext_cnt` is *fixed* at the moment of the creation, so the size of
`struct ext_arr_t` is known.

It is possible to *pre-allocate* more blocks than the required, just
having a larger *extent* or an extra *extent* in the array. This enables room
for growing at the expense of wasting unused space.

Therefore the exact byte count/size/length of useful data is defined
elsewhere.

A `blk_nr` and `blk_cnt` of zeros means that that *extent* is inactive.
Because `ext_cnt` says how many entries the `exts` array has, this is the
only way to signal that an entry (an *extent*) should be ignored.

This RFC does not fully define the meaning of `blk_nr` or `blk_cnt`.
For example the library should not assume that all the 32 bits of `uint32_t blk_nr`
are fully allocated to encode the block number. A future RFC will provide
such definitions.

This RFC does not define a *sub-block allocation* schema for
reducing the internal fragmentation for data smaller
than the block size.


### Rationale - Extents and Data Blocks

Most of the *strokes* have around 100 coordinates but some
reach 1000.

With a conservative 4 bytes per coordinate, this gives between sizes
around 400 up to 4000 bytes.

*Texts* are around 10 and 100 bytes, *tex/latex images* are 10 kilobytes
and *images* are around 100 kilobytes and 1 MB.

This are educated guessed numbers based on
[statistics (discussions 4862)](https://github.com/xournalpp/xournalpp/discussions/4862)
calculated from some real-life `.xopp` files.

In essence, `.xoz` is like a tiny file system storing several tiny and
not so tiny files.

Indexing them by byte would be wasteful so the author proposes to index
by *block*.

Because most the data to be stored is expected to have a known fixed
size (like an image), the library can allocate a contiguous array of
blocks.

In this case, indexing them block by block is not necessary and a simple
block number plus length is enough: an *extent*.

The author took inspiration from the
[ext4 file system](https://en.wikipedia.org/wiki/Ext4)


## Objects

Any *object* or *continuation* share a common header:

```cpp
struct obj_hdr_t {
    // bit mask: <DCRR RSSS SSSS SSSS>
    uint16_t {
        uint dead     : 1;  // mask 0x8000
        uint cont     : 1;  // mask 0x4000
        uint reserved : 3;  // mask 0x3800
        uint size     : 11; // mask 0x07ff
    };

    uint32_t {
        uint type     : 10; // mask 0xffc00000
        uint id       : 22; // mask 0x003fffff
    }
};
```

The following bit field is backed by the first `uint16_t` integer:

 - `dead` says if the object is alive (0) or dead (1) (aka, zombie)
 - `cont` says if the object is an ordinary
   object (0) or if it is a *continuation* (1).
 - `reserved` are reserved.
 - `size` says the size of the *object descriptor* in words of 2 bytes.
   This imposes a limit of 2^11 = 2048 2-byte words = 4096 bytes.

This last item implies that any *object descriptor* or *continuation*
**must** have a size multiple of 2.

Then the next `uint32_t` integer has two parts:

 - `type` encodes the *object type* (~1024 types)
   `.xoz` supports different types of objects which are defined below.
 - `id` says which *object id* is assigned to this *object* (~4M ids)

The field `id` represents the 22-bits unique identifier of the *object* being
described by the *descriptor* or of the *object* that the *continuation*
is extending/attaching to.

However the interpretation of `id` may change for some special
*object types* and the `id` 0x000000 and 0x3fffff are reserved.

The generation and assignation of *object ids* is not part of this RFC.

### Object Types (Summary)

For convenience the *object types* are grouped and assigned
a subset of the 10 bits space.

Types of plain objects are in the range `0x001 - 0x00f`, continuations
are in the range `0x010 - 0x01f` and so on.

Plain objects:

 - 0x001: *stroke object*
 - 0x002: *text object*
 - 0x003: *image object*
 - 0x004: *Tex/Latex image object*
 - 0x005: *blob object*
 - 0x005: *solid background object*

Continuations:

 - 0x010: *more extents continuation*
 - 0x011: *stroke pattern continuation*
 - 0x012: *link*
 - 0x013: *tag*
 - 0x014: *stream continuation* (reserved)
 - 0x015: *tile continuation* (reserved)
 - 0x016: *layer continuation* (reserved)

Structural:

 - 0x000: *discard bytes object*.
 - 0x080: *document object*
 - 0x081: *tile object*
 - 0x082: *background tile object*
 - 0x083: *layer object*
 - 0x3ff: *end of stream object*


**Note:** The value assigned to each type is mandatory, the ranges are not.
For example, the library must not assume that `0x010 - 0x01f` is the exclusive
range of *continuation* types: *continuation types* may exist outside
the range and *non-continuation types* may exist inside the range.


### Discard Bytes Object

```cpp
struct obj_discard_bytes_desc_t {
    struct obj_hdr_t hdr;   // type: 0x000
};
```

The *discard bytes object* represents a *"hole"* or a *"gap"*.

The least 16 significant bits of `hdr.id` represents the
length of the gap: how many bytes should be skipped.

The rest of the bits of `hdr.id` are reserved and the flags
`dead` and `cont` are ignored (should be 0)

### Stroke Object

The stroke objects are defined as:

```cpp
struct obj_stroke_desc_t {
    struct obj_hdr_t hdr;   // type: 0x001

    uint8_t flags;
    struct ext_arr_t exts;

    uint32_t color_rgba;
};
```

`color_rgba` encodes the red-green-blue-alpha color.

`flags` are a reserved.

Data blocks pointed by the extents must be concatenated and
seen as a contiguous buffer from which the following structure
is read:

```cpp
struct obj_stroke_data_t {
    // bit mask: <FFFF FFFF CCCC PPPP>
    uint16_t {
        uint fill      : 8; // mask 0xff00
        uint cap_style : 4; // mask 0x00f0
        uint pattern   : 4; // mask 0x000f
    };

    uint32_t {
        uint tool      : 5;  // mask 0xf8000000
        uint enc       : 4;  // mask 0x07800000
        uint reserved  : 3;  // mask 0x00700000
        uint point_cnt : 20; // mask 0x000fffff
    };

    float coords[/* based on enc and point_cnt */];
};
```

The first `uint16_t` is interpreted as follows:

 - `fill` encodes the *fill transparency* of the stroke: it is
interpreted as an `uint8_t` linear scale being 1 a
fill nearly transparent and 255 a fill fully opaque.
The value 0 means no fill at all.

 - `cap_style` encodes the *cap-style* of the stroke:
currently `butt` (0), `round` (1) and `square` (2) are defined.
The bit pattern `1111` (7) is reserved.

 - `pattern` encodes the style or *pattern* of the stroke:
currently `solid` (0), `dot` (1), `dashdot` (2) and `dash` (3) are
defined. The bit pattern `1110` (14) means that it is a
*custom style/pattern* and it is defined in the *continuation*
`struct cont_stroke_pattern_t`. The bit pattern `1111` (15) is reserved.

Then, the next `uint32_t` is interpreted as follows:

 - `tool` encodes the *tool* used to draw the stroke:
currently `pen` (0), `highlighter` (1) and `eraser` (2) are defined.
The bit pattern `11111` (31) is reserved.

 - `enc` says how to interpret the coordinates in `coords`
(*point encoding*, see below)

 - `reserved` are reserved bits.

 - `point_cnt` encodes how many points form the *stroke*. How many
coordinates the `coords` has is a combination of the `point_cnt`
and the dimension set in `enc`.

Point encoding (`enc`):

 - 0x0: there are `2 * N + 1` coordinates for a *stroke* of `N` points.
The last coordinate is the *uniform width* of the *stroke*.
 - 0x1: there are `3 * N - 1` coordinates for a *stroke* of `N` points
for a non-uniform *width*/*pressure* *stroke*.

**TODO:** the *point encoding* is still under review.

### Text Object

The *text objects* are defined as:

```cpp
struct obj_text_desc_t {
    struct obj_hdr_t hdr;   // type: 0x002

    uint8_t flags;
    struct ext_arr_t exts;

    uint32_t color_rgba;

    float x;
    float y;

    uint32_t {
        uint font_size : 7;  // mask 0xfe000000
        uint reserved  : 3;  // mask 0x01c00000
        uint font_id   : 22; // mask 0x003fffff
    }
};
```

The 8 bits of `flags` and the 3 bits of `reserved` are reserved.

The `color_rgba` is the color of the text and `x` and `y` its position.

The `font_size` says the size of the text and `font_id` is the
*object id* of the *font object*.

Data blocks pointed by the extents must be concatenated and
seen as a contiguous null-terminated UTF-8 encoded text.

### Image Object

The *image objects* are defined as:

```cpp
struct obj_image_desc_t {
    struct obj_hdr_t;   // type: 0x003

    uint8_t flags;
    struct ext_arr_t exts;

    float x;
    float y;
};
```

Data blocks pointed by the extents must be concatenated and
seen as a contiguous buffer from which the following structure:

```cpp
struct obj_image_data_t {
    uint16_t flags;

    uint8_t format;
    uint32_t width;
    uint32_t height;

    uint32_t size;
    uint8_t raw[/* size */];
};
```

All the 16 bits of `flags` are reserved.

The possible compressed raw representation of the image is stored
in the `raw` array of `size` bytes.

The `format` encodes the image format and `width` and `height` the
image dimensions in pixels.

They may be zero however in which case the
library may try read the first bytes of `raw` to deduce the missing
information.

Supported image formats are:

 - `0x00`: undefined
 - `0x01`: PNG

### Tex/Latex Image Object

The *Tex/Latex image objects* (or just *teximage objects*) are defined as:

```cpp
struct obj_teximage_desc_t {
    struct obj_hdr_t;   // type: 0x004

    uint8_t flags;
    struct ext_arr_t exts;

    float x;
    float y;
};
```

Data blocks pointed by the extents must be concatenated and
seen as a contiguous buffer from which the following structure:

```cpp
struct obj_teximage_data_t {
    struct obj_image_data_t img;

    uint8_t texsrc[/* null terminated */];
};
```

All the 16 bits of `img.flags` are reserved.

The Tex/Latex source code from which the image was generated is stored
in `texsrc` as a null terminated UTF-8 encoded text.


### Blob Object

*blob objects* represent arbitrary data: this can be fonts,
audio or even ZIP files.

```cpp
struct obj_blob_desc_t {
    struct obj_hdr_t;   // type: 0x005

    uint8_t flags;
    struct ext_arr_t exts;

    uint16_t flags2;
    uint32_t blob_size;
};
```

Data blocks pointed by the extents must be concatenated and
seen as a contiguous buffer from which the binary blob of
`size` bytes can be read or written.

`flags` and `flags2` are reserved.

### Solid Background Object

```cpp
struct obj_solid_bg_desc_t {
    struct obj_hdr_t;   // type: 0x006
    uint32_t color_rgba;
    uint8_t style;

    // bit map
    uint32_t {
        uint fg_color_1_set     : 1;
        uint fg_alt_color_1_set : 1;
        uint fg_color_2_set     : 1;
        uint fg_alt_color_2_set : 1;

        uint line_width_set     : 1;
        uint margin_set         : 1;
        uint round_margin_set   : 1;
        uint raster_set         : 1;

        uint reserved           : 24;
    }
    uint8_t params[/* based on bits set */];
}
```

Each bit set of the bitmap adds 4 bytes to the `params` array.

These 4 bytes are interpreted as `uint32_t` numbers for the `fg_*_set`
bits and as `float` numbers for the rest of the `*_set` bits.


### End of Stream Object

This *object descriptor* marks the end of a *stream*.

```cpp
struct obj_end_of_stream_desc_t {
    struct obj_hdr_t hdr;   // type: 0x3ff
};
```

Except `hdr.type` and `hdr.size`, the rest of the content of `hdr`
is reserved.

## Object Continuations

An *object descriptor* may not be enough to encode all the information.

A *continuation* may be used to

 - add more room for more data.
 - attach additional objects or metadata.
 - specify optional parameters.

An *object continuation* is an structure that looks like an ordinary
object but the `id` of `struct obj_hdr_t` is reinterpreted
not as the `id` of the continuation but the `id` of the object
that this continuation is extending.

Like any other *descriptor*, a *continuation* must have a size
multiple of 2.

### More Extents

The *continuation* adds `ext_cnt` additional extents to the object. The
interpretation of the data blocks is up to the original object.

```cpp
struct cont_more_extents_t {
    struct obj_hdr_t;   // type: 0x010

    uint8_t flags;
    struct ext_arr_t exts;
};
```

If an *object descriptor* has a `struct ext_arr_t` with an `ext_cnt`
of 2, let's say `[A, B]`, if a following `struct cont_more_extents_t`
adds 3 additional extents `[C, D, E]`, then the *data blocks* of the object
are described by the extents `[A, B, C, D, E]`, in that order.

More than one `struct cont_more_extents_t` can be used to extend
the same *object*.


### Stroke Non-Standard Pattern

A *stroke* may be draw using a non-standard pattern. This pattern is
defined as a array of `valuecnt` values which interpretation is
based on `pattern_type` and it is up to the application.

```cpp
struct cont_stroke_pattern_t {
    struct obj_hdr_t hdr;   // type: 0x011

    uint8_t pattern_type;
    uint8_t valuecnt;
    float values[/* valuecnt */];
};
```

### Link

Any object can be *linked* to another. The purpose or reason
of the link depends on the `meaning` field.

```cpp
struct cont_link_t {
    struct obj_hdr_t hdr;   // type: 0x012

    uint32_t {
        uint meaning   : 5;  // mask 0xf8000000
        uint reserved  : 5;  // mask 0x07c00000
        uint obj_id    : 22; // mask 0x003fffff
    }
};
```

Defined `meaning` values:

 - 0x00: play an audio on user request, `obj_id` must refer to a
*blob object* storing an audio file.

**Note:** linking object `A` to another object `B` opens several questions:

 - if the `obj_id` is `B`, does it mean that `A` links to `B` or `B`
   links to `A`?
 - if `A` is deleted, should `B` being deleted? If not, what would
   be the state of the link if `obj_id` points to a deletes object?
 - cycles are possible, what should happen?

These questions are meant to be answered in a future RFC.

### Tag

Any object can be *tagged* with some arbitrary text.

```cpp
struct cont_tag_t {
    struct obj_hdr_t hdr;   // type: 0x013

    uint8_t {
        uint meaning   : 5;  // mask 0xf8
        uint reserved  : 3;  // mask 0x07
    };
    uint8_t tag[/* null terminated */];
};
```

`tag` is null-terminated UTF-8 encoded text possibly padded with an
additional zero byte to make the size of `struct cont_tag_t` multiple
of 2.

## Stream

### Stream of Objects

A *stream* is defined as:

```cpp
struct stream_desc_t {
    uint8_t reserved;
    struct ext_arr_t exts;
}; // size multiple of 2
```

The `exts.ext_cnt` of a *stream* should be equal or greater than 2.
This makes room for at least 2 entries in the array `exts` even
if they are zero'd so the *stream* can grow and add more
*data blocks* without requiring changes in its *descriptor*.

The *data blocks* pointed by the same extent should be considered
a *contiguous* buffer from which **complete / non-fragmented**
*object descriptors* can be read.

An *object descriptor* must not be fragmented and spread across
different *data blocks* of **different** extents.

The *stream* works as an *append-only* journal: new *objects*
are added to the end but no *object* can be removed or resized.

*Objects* may be marked *in-place* as deleted (become *zombie*),
removed or shrank leaving a *hole* or may be expanded with a
*continuation*.

In no case the layout in disk of a *stream* should change except
only during a *compaction*.

Hence the need to put in the *object descriptors* the most likely
attributes that can change so they can be updated *in-place*
without affecting the layout.

When appending a new entry to the *stream*, if it does not fit because
it is at the end of the latest *data block* of the extent, the entry
is added somewhere else and the space remaining will be wasted.

This space must be filled with a `struct obj_discard_bytes_desc_t`
to describe how many bytes are left.

If no space for `struct obj_discard_bytes_desc_t` exists, pad with
zeros.

On load, the library should access the latest valid extent in `exts`,
load its blocks and scan from the beginning in search for the
`struct obj_end_of_stream_desc_t` *descriptor* that signals the end.

A *stream* must have one and only one `struct obj_end_of_stream_desc_t`
at its end.

### Rationale - Append-Only

Xournal++ elements tend to have two parts: one that it is likely to
be modified by the user and the other that it is unlikely or even
impossible to change.

The former part tends to be a small set of known fixed-size attributes,
of only a few bytes; the latter tends to be an unknown fixed-size,
possibly large data.

For example, when an user creates a *stroke*, its position and
color can be changed but not the amount of points.

If an user deletes a part of a *stroke* cutting it into halves,
Xournal++ models this as a full deletion of the former and the creation
of 2 independent *strokes*.

The points of a *stroke* cannot be changed.

This suggests two different locations for the data.

Most of the *streams* will represent (part of) *pages* with several
*objects* to be rendered (from a ~10 to ~1000 elements).


### Reordering

The application is in its own right to reorder the *objects* of
a *stream*, this RFC does not put any restriction on that.

The restrictions on the reordering are for any possible reordering
that the *library* may do (not the application).

Let be a *stream* with two objects `A` and `B` where `A` and `B`
are *object descriptors*.

If `A` is before than `B`
(schematically `[.. A .. B ..]`), then the library guarantees that if a
reorder happen, the *relative order* between the objects
*will not change*.

In other words, `A` will still be before `B`.

For *continuations* something similar applies: if an object `A` has 2
*continuations* `a1` and `a2` and `a1` is before `a2`, then the library
guarantees that if a reorder happen, `A` will still be before `a1`
and `a1` will still be before `a2`
(schematically `[.. A .. a1 .. a2..]`)

The library also must guarantee that any *continuation* of an *object*
appear after the *object descriptor*.

The relative order preserved can be used by Xournal++ to encode
the rendering order of the objects of a *page*.

### Compaction

*Zombie objects* are *object descriptors* in a *stream*
marked as dead but still occupying space in the *stream* (in the
form of *descriptors* and *continuations*) and space in *data blocks*
(pointed by the *object descriptor*).

The library should track how many *zombies objects* a *stream* has, how
many bytes in the *stream* are unreclaimed and many bytes in the *data
blocks* are unreclaimed.

If the *stream* grows beyond certain threshold, the library may do a *compaction*:
a full scan and rewrite of the *stream*.

The author does not think that a sophisticated mechanism is required
but some kind of minimal compaction should be implemented.

If an *zombie object* is removed from the *stream*, all
the *continuations* pointing to it **must** be removed as well.

This is because as long as the *object* exists in the *stream*,
its *object id* is still in-use and *continuations* can safely
point to it because no other *object* will have the same id.

When removing an *object* and releasing the its *object id*,
a *continuation* pointing to it would be ambiguous: is it
pointing to the deleted *object* or is pointing to the
future-incoming new *object* that by luck has the same id?

*Links* between an *object* and a *zombie* should be handled too.

This RFC does not define exactly how or when to do the compaction
or if the compaction has to be done per extent basis, or across
all the extents, if objects from one extent should migrate
to a previous or if empty extents should be released as well
or what to do with linked objects.


## Tiles and Layers

### Tile Object

A *tile descriptor* is a specialization of a *stream descriptor* .
The content of the *tile* is stored in the `exts` extents of the *stream*
`content`.

This is where most of the *objects* and *continuations* will leave.

```cpp
struct tile_desc_t {
    struct obj_hdr_t hdr;   // type: 0x081
    struct stream_desc_t content;
    uint32_t row;
    uint32_t col;
};
```

The `row` and `col` are the location of this *tile* in the
mosaic defined by the *layer* that owns this *tile*.

### Background Tile Object

The *background tile descriptor*  has the following structure:

```cpp
struct bg_tile_desc_t {
    struct obj_hdr_t hdr;   // type: 0x082
    uint32_t row;
    uint32_t col;

    float width;
    float height;

    uint32_t {
        uint bg_type   : 2;  // mask 0xc0000000
        uint reserved  : 8;  // mask 0x3fc00000
        uint obj_id    : 22; // mask 0x003fffff
    }

    uint32_t index;
};
```

The supported background types (`bg_type`) are:

 - 0x0: Image: `obj_id` should refer to an *image object*
 - 0x1: PDF: `obj_id` should refer to a *blob object* encoding a PDF file
 - 0x2: Solid: `obj_id` should refer to a *solid background object*

The `row` and `col` are the location of this *tile* in the
mosaic defined by the *layer* that owns this *tile*.

The `index` selects which page of the PDF file should be used
as the background. For the rest of the types, the field is
not used.

### Layer Object

A *layer* is a 2-dimensional matrix or *mosaic* of *tiles*
using a *stream of tiles*.

This *stream* is may be a sequence of `struct tile_desc_t`
or `struct bg_tile_desc_t` and optionally of
*layer* and *tile continuations*.

A location in the *mosaic* may have a *tile* or not:
Empty locations are possible.

![image](https://github.com/eldipa/xoz/assets/2665522/75f920fd-a947-4c6b-9961-eb1229d52a94)

The *layer descriptor* is as follows:

```cpp
struct layer_desc_t {
    struct obj_hdr_t hdr;   // type: 0x083
    struct stream_desc_t tiles_stm;

    uint8_t capacity;
    char name[/* capacity */];  // UTF-8 null terminated
}
```

The `name` is the optional name of this *layer*. It is a null terminated
UTF-8 string but more bytes may had been reserved (`capacity`) to pad
the structure and have a size multiple of 2.

## Document

```cpp
struct doc_desc_t {
    struct obj_hdr_t hdr;   // type: 0x080
    struct stream_desc_t layers_stm;

    // more fields
};
```

The *layers* in the *stream* `layers_stm` are ordered:
the first *layer* corresponds
to the *background layer* at the bottom of the stack. Subsequent
*layers* follow the stack-order.

![image](https://github.com/eldipa/xoz/assets/2665522/519a7424-c580-42a2-bc24-2996b5f862b5)

The rest of the `struct doc_desc_t` will be defined in a future RFC.

This includes:

 - tracking of global *objects* (the ones that do not live in a tile)
such *fonts*, *PDF* and other *"resources-like" objects*.
 - metadata about the document creation, history and user defined data
 - additional settings and configurations of Xournal++ like *page template*

# Backwards and Forwards Compatibility

The new `.xoz` is not backward compatible and it cannot be read by
previous versions of Xournal++.

`.xoz`-aware versions of Xournal++ should be able to read `.xopp` so a
migration from `.xopp` to `.xoz` can be made by Xournal++ without any
user intervention (except upgrading Xournal++).

The size of each *object descriptor* is in the header
`struct obj_hdr_t`: the library should use this to know how many
bytes should be read or ignore.

This allow the *descriptors* to grow in size in future versions
of `.xoz` without breaking compatibility.

*Continuations* are also a way to extend them.

A version of Xournal++ may not be able to understand completely
a higher version of `.xoz` but it should be capable of ignore what
it does not understand and process the rest without trouble.

This RFC does not define the *features flags* schema for `.xoz`.


# Security Implications

The proposed `.xoz` does not introduce any additional security
implication with respect the current `.xopp` format.

We should take care and defenses against possible
malicious `.xoz` files:

 - any `size`/`length`-like fields may trigger the parser to
reserve huge amount of memory for "very large" objects.
 - objects have references to other objects (like a *background tile*
pointing to a *font object*), the parser should protect
itself against recursive cycles.

# How to Teach This

Once `.xoz` gets integrated and a `.xoz`-aware Xournal++ version gets released,
we should document in the webpage about the new format.

Users having at least one pre `.xoz` version of Xournal++ should stick
to `.xopp` format until they can upgrade.

`.xoz`-aware Xournal++ versions will be able to convert `.xopp` files
into `.xoz`.

Xournal++ developers and contributors that wants to know more about the
file format and the library are encouraged to read this RFC.

# Reference Implementation

Git repository: [https://github.com/eldipa/xoz](https://github.com/eldipa/xoz)

Currently implemented features:

 - Block allocator

# Alternative Ideas

## Use an Archive Format

Some these [archive formats](https://en.wikipedia.org/wiki/List_of_archive_formats) include:

 - `tar` [manpage](https://www.gnu.org/software/tar/manual/html_node/Standard.html)
 - `zip` [rfc 1952](https://datatracker.ietf.org/doc/html/rfc1952)
 - `7z` [ref](https://py7zr.readthedocs.io/en/latest/archive_format.html)
 - `zpaq` [ref](http://mattmahoney.net/dc/zpaq.html)


To the author's knowledge, none of these format support updating a file entry
without rewriting the archive entirely.

`zpaq` and `tar` allow to *append* new versions of an entry but they
don't reuse the space of the former to store anything else.

For an application like Xournal++ this may be a problem.

An user could generate a Tex/Latex generated image, then delete it and
regenerate another changing the Tex/Latex source code.

For each regeneration a entirely new image would be appended to the archive
without reusing the space left by the previously deleted image.

## Journal Append-Only Format

Instead of using *data blocks* and a non-trivial block allocation
schema, it would be much easier to have an append-only journal-like
file.

Creation modifications and deletions are appended to the end of the
file. On reading, the library would require to read the entire file and
discard any item that is modified later.

This is how PDF works but this
strategy generates large files and forces the application to wait
for reading the entire file on open.

The append-only journal-like strategy is applied to *streams* to get
most of the benefices but with limited deficiency.

# Open Issues / Questions

## Inline Data

For *small objects* like texts, it may be better to store the data
within the *descriptor* or one of its *continuation* to avoid
fragmentation.

By how much?

## Markdown

`struct obj_text_desc_t` has a reserved `flags` field. A future
extension could interpret this to enable Markdown text.

Support for Markdown could enable a bunch of features on Xournal++:

 - rich text: [issue 4503](https://github.com/xournalpp/xournalpp/issues/4503), [issue 3659](https://github.com/xournalpp/xournalpp/issues/3659), [issue 729](https://github.com/xournalpp/xournalpp/issues/729)
 - links: [issue 4772](https://github.com/xournalpp/xournalpp/pull/4772)

Links could not be just to web resources (URL) but to internal ones like
jumping to a arbitrary page or playing an audio.

Almost everything in `.xoz` has an *object id* which identifies uniquely
the object so mounting anchoring and linking on top of that is possible.

However a *reference count* should be taken into account so we can know
when an object is safe to be deleted or not based if other object
points/links to it.

## Minimum Extent Count for Stream

This RFC proposes a minimum of 2 *extents* to hold all the
*data blocks* of a *stream* with the expectation to be enough
and avoid any reallocation or expansion.

Will be this enough?

## Object Zombies (not reclaimed)

*Objects* are marked as destroyed or deleted in its *descriptor* with a
single bit preserving the *descriptor*, its *continuations* and
*data blocks*.

The library may defer any deletion in case the application wants to
*"resurrect"* the object.

For how much we should avoid reclaiming the space of such *zombie
objects*?

What about the *objects* which *stream* is deleted? Should the *stream*
deletion deferred? Should be deleted but its *objects* temporally
preserved (aka *orphans*)?

## Compression

A compression could be applied to the *data blocks* of a *object* based on its *type*.

The same compression context could be reused to compress *text objects*
from the same *stream* so it can be achieved better compression ratios
compared with compressing each *text object* separately.

For binary embedded files the library
could try to compress it once and if the compression ratio worth it, it
may decide to leave it compressed or not, trading off space by CPU time.

For *strokes*, how their coordinates should be encoded?
(aka *point encoding*)

What compression algorithm(s) use, if any, it open to research.


## Transformations

Xournal++ computes the scale or rotation of *stroke*'s points
or *image* and save it as is as a new stroke/image.

It could be possible with the new `.xoz` format to add transformations:
the strokes/images are stored without any rotation/scale applied and
such transformation is annotated.

The application would apply such transformation each time the object is
read.

Would be this useful? We can avoid rewriting the whole stroke/image on a
simple rotation/scale. More over we can preserve the original data and
revert any transformation without losing quality (raster images are
particular sensible to this).

Such transformation could be encoded as 4 numbers `[a, b, c, d]`:

 - `[sx, 0, 0, sy]`: scale the object by `sx` in the `x` axis and `sy`
    in the `y` axis.
 - `[cos(q), sin(q), -sin(q), cos(q)]`: rotates the object by `q`
    degrees clockwise.
 - `[1, tan(wx), tan(wy), 1]`: skews the object by `wx` degrees
    in the `x` axis and `wy` degrees in the `y` axis.

Reference: 2008 PDF Reference, Section 8.3.3

## Tolerance to Failures

The current RFC does not talk about any mechanism to prevent data
corruption in case of a crash or to recover from corrupted files.

Binary format is particularly sensible: a single bit flip may render the
whole file unreadable.

The author considers that this must be resolved and taken into account
before releasing the file format.

# Previous work

People in the past tried to reduce the impact of using a text-based
format (XML):

 - Reduce the file size writing less digits in the strokes' points - [issue 1277 (merged)](https://github.com/xournalpp/xournalpp/issues/1277)
 - Encode in binary-raw format the PNG images - [issue #3416 (open)](https://github.com/xournalpp/xournalpp/issues/3416)

About the load/save time, there were also some tried to reduce them:

 - Explore different compression strategies. `zstd` and `brotli` are
the winners but it was shown that encoding in binary and not in text is even better - [issue 1869 (research/proposal)](https://github.com/xournalpp/xournalpp/issues/1869)
 - Per page `"was modified"` flag so the saving would write to disk only
those pages which changed - [issue #2130 (proposal)](https://github.com/xournalpp/xournalpp/issues/2130)
 - Write in parallel (speed up) and/or in background (do not block the user) approaches were explored - [issue #2684 (proposal)](https://github.com/xournalpp/xournalpp/issues/2684)

There were some work on embedding other files and making `.xopp`
"self-contained":

 - Enable non-PNG image loading for `.xopz` (but PNG was enforced for backward compatibility) - [pull request #3782 (merged)](https://github.com/xournalpp/xournalpp/pull/3782)
 - Embed PDF inside Xournal++ files - [issue #4252 (open)](https://github.com/xournalpp/xournalpp/issues/4252), [issue #4249 (open)](https://github.com/xournalpp/xournalpp/issues/4249)
 - Background images are not embedded (bug) - [issue #3845 (open)](https://github.com/xournalpp/xournalpp/issues/3845)


# Copyright/License

This document is placed in the public domain or under the
CC0-1.0-Universal license, whichever is more permissive.

# References

For the reader that wants to learn more about the `.xopp` format,
it is fully described in [issue #2124](https://github.com/xournalpp/xournalpp/issues/2124).

The first discussions about the need of a new format and the proposed
ideas can be found in [issue #937](https://github.com/xournalpp/xournalpp/issues/937).


