# RFC: Specification of Segment, Extent and Block Size

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 1


 ```cpp
struct descriptor_t {
    uint16_t {
        uint own_edata : 1;    // mask: 0x8000
        uint lo_dsize  : 5;    // mask: 0x7c00
        uint has_id    : 1;    // mask: 0x0200
        uint type      : 9;    // mask: 0x01ff
    };

    /* present if has_id == 1 */
    uint32_t {
        uint hi_dsize : 1;      // mask: 0x80000000
        uint id       : 31;     // mask: 0x7fffffff
    };

    /* present if own_edata == 1 */
    uint16_t {
        uint large    : 1;      // mask: 0x8000
        uint lo_esize : 15;     // mask: 0x7fff
    };

    /* present if own_edata == 1 && large == 1 */
    uint16_t hi_esize;

    /* present if own_edata == 1 */
    struct segment_t segm;

    /* present if type == 0x1ff */
    uint16_t alt_type;

    /* to be interpreted based on descriptor/object type;
         dsize = lo_dsize if hi_dsize is not present
         dsize = hi_dsize << 5 | lo_dsize if hi_dsize is present */
    uint8 data[dsize * 2];
};
```

The size of the descriptor's `data` is given words of 2 bytes by the `lo_dsize` field
and if `hi_dsize` is present, `lo_dsize` is extended with `hi_dsize` as its MSB.
The maximum size of a descriptor without counting its header
is therefore 64 bytes (32 * 2) or 128 bytes (64 * 2).
How `data` is interpreted depends on the descriptor `type`.

`type` is the descriptor type; there can be up to 511 different types.
This RFC defines `0x00`: padding or end-of-stream descriptor

If `type` is 0x1ff, the field `alt_type` is present and it becomes the
type of the descriptor. Therefore there are two possible ways to encode
the first 511 types either with `type` or with `type = 0x1ff` and
`alt_type`.

## Descriptors that own external data

The `struct descriptor_t` can refer either to a descriptor
that *owns* blocks of data referenced by `segm` (`own_edata` is 1)
or to a descriptor that does not owns any segment (`own_edata` is 0).
The term *external* data is to distinguis it from the `data` field
embebbed in `struct descriptor_t`.

When a descriptor owns a segment it implies that if the descriptor
is deleted from the stream, the segment is freed and, if the descriptor
is not deleted, the segments must be allocated.

How much of the owned segment is in use (has meaningful external data) is given
by `lo_esize` (lower 15 bits) and if `large` is 1, by `hi_esize` (16
most significat bits). This gives an upper limit to how much external data the
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
size is large enough that the `hi_dsize` bit must be set that implies
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

For a *descriptor* that *does not own* a segment:

 - it has a minimum size of 2 bytes (when `has_id` and `lo_dsize` of 0)
 - it has a maximum size of 132 bytes (when `has_id` is 1 and the size of `data` is (64-1)*2)

For a *descriptor* that *owns* a segment:

 - it has a minimum size of 6 bytes (`has_id` is 0, size of `data` is 0
     and the `segm` has the minimum of 2 bytes, no space allocated)
 - there is no maximum size as `segm` is unbound; the descriptor may have up to 136 bytes
   if `segm` is not counted however.
 - the owned data can be up to 2GB bytes

In both cases, the descriptor size is always a multiple of 2;
the owned data is not required to be a multiple of 2.

## Descriptor size is fixed

Once a descriptor is present in the `xoz` file, its size cannot be
changed (aka `lo_dsize` and `hi_dsize` are fixed).

Growing the descriptor it is not possible because the descriptors
are packed tight in the stream and there is no room to grow.

Only if the application wants to store less data in the descriptor,
it may update the descriptor in place, pad with zeros the unused space
and adjust `lo_dsize` and `hi_dsize` accordingly.

In any case, if a resize is required, the `xoz` library will just write
a new descriptor with the same type and id than the former
to override it (this of course comes at expenses of wasted space).


## Descriptor Type 0: padding and end of stream

The `dtype` of 0 has a special meaning:

 - if `own_edata`, `has_id` and `lo_dsize` are 0, the descriptor
   works as 2 bytes padding (zeros).
 - if `own_edata` and `has_id` are 0 *but* `lo_dsize` is not, the
   descriptor works as *the end of the stream* marker.
 - under other settings, the meaning of the descriptor is reserved but
   the semantics of `lo_dsize` and `has_id` holds therefore it is
   known how many bytes the header and the descriptor occupy.

TODO: checksum





 ```cpp
struct dsc_position_t {
    uint16_t x;
    uint16_t y;
    uint16_t {
        uint has_more : 1;
        uint z        : 15;
    }

    /* if has_more == 1 */
    .. rotate / flip / mirror / skew
};
```
