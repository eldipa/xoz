# RFC: Specification of Descriptor and Descriptor Set

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 2

### Summary of changes

Changed in version 2:

 - rename `edata`, `esize`, `data` and `dsize` to `cdata`, `csize`,
   `idata` and `isize` in `struct descriptor_t`.
 - rewrite from scratch the `struct dsc_dset_t` adding sizes
   and removing the indirect mode.
 - allow for descriptor set inheritance.

# Descriptor

 ```cpp
struct descriptor_t {
    uint16_t {
        uint own_content : 1;   // mask: 0x8000
        uint lo_isize    : 5;   // mask: 0x7c00
        uint has_id      : 1;   // mask: 0x0200
        uint type        : 9;   // mask: 0x01ff
    };

    /* present if has_id == 1 */
    uint32_t {
        uint hi_isize : 1;      // mask: 0x80000000
        uint id       : 31;     // mask: 0x7fffffff
    };

    /* present if own_content == 1 */
    uint16_t {
        uint large    : 1;      // mask: 0x8000
        uint lo_csize : 15;     // mask: 0x7fff
    };

    /* present if own_content == 1 && large == 1 */
    uint16_t hi_csize;

    /* present if own_content == 1 */
    struct segment_t segm;

    /* present if type == 0x1ff */
    uint16_t ex_type;

    /* to be interpreted based on descriptor/object type;
         isize = lo_isize if hi_isize is not present
         isize = hi_isize << 5 | lo_isize if hi_isize is present */
    uint8_t idata[isize * 2];
};
```

The size of the descriptor's `idata` is given in words of 2 bytes by the `lo_isize` field
and, if `hi_isize` is present, `lo_isize` is extended with `hi_isize` as its MSB.
The maximum size of a descriptor without counting its header
is therefore 64 bytes (32 * 2) or 128 bytes (64 * 2).
How `idata` is interpreted depends on the descriptor `type`.

`type` is the descriptor type; there can be up to 511 different types.
This RFC defines some types:

 - `type` 0x00 signals that the descriptor works as padding (some other
   conditions must hold too, see below)

 - when `type` is 0x1ff, the field `ex_type` is present and it becomes the
   (extended) type of the descriptor. Therefore there are two possible ways to encode
   the first 511 types either with `type` or with `type = 0x1ff` and
   `ex_type`.

 - `type` 0xffff is reserved.

 - `type` in between 0x01e0 and 0x01e0 + 2048 (both inclusive) are
    application defined but their implementation must be subclasses of
    the *descriptor set* class.

## Descriptors that own content

The `struct descriptor_t` can refer either to a descriptor
that *owns* blocks of data referenced by `segm` (`own_content` is 1)
or to a descriptor that does not owns any segment (`own_content` is 0).
The term *content* data is to distinguish it from the `idata` field
embedded in `struct descriptor_t`.

If a descriptor owns a segment, it is responsible to free it when the
descriptor is deleted from the *descriptor set*.

How much of the owned segment is in use (has meaningful external data) is given
by `lo_csize` (lower 15 bits) and if `large` is 1, by `hi_csize` (16
most significant bits). This gives an upper limit to how much external data the
segment can hold of 2GB (31 bits).

The `segm` is the segment the indicates where the data is stored
and its length is not explicit in the header. Instead, the segment ends
when an inline-data extent is found so the segment must be *inline-data ended*.

## Descriptor id

Every descriptor has an unique id: it may be given by the field `id`
or it may be generated in runtime by the `xoz` library.

An id generated in runtime is called *temporal id* and it is generated
and assigned to the descriptor on loading. Such
descriptors cannot be referenced by others (because its id may change
between sessions of the application).

Because of this a *temporal id* does not have to be stored in the `xoz`
file so `has_id` should be false. An exception is when the descriptor
size is large enough that the `hi_isize` bit must be set that implies
that `has_id` must also be set. In this case the `id` must be 0 to
signal that there is no id really stored in the file.

An id explicitly stored `id` given  when `has_id` is 1 and `id` is not 0
is called *persistent id*.

In runtime, the `xoz` library will distinguish both with the MSB of the
32 bits number that the id has: if the MSB is 1, it is a *temporal id*,
if not, it is a *persistent id*. Hence the id to be stored in the file
requires only 31 bits (the `id` field).

In runtime, in no case a descriptor can have an id of 0.

## Descriptors invariants

For a *descriptor* that *does not own* content:

 - it has a minimum size of 2 bytes (when `has_id` and `lo_isize` of 0)
 - it has a maximum size of 132 bytes (when `has_id` is 1 and the size of `idata` is (64-1)*2)

For a *descriptor* that *owns* content:

 - it has a minimum size of 6 bytes (`has_id` is 0, size of `idata` is 0
     and the `segm` has the minimum of 2 bytes, no space allocated)
 - there is no maximum size as `segm` is unbound; the descriptor may have up to 136 bytes
   if `segm` is not counted however.
 - the owned data can be up to 2GB bytes

In both cases, the descriptor size is always a multiple of 2;
the owned data is not required to be a multiple of 2.

## Descriptor size is fixed

Once a descriptor is present in the `xoz` file, its size cannot be
changed (aka `lo_isize` and `hi_isize` are fixed).

Growing the descriptor it is not possible because the descriptors
are packed tight in the *descriptor set* and there is no room to grow.

Only if the application wants to store less data in the descriptor,
it may update the descriptor in place, pad with zeros the unused space
and adjust `lo_isize` and `hi_isize` accordingly.

In any case, if a resize is required, the `xoz` library will just write
a new descriptor with the same type and id than the former
to override it (this of course comes at expenses of wasted space).

# Descriptor Type 0: padding

The `type` 0x00 has a special meaning: if `own_content`, `has_id` and `lo_isize` are 0,
the descriptor works as 2 bytes padding (zeros), any other setting is
reserved but the semantics of `lo_isize` and `has_id` holds therefore it is
known how many bytes the header and the descriptor occupy.

# Descriptor Type 1: descriptor set

The *descriptor set* (or just *set*) is a descriptor with type number 1
that always owns content: the `own_content` is always true. When `own_content` is false the whole
descriptor should be treated as reserved.

```cpp
struct dsc_dset_t {
    uint16_t {
        uint psize      : 4;
        uint reserved   : 12;
    }

    /* present if own_content is false */
    uint16_t sflags;

    uint8_t pdata[psize * 2];

    /* only for subclasses (inheritance) */
    uint8_t app_defined_fields[];
}
```
When a *descriptor set* is empty (no descriptors within), the set can
release its `hdr.segm` and set `hdr.own_content` to false.
In this case, the header of the set is stored not in the content section
but in `sflags` field of `struct dsc_dset_t`.

The `psize` is the size of `pdata` in words of 2 bytes;
`pdata` is a reserved space for future attributes.

## Inheritance

In general, a *descriptor* does not have to track the size of its data:
`isize` field is for that.
In the case of future versions of xoz or of the application,
a descriptor just need to add
more fields at the end and if no previous field is modified (the layout
is preserved), no backward/forward incompatibility should arise.

But for a *descriptor set* the things are special: we want to allow the
application to extend from it and add its own fields. To preserve
the layout, it is then needed to track the size of the *descriptor set* specific
fields to separate them from the application ones.

The final `isize` must take into account the size of `struct dsc_dset_t`
including the size of `app_defined_fields`.
The `app_defined_fields` has no explicit size: it represents the possible space
of a subclass for adding its own fields.

## Set content

The content of a *descriptor set* (the space pointed by its `segm`) has
a fixed size structure `dset_header_t` followed by zero or more
*descriptors*, one after the other, with any amount of padding in between.

The `dset_header_t` or just the *header* is as follows:

```cpp
struct dset_header_t {
    uint16_t sflags;
    uint16_t checksum;
};
```

The `sflags` are reserved for future use; the `checksum` is a 16 bits *internet checksum*

The checksum of the *set* is stored in the *header*
and not in its *descriptor* for performance: if the set changes, its
checksum also changes and if we store the checksum in the *descriptor*,
**its** descriptor set will have different checksum that needs
to be updated on **its** *descriptor*. On a modification a chain of
checksum updates is propagated. That's why we store the checksum
in the *set*'s *header*: to prevent the chain reaction.


# Internet checksum

`xoz` implements the same checksum than the one described in the RFC 1071 *"Internet Checksum"*.

The process consists in taking each 2-bytes word, seeing them as a 2-byte
unsigned integer (in little endian order) and sum them with 1's complement logic.

The result is a 16 bits checksum.

For 1's complement, there are 2 different ways to represent 0: either 16
bits of zeros or 16 bits of 1. When the checksum is written to disk
however, if the checksum is 16 bits of 1, it is mapped to its other
representation.

The *internet checksum* was chosen due its simplicity, and because it is very
easy to update a checksum if part of the payload changed without
requiring to compute the checksum for the whole.
It is not particularly strong, specially if it only has 16 bits. In
`xoz`, it is not designed with the intention of catch any possible
corruption but only a few cases. A complete and more robust corruption
detection is out of the scope of this RFC.

Reference: https://datatracker.ietf.org/doc/html/rfc1071
