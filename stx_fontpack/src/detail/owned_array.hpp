// stx_fontpack: a growable array of trivially copyable records whose storage
// comes from a caller's Allocator.
//
// The array owns data[0, capacity); data[0, length) is live. Every growth is
// checked for size overflow before it allocates, and a failed growth leaves
// the array as it was. The array never releases itself: its owner calls
// owned_array_release with the allocator that filled it.
#pragma once

#include "stx_fontpack/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace stx_fontpack {

template <typename Element>
struct OwnedArray {
    static_assert(std::is_trivially_copyable_v<Element>, "OwnedArray holds plain records only");
    Element* data;
    std::size_t length;
    std::size_t capacity;
};

template <typename Element>
void owned_array_reset(OwnedArray<Element>& array) noexcept {
    array.data = nullptr;
    array.length = 0;
    array.capacity = 0;
}

template <typename Element>
void owned_array_release(OwnedArray<Element>& array, const Allocator& allocator) noexcept {
    if (array.data != nullptr) {
        allocator.release(allocator.context, array.data, array.capacity * sizeof(Element));
    }
    owned_array_reset(array);
}

// Makes capacity at least wanted, keeping the live elements. False (and the
// array unchanged) when wanted * sizeof(Element) overflows or allocation fails.
template <typename Element>
[[nodiscard]] bool owned_array_reserve(OwnedArray<Element>& array, const Allocator& allocator, std::size_t wanted) noexcept {
    if (wanted <= array.capacity) {
        return true;
    }
    if (wanted > std::numeric_limits<std::size_t>::max() / sizeof(Element)) {
        return false;
    }
    const std::size_t bytes = wanted * sizeof(Element);
    Element* grown = static_cast<Element*>(allocator.allocate(allocator.context, bytes));
    if (grown == nullptr) {
        return false;
    }
    if (array.length != 0) {
        std::memcpy(grown, array.data, array.length * sizeof(Element));
    }
    if (array.data != nullptr) {
        allocator.release(allocator.context, array.data, array.capacity * sizeof(Element));
    }
    array.data = grown;
    array.capacity = wanted;
    return true;
}

// Sets the live length to length, growing to exactly length when needed;
// new elements are zero-filled.
template <typename Element>
[[nodiscard]] bool owned_array_resize_zeroed(OwnedArray<Element>& array, const Allocator& allocator,
                                             std::size_t length) noexcept {
    if (!owned_array_reserve(array, allocator, length == 0 ? 1 : length)) {
        return false;
    }
    if (length > array.length) {
        std::memset(static_cast<void*>(array.data + array.length), 0, (length - array.length) * sizeof(Element));
    }
    array.length = length;
    return true;
}

// Appends value, doubling the capacity when full (at least 16 elements).
template <typename Element>
[[nodiscard]] bool owned_array_append(OwnedArray<Element>& array, const Allocator& allocator, const Element& value) noexcept {
    if (array.length == array.capacity) {
        std::size_t wanted = 16;
        if (array.capacity >= 16) {
            if (array.capacity > std::numeric_limits<std::size_t>::max() / 2) {
                return false;
            }
            wanted = array.capacity * 2;
        }
        if (!owned_array_reserve(array, allocator, wanted)) {
            return false;
        }
    }
    array.data[array.length] = value;
    array.length += 1;
    return true;
}

}  // namespace stx_fontpack
