# RFC: Specification of Segment, Extent and Block Size

 - **Author:** Martin Di Paola
 - **Status:** Draft
 - **Version:** 1


 ```cpp
struct descriptor_t {
    uint16_t {
        uint is_obj    : 1;    // mask: 0x8000
        uint lo_dsize  : 5;    // mask: 0x7c00
        uint has_id    : 1;    // mask: 0x0200
        uint type      : 9;    // mask: 0x01ff
    };

    /* present if
        is_obj == 1 || (is_obj == 0 && has_id == 1) */
    uint32_t {
        uint hi_dsize : 1;      // mask: 0x80000000
        uint obj_id   : 31;     // mask: 0x7fffffff
    };

    /* present if is_obj == 1 */
    uint16_t {
        uint large    : 1;      // mask: 0x8000
        uint lo_osize : 15;     // mask: 0x7fff
    };

    /* present if is_obj == 1 && large == 1 */
    uint16_t hi_osize;

    /* present if is_obj == 1 */
    struct segment_t segm;

    /* to be interpreted based on descriptor/object type;
         dsize = lo_dsize if hi_dsize is not present
         dsize = hi_dsize << 5 | lo_dsize if hi_dsize is present */
    uint8 data[dsize * 2];
};
```

The `struct descriptor_t` can refer either to an *object descriptor*
(`is_obj` is 1) or to a *non-object descriptor* (`is_obj` is 0).
The former defines the existence
of an object while the latter just has a reference to an object
that it is defined somewhere else.

### Non-object descriptors

When `is_obj` is 0 (a *non-object descriptor*), the `obj_id` is
the object identifier that descriptor is referring to. It is set
explicit when `has_id` is 1; if it is 0, the descriptor refers
to same object that the previous descriptor in the *stream*.

`type` is the descriptor type; there can be up to 512 different types.
This RFC defines only a few.

The size of the descriptor's `data` is given words of 2 bytes by the `lo_dsize` field
and if `hi_dsize` is present, `lo_dsize` is extended with `hi_dsize` as its MSB.
The maximum size of a descriptor without counting its header
is therefore 64 bytes (32 * 2) or 128 bytes (64 * 2).
How `data` is interpreted depends on the descriptor `type`.

### Object descriptors

When `is_obj` is 1 (an *object descriptor*), the `obj_id` is
the object identifier that descriptor is defining and it is a mandatory
field.

The field `has_id` is then repurposed as the MSB of `type`: the object
type then is represented by `has_id` (MSB) and `type` lower bits giving
a maximum of 1024 different types.
This RFC defines only a few.

The size of the descriptor's `data` is given words of 2 bytes by
the `lo_dsize` and `hi_dsize` fields
and it has the same meaning than for the *non-object descriptors*.

An *object descriptor* defines an object with data outside of the
descriptor; potentially very large data, much larger than a descriptor
could hold (`lo_dsize` and `hi_dsize`).

`lo_osize` is the size in bytes of the object external data (the size of the
*descriptor* is not included). If the data is
larger than 32 kilobytes minus 1, the flag `large` is set to 1 and the size
is the combination of `lo_osize` (lower 15 bits) and `hi_osize` (higher
16 bits). Therefore, the maximum size is 2GB.

The `segm` is the segment the indicates where the object data is stored
and its length is not explicit in the header. Instead, the segment ends
when an inline-data extent is found so the segment must be *inline-data ended*.


# Descriptors invariants

For a *non-object descriptor*:

 - it has a minimum size of 2 bytes (`has_id` and `lo_dsize` of 0)
 - it has a maximum size of 70 bytes (`has_id` is 1 and `lo_dsize` is 32)
   with up to 64 bytes for usable data within the descriptor.

For an *object descriptor*:

 - it has a minimum size of 8 bytes for objects of zero bytes data
 - large data objects has a minimum descriptor size of 10 bytes.
 - the maximum size of the descriptor is of 138 bytes with up to
   128 bytes for usable data within the descriptor.
 - object data (external) can be up to 2GB bytes

In both cases, the descriptor size is always a multiple of 2;
object data (external) is not required to be a multiple of 2.


The `dtype` 0 is special:

 - if `lo_dsize` is 0 and `has_id` is unset, the descriptor works
   as 2 bytes padding (zeros).
 - otherwise, the meaning of the descriptor is reserved but
   the semantics of `lo_dsize` and `has_id` holds therefore it is
   known how many bytes the header and the descriptor occupy.


```cpp
struct obj_descr_t {
    struct descr_hdr_t hdr;  // hdr.is_obj == 1


};
```


