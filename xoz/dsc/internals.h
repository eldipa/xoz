#pragma once

#include <memory>

#define MASK_OWN_EDATA_FLAG uint16_t(0x8000)
#define MASK_HAS_ID_FLAG uint16_t(0x0200)

#define MASK_LO_DSIZE uint16_t(0x7c00)
#define MASK_TYPE uint16_t(0x01ff)

#define MASK_HI_DSIZE uint32_t(0x80000000)
#define MASK_ID uint32_t(0x7fffffff)

#define MASK_LARGE_FLAG uint16_t(0x8000)

#define MASK_LO_ESIZE uint16_t(0x7fff)
#define MASK_HI_ESIZE uint16_t(0xffff)

#define EXTENDED_TYPE_VAL_THRESHOLD uint16_t(0x1ff)

namespace xoz::dsc::internals {

template <typename map_iterator_t, typename descriptor_type = Descriptor>
class DescriptorIterator {
private:
    map_iterator_t it;

    using descriptor_ptr_t = std::shared_ptr<descriptor_type>;

    // To avoid creating an descriptor_ptr_t object every time that
    // the operators * and -> are called
    //
    // On each iterator movement (aka ++it) the cache becomes
    // invalid and is_cache_synced will be false until another
    // call to operator * and -> is made.
    mutable descriptor_ptr_t cached;
    mutable bool is_cache_synced;

public:
    // Public traits interface saying
    //
    // - which values the iterator
    // points to (descriptor_ptr_t, const descriptor_ptr_t& and const descriptor_ptr_t*);
    //
    // - which type can represent the difference between iterators
    // (the same that the original container's iterators use);
    //
    // - and in which category this iterator falls (Input Iterator).
    using value_type = descriptor_ptr_t;

    using reference = descriptor_ptr_t const&;
    using pointer = descriptor_ptr_t const*;

    using difference_type = typename map_iterator_t::difference_type;

    using iterator_category = std::input_iterator_tag;

    explicit DescriptorIterator(map_iterator_t const& it): it(it), cached(nullptr), is_cache_synced(false) {}

    DescriptorIterator& operator++() {
        ++it;
        is_cache_synced = false;
        return *this;
    }

    DescriptorIterator operator++(int) {
        DescriptorIterator cpy(*this);
        ++it;
        is_cache_synced = false;
        return cpy;
    }

    inline bool operator==(const DescriptorIterator& other) const { return it == other.it; }

    inline bool operator!=(const DescriptorIterator& other) const { return it != other.it; }

    inline const descriptor_ptr_t& operator*() const {
        update_current_extent();
        return cached;
    }

    inline const descriptor_ptr_t* operator->() const {
        update_current_extent();
        return &cached;
    }

private:
    inline void update_current_extent() const {
        if (not is_cache_synced) {
            cached = it->second;
            is_cache_synced = true;
        }
    }
};

}  // namespace xoz::dsc::internals
