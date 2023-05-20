# XOZ Format

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 1

# Abstract

We propose a new binary format `.xoz` to support Xournal++ specific
needs:

 - Random read access to data associated with a document's page
including strokes, images and other embedded files.
 - Random write access to update in-place the associated data with a
page.

The present RFC describes the `.xoz` format and
which modifications to Xournal++ source code are required to integrate
the new format and which may be delayed for a future version.

# Motivation

Xournal++ has `.xopp` as its unique
file format: a text-based XML file compressed with GZip.

The XML format does not support random access for reading nor modifications
in-place. Due its text-based nature, it require an explicit encoding to
store binary data.

These limitations are not specific to Xournal++, it is just how XML
works.

Compressing the entire file with GZip does not help.

While it reduces the file size, trading it by CPU time, it also prevents
random read/write access.

This means that to read and render a page, Xournal++ requires to
uncompress and read the entire file; On saving, it requires to compress
and write the entire file even if only a single page changed.

For small files, this is not a real issue but it is inadequate
to handle large contents.

Users had reported in the past that `.xopp` files were "too large"
or Xournal++ takes "too much time" in load them.

People in the past tried to reduce the impact of using a text-based
format (XML):

 - Reduce the file size writing less digits in the strokes' points - [issue 1277 (merged)](https://github.com/xournalpp/xournalpp/issues/1277)
 - Encode in binary-raw format the PNG images - [issue #3416 (not merged)](https://github.com/xournalpp/xournalpp/issues/3416)

About the load/save time, there were also some tried to reduce them:

 - Explore different compression strategies. `zstd` and `brotli` are
the winners but it was shown that encoding in binary and not in text is even better - [issue 1869 (not merged)](https://github.com/xournalpp/xournalpp/issues/1869)
 - Per page `"was modified"` flag so the saving would write to disk only
those pages which changed - [issue #2130 (not merged)](https://github.com/xournalpp/xournalpp/issues/2130)
 - Write in parallel (speed up) and/or in background (do not block the user) approaches were explored - [issue #2684](https://github.com/xournalpp/xournalpp/issues/2684)

While read performance can be amortized in time (we don't expect a
typical user to open several files every minute), write performance is
more critical.

Xournal++ writes to disk a backup file every 5 minutes or so for crash
recovery so even if the user does not modify the document, he/she will
pay the cost.

Moreover, current SSD disks degrade their performance after multiple
writes. This is specially true for phones / tablets that ship with more
"cheap" SSD disks.

On top of those limitations, there is interest in adding even more
data into a `.xopp`, making Xournal++ files "self-contained".

 - Enable non-PNG image loading for `.xopz` (but PNG was enforced for backward compatibility) - [pull req #3782 (merged)](https://github.com/xournalpp/xournalpp/pull/3782)
 - Embed PDF inside Xournal++ files - [issue #4252 (not merged)](https://github.com/xournalpp/xournalpp/issues/4252), [issue #4249 (not merged)](https://github.com/xournalpp/xournalpp/issues/4249)
 - Background images are not embedded (bug) - [issue #3845 (open)](https://github.com/xournalpp/xournalpp/issues/3845)

The author would add also embed audio files and specially fonts. While Xournal++
gracefully fallbacks to a default font if the one specified in the
`.xopp` is found, it will be easier for the users to have everything in
the same file.

Trying to support any of those embedded files into `.xopp` would require
encoding their binary data, necessary increasing the file size.


All the pain points and feature requests boil down to support:

 - Random read access: this means allow Xournal++ to read
only a fraction of the file in order to render an arbitrary page.
 - Random write access: knowing which part of document had
changed, Xournal++ may update and write to disk only those changed,
avoiding a entire rewrite.
 - Efficient representation of strokes and points.
 - Embed arbitrary files, including but not limited to, PDF, images, audio
and fonts.

# Overview

## Objects

In this RFC, Xournal++'s strokes, images and texts are globally named
*objects*.

An *object* has three parts:

 - a *object descriptor*
 - zero or more *continuations*
 - zero or more *data blocks*

The *object descriptor* is the only required piece of information that
describes the object.

For cases where the *descriptor* is not enough or additional metadata
is added, the *descriptor* can be
continued or extended with a *continuation*.

*Continuations* always live in the same *stream*
where its *object descriptor* lives.

An *object* may require much more space that its *descriptor* can offer
to store its data. This is stored in *data blocks*.

The *object descriptor* and/or one or more *continuations* will encode
which blocks belong to the object using *extents*.

## Streams

A *stream* is a ordered sequence of *object descriptors* and their
*continuations*.

All the *objects* belongs to one and only one *stream*.

A *stream* has two components:

 - a *stream descriptor*
 - zero or more *data blocks*

The *data blocks* of a *stream* contains the ordered sequence of
*object descriptors* and their *continuations*.

The implementation is an *append-only* stream:

 - modification of *object descriptors* is allowed if they can be do it
   *in-place* (without requiring displacements)
 - deletion of *object descriptors* is achieved marking *in-place*
   the deletion (see `struct obj_hdr_t`) or overwriting the
   *descriptors* with zeros.
 - modifications that cannot be done *in-place* or via *continuations*,
   the original *descriptor* is replaced with a
   `struct desc_moved_downstream_t`
   and the modified *descriptor* is appended at the end of the *stream*

The library may run a *compaction* if detects that a significant
amount of space can be recovered.

# Rationale

## Object and Stream Descriptors

The reason behind having separated the *descriptor* from the *object*'s
or *stream*'s main body is because we can store there attributes
that are likely to be changed.

To mention a few examples:

 - strokes' and texts' color
 - strokes', texts' and images' position (x and y)

Having them in a *descriptor* allow to do only a *tiny update in-place*
with the possibility of modifying several *descriptors* of the
same *stream* all at once (because they would be probably in the
same *stream*'s *data block*).

If these attributes where in a *data block*, would require an additional
lookup (from the *descriptor* to the *data block*) and would definitely
require accessing and modifying several blocks.

## Append-only Streams

Xournal++ has only a few object types:

 - stroke
 - text
 - image
 - teximage
 - embedded file

All of them, except text, are of fixed size: once known the size will
not change.

This allows to allocate the exact count of *data blocks* and *extents*
required and having the *descriptor* of a fixed size
(see `struct desc_stroke_t` for an example).

Nothing needs to be expanded so everything can be consecutively written
without padding and without worrying to reserve some slack space for
growing.

The library would require to read the entire *stream* once and optionally
build an *index* for direct random access to each *object descriptor*.

And keeping the index updated, where the indexed entries don't move,
is much simpler.

Such index enables modifications of common attributes like color
and position to be done *in-place*.

The author believes that most of the changed done by the users
are mostly creation of new objects and modification such translations
of existing ones. For this an *append-only* structure fits well.

On deletion, deleted *objects* are marked or zero'd leaving *"holes"*
in the *stream* which will not be filled until a *stream compaction*.

The *append-only* implementation trades off speed by space (wasted
in fragmentation).

The author believes that this use case is less frequent than
the rest and the benefits and simplicity of the *append-only* implementation
compensate the temporal fragmentation.

## Moved downstream

For text objects the thing may be different: they may grow or shrink in size
and they are small enough to not be worth of spending *full data blocks*
on them.

In this case it may be better to store the text inside the *descriptor*
(aka *inline data*) but it is an open question yet.

A general solution is to replace the *text descriptor* with a special
*descriptor* that signals that the object was moved downstream:
`struct desc_moved_downstream_t`.

Then, the modified (and possibly larger) *text object* is appended at
the end of the *stream*.

This *preserves* the original ordering of the objects in the *stream*.

# Specification

All the present structures in this RFC are the structures in-disk that form the
`.xoz` file. They are aligned to 1 byte, with numbers in little endian.

## Data Block

*Data blocks* are the basic unit of allocated space to be used. With the
exception of the *trailer*, anything is stored in a *data block*.

The `.xoz` file is divided into blocks of some fixed size: 1KB but other
power of 2 greater than 1KB could be used.

Each *data block* allocated and in use is assigned to an *object* or to a
*stream*.

A `.xoz` file may be larger than the sum of the in-use blocks: the
library may had allocated more blocks which remain free.

The administration of the *data blocks* is in charge of the
*block allocator*.

The mechanisms of the *block allocator* are not part of this RFC.

### Block Number 0

The block number 0 is a special one and it cannot be accessed, pointed
to, reused or freed.

It is exclusively used to store the *header* and other important
main structures: *free map* and the *page index*.

### Free Map

The *block allocator* needs to know which blocks are in-use and which are
free.

The *free map* contains a snapshot of the block usage so the
*block allocator* is not required to scan the entire file and detect
heuristically which blocks are free and which aren't.

```cpp
struct free_blk_map_t {
    uint32_t next_blk_nr;

    uint16_t extcnt;
    struct extent_t exts[/* extcnt */];
};
```

The `next_blk_nr` is the block number of the next block that expands
and continues the *free map*. If it is zero, no more blocks
are needed to complete the *free map*.

`extcnt` counts how many entries the following `exts` array has.
Note that this `extcnt` is `uint16_t` and not `uint8_t` as it is
defined in `struct ext_arr_t`.


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
    uint8_t extcnt;
    struct extent_t exts[/* extcnt */];
}
```

The `extcnt` is *fixed* at the moment of the creation, so the size of
`struct ext_arr_t` is known.

It is possible to *pre-allocate* more blocks than the required, just
having a larger *extent* or an extra *extent* in the array.

This enables room for growing at the expense of wasting unused space.

Therefore the exact byte count/size/length of useful data is defined
elsewhere and dependent of the type of the *object* , *stream* or
similar.

A `blk_nr` and `blk_cnt` of zeros means that that *extent* is inactive.
Because `extcnt` says how many entries the `exts` array has, this is the
only way to signal that an entry (an *extent*) should be skipped.

### Page index



## Object

Any object share a common header:

```cpp
struct obj_hdr_t {
    uint16_t type; // bits: <DCRR RRTT TTTT TTTT>
    uint32_t id;
};
```

Each object is identified uniquely by its `id`.

The most significant bits of `type` have a special purposes:

 - The MSB says if the object is alive (0) or dead (1)
 - The next bit says if the object is an ordinary
object (0) or if it is a *continuation* (1).
 - The next 4 bits are reserved.
 - The remaining 10 bits encode the type of the object.


The `type` of an object defines how to interpret the rest of the fields
of `struct obj_hdr_t` and possibly the bytes *after* the structure.

`.xoz` supports different types of objects which are defined below.


### Stroke Objects

The stroke objects are defined as:

```cpp
struct desc_stroke_t {
    struct obj_hdr_t;
    uint32_t color_rgba;

    struct ext_arr_t;
};
```

Data blocks pointed by the extents store the following:

```cpp
struct data_stroke_t {
    uint16_t flags;  // bits: <FFFF CCCC PPPP TTTT>
    uint32_t pointcnt;
    float coords[/* pointcnt * (2 or 3) */];
};
```

 - bits 0 to 3 (mask 0x000F) encode the *tool* used to draw the stroke:
currently `pen` (0), `highlighter` (1) and `eraser` (2). The bit pattern `1111`
(15) is reserved.

 - bits 4 to 7 (mask 0x00F0) encode the style or *pattern* of the stroke:
currently `solid` (0), `dot` (1), `dashdot` (2), `dash` (3). The bit
pattern `1110` (14) means that it is a *custom style/pattern* and it is defined
in the *continuation* `struct cont_stroke_pattern_t`. The bit pattern `1111` (15) is reserved.

 - bits 8 to 11 (mask 0x0F00) encode the *cap-style* of the stroke:
currently `butt` (0), `round` (1) and `square` (2). The bit pattern `1111`
(15) is reserved.

 - bits 12 to 17 (mask 0xF000) encode the *fill transparency* of the stroke: it is
interpreted as an `uint8_t` number with 1 being a fill nearly transparent
and 255 a fill fully opaque. The bit pattern `0000` (0) means no fill at
all.

**TODO:** explain about the continuations for a stroke to define
*custom* style patterns.

### Resources

Resources are objects that can be *referenced* by zero, one
or more objects.

```cpp
struct resource_t {
    struct obj_hdr_t hdr;
    uint32_t refcnt;

    uint8_t extcnt;
    struct extent_t exts[/* extcnt */];
};
```

The `refcnt` field tracks how many other objects are referencing this
resource.

This RFC does not mandates what to do with a resource with a `refcnt`
of zero.

The application may decide to delete the resource to release space
**but** it may also delete data that the user may not be
able to recover later.



```cpp
struct res_font_t {
};
```


## Object's Continuations

As said any object may require more information or be extended in
someway beyond what its *descriptor* offer.

An *object continuation* is an structure that looks like an ordinary
object but with the most significant bit of `type` field set to 1.

The `id` of `struct obj_hdr_t` is reinterpreted not as the `id` of the
continuation but the `id` of the object that this continuation is
extending.

#### More Extents

The *continuation* adds `extcnt` additional extents to the object. The
interpretation of the data blocks is up to the original object.

```cpp
struct cont_more_extents_t {
    struct obj_hdr_t;

    uint8_t extcnt;
    struct extent_t exts[/* extcnt */];
};
```

If an *object descriptor* has a `struct ext_arr_t` with an `extcnt`
of 2, let's say `[A, B]`, if a following `struct cont_more_extents_t`
adds 3 additional extents `[C, D, E]`, then the *data blocks* of the object
are described by the extents `[A, B, C, D, E]`, in that order.

More than one `struct cont_more_extents_t` can be used to extend
the same *object*.


#### Stroke's Non-Standard Pattern

A *stroke* may be draw using a non-standard pattern. This pattern is
defined as a array of `valuecnt` values which interpretation is
based on `pattern_type` and it is up to the application.

```cpp
struct cont_stroke_pattern_t {
    struct obj_hdr_t;

    uint8_t pattern_type;
    uint8_t valuecnt;
    float values[/* valuecnt */];
};
```

#### Audio Attachment

**TODO:** this requires more thought

Any object can have one or more audio attached. On user request,
Xournal++ can then play it.

```cpp
struct cont_audio_attach_t {
    struct obj_hdr_t;

    uint32_t iobj_audio;
};
```


## Stream

### Stream Descriptor

```cpp
struct desc_stream_t {
    uint32_t last_blk_nr;
    uint16_t offset;

    uint8_t extcnt; // extcnt >= 2
    struct extent_t exts[/* extcnt */];
};
```

The `last_blk_nr` and `offset` indicate the block and offset within of
the last entry added to the *stream* so it is not required to scan
entirely.

On load, the library may read the next few bytes and check that they are
zeros just to confirm it is at the end of the *stream*.

The `extcnt` of a *stream* should be equal or greater than 2. This makes
room for at least 2 entries in the array `exts` even if they are zero'd
so the *stream* can grow and add more *data blocks* without requiring
changes in its *descriptor*.



### Reordering

The application is in its own right to reorder the *objects* of
a *stream*, this RFC does not put any restriction on that.

The restrictions on the reordering are for any possible reordering
that the *library* may do (not the application).

Let be a *stream* with two objects `A` and `B` where `A` and `B`
are *object descriptors* or the first `struct desc_moved_downstream_t`
*descriptors*.

If `A` is before than `B`
(schematically `[.. A .. B ..]`), then the library guarantee that if a
reordering happen, the *relative order* between the objects
*will not change*.

In other words, `A` will still be before `B`.

For *continuations* something similar applies: if an object `A` has 2
*continuations* `a1` and `a2` and `a1` is before `a2`, then the library
guarantees that if a reordering happen, `A` will still be before `a1`
and `a1` will still be before `a2`
(schematically `[.. A .. a1 .. a2..]`)

The library also must guarantee that any *continuation* of an *object*
appear after the *object descriptor* or after its first
`struct desc_moved_downstream_t` *descriptor*.

These rules guarantee that when the library read the *stream* in order
it will find first or the *object descriptor* or its `struct
desc_moved_downstream_t` *descriptor* and only then its continuations.

The relative order preserved can be used by the application to encode
the rendering order for example.

### Compaction

The library should track how many *zombies objects* a *stream* has (dead but not
removed), how many *zero'd holes* has (dead objects that were removed)
and how many bytes are not reclaimed.

This not reclaimed space comes from space wasted in *zombies* and
*zero'd holes* inside the *stream* but also comes from the *data blocks* of dead
objects and from their *continuations*.

If the *stream* grows beyond certain threshold, the library may do a *compaction*:
a full scan and rewrite of the *stream*.

This RFC does not defines exactly how or when.


# Backwards Compatibility

The new `.xoz` is not backward compatible and it cannot be read by
previous versions of Xournal++.

`.xoz`-aware versions of Xournal++ should be able to read `.xopp` so a
migration from `.xopp` to `.xoz` can be made by Xournal++ without any
user intervention (except upgrading Xournal++).

It may be theoretically possible to write `.xopp` files in such a way
that a `.xoz` is embedded inside.

`.xoz`-non-aware versions of Xournal++ would read/write `.xopp` files
as usual ignoring the `.xoz` content; `.xoz`-aware would require to read
once the modifications in the `.xopp` part and translate them to the `.xoz`
on loading.

None of this was thoroughly thought-out however: this RFC does not
explore this any further and it should not be considered for granted
that it may be implemented in the future.

# Security Implications

The proposed `.xoz` does not introduce any additional security
implication with respect the current `.xopp` format.

We should take care and defenses against possible
malicious `.xoz` files:

 - any `size`/`length`-like fields may trigger the parser to
reserve huge amount of memory for "very large" objects.
 - objects have references to other objects, the parser should protect
itself against recursive cycles.

# Integration Roadmap

The author proposes the following milestones

 1) Implement a working `.xoz` library (but perhaps not fully optimized)
 2) Refactor `LoadHandler` and `SaveHandler` to delegate everything to a
    `.xopp` library. `LoadHandler` and `SaveHandler` will still
    load/save an entire `.xopp` file but most of the code will be behind
    the library
 3) Integrate `.xoz` library and make it accessible from `LoadHandler`
    and `SaveHandler`.

With milestone 3, Xournal++ will be able to read and write both `.xopp`
and `.xoz` files. Being still `.xopp` the default but the user will
allowed to change it.

We can configure the save-on-crash and backup systems to write both
`.xopp` and `.xoz` just in case.

When we have confidence, we can change the default to `.xoz`.

This is the most sensible phase as it requires the user to upgrade
their Xournal++ versions and start migrating the files.

People may have different devices with different versions and they may
not be able to upgrade all of them. If an user has a single pre-`.xoz`
version, the user will probably not want to switch to `.xoz`.

Adoption of `.xoz` will take time so releasing a Xournal++ version with
support fo `.xoz` as soon as possible is important.

In the meantime, we can work on a better integration. After milestone 3
Xournal++ still loads and saves the entire `.xoz` file, not taking
advantage of the random access and in-place writing.

The next milestones would be:

 4) Design a *page management (PM)* layer on top of `.xoz`: Xournal++ will
 assume that all the pages are loaded in memory (as today) but it will
 not.
 On request, the PM will load the page from `.xoz`; on memory pressure
 or timeout the PM will offload the page saving it on disk.
 5) Implement and integrate the page management.

The last milestones are the ones that will enable Xournal++ to take full
advantage of `.xoz` and they can be developed in the meantime the users
slowly upgrade and migrate to `.xoz`.

# How to Teach This

Once `.xoz` gets integrated and a `.xoz`-aware Xournal++ version gets released,
we should document in the webpage about the new format.

Users having at least one pre-`.xoz` version should stick to `.xopp`
format until they can upgrade.

`.xoz`-aware Xournal++ versions will be able to convert `.xopp` files
into `.xoz`.

Xournal++ developers and contributors that wants to know more about the
library are encouraged to read this RFC.

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

This is how PDF works but as explained in the previous section, this
strategy generates too large files and forces the application to wait
for reading the entire file on open.

The append-only journal-like strategy is applied to *streams* to get
most of the benefices but with bounded defects.

# Open Issues / Questions

## Block Size

Larger block size means much smaller and fewer *extents*, useful for
storing large embedded files, like images.

On the other hand it means more wasted space due internal fragmentation.
This is specially true for small objects like texts.

The author believes that a block size of 1KB is the sweet spot but more
experimentation is required.

Sub-block allocation is possible too to mitigate the internal
fragmentation (but trading it with external fragmentation).

## Block Alignment

This RFC does not mandate the begin of a block to be aligned to any
particular number. When the `.xoz` starts at the begin of the file, each
block will be naturally aligned to its size.

Should we check this?

## Inline Data

For *small objects* like texts, it may be better to store the data
within the *descriptor* or one of its *continuation* to avoid
fragmentation.

By how much?

## Reference Count

Global objects have a reference count to know if it is safe to deleted
or not.

Should the non-global objects have a reference count too?

The author couldn't think in a use-case to justify it but the PDF format
apparently allows to do it.

## Minimum Extent Count for Stream

This RFC proposes a minimum of 2 *extents* to hold all the
*data blocks* of a *stream* with the expectation to be enough
and avoid any reallocation or expansion.

Will be this enough?

## Stream-Local Objects' IDs

The RFC uses object ids that are unique globally: all the objects of all
the streams including global objects have an unique id.

This requires 32 bits (but possibly fewer bits are ok).

Should *objects* living in a *stream* have an id unique within that *stream*
but not globally?

We may save 1 byte or 2 per object.

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

## Infinite Pages

`.xoz` does not handle in any way this but it should be.

## Merge of `.xoz` Files

There are a few tickets and tools of users merging `.xopp` files for
different reasons:

 - [issue #2816](https://github.com/xournalpp/xournalpp/issues/2816)
 - [xopp-merger-app](https://github.com/MrPancakes39/xopp-merger-app)
 - [xoppmerge](https://github.com/sergeigribanov/xoppmerge)
 - [xopp-merger-GUI](https://github.com/afonsosousah/xopp-merger-GUI/blob/main/Form1.cs)
 - [XOPP_Functions](https://github.com/Leonardo0101/XOPP_Functions)
 - [xopp-merger-cli](https://github.com/MrPancakes39/xopp-merger-cli)

Other users had created tools to manipulate the data inside `.xopp`:
 - [extract_formulas](https://github.com/Lautron/xopp2latex/blob/master/extract_formulas.py)

`.xoz` probably should handle this case or at least not block other
solutions.

## `.xoz` Embedded in a PDF

The `.xoz` format will support embed PDF what if we do the other way around:
that `.xoz` be embedded *into* a PDF.

The author thinks that it is possible to do it:

 - contrary to popular belief, the PDF can be updated so a `.xoz` could
be added and eventually updated.
 - the `.xoz` format does not hardcode any file offset: anything is in
terms of block numbers and offsets within the block.

It is not clear if worth it but it should be possible.

## Compression

A compression could be applied to the *data blocks* of a *object* based on its *type*.

The same compression context could be reused to compress *text objects*
from the same *stream* so it can be achieved better compression ratios
compared with compressing each *text object* separately.

For binary embedded files the library
could try to compress it once and if the compression ratio worth it, it
may decide to leave it compressed or not, trading off space by CPU time.

What compression algorithm(s) use, if any, it open to research.

## Transformations

Currently on a rotation or scale, Xournal++ computes the final stroke's points
or image and save it as is as a new stroke/image.

It could be possible with the new `.xoz` format to add transformations:
the strokes/images are stored without any rotation/scale applied and
such transformation is annotated.

The application would apply such transformation each time the object is
read.

Would be this useful? We can avoid rewriting the whole stroke/image on a
simple rotation/scape. More over we can preserve the original data and
revert any transformation without losing quality (raster images are
particular sensible to this).

## Tolerance to Failures

The current RFC does not talk about any mechanism to prevent data
corruption in case of a crash or to recover from corrupted files.

Binary format is particularly sensible: a single bit flip may render the
whole file unreadable.

The author consideres that this must be resolved and taken into account
before releasing the file format.

# Forward and Backward Compatibility

This RFC mandates that certain bits across certain structures are
reserved for future uses. *Continuations* is a mechanism to add or patch
an *object*.

Older version of Xournal++ (but aware of the new `.xoz` format) should
safely fallback.

Is this enough?

# Copyright/License

This document is placed in the public domain or under the
CC0-1.0-Universal license, whichever is more permissive.

# References

For the reader that wants to learn more about the `.xopp` format,
it is fully described in [issue #2124](https://github.com/xournalpp/xournalpp/issues/2124).

The first discussions about the need of a new format and the proposed
ideas can be found in [issue #937](https://github.com/xournalpp/xournalpp/issues/937).


