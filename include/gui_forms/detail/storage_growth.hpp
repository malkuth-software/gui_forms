#pragma once

// Geometric growth for containers that reserve before each insertion so a
// failed allocation changes nothing. reserve(size() + 1) gives exactly one
// more element, and std::vector then reallocates on every insertion: n
// insertions copy O(n^2) elements. These reserve room for one more element
// by at least doubling the capacity, so the reallocations are O(log n) and
// the strong guarantee is kept: after reserve_one_more returns, the next
// insertion does not allocate the container's storage.

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace gui_forms::detail {

// The smallest capacity reserve_one_more asks for.
inline constexpr std::size_t minimum_growth_capacity = 8U;

// The capacity that holds one element more than live: live + 1 at least,
// and double the current capacity when that is larger. Throws
// std::length_error when live + 1 overflows.
[[nodiscard]] inline std::size_t grown_capacity(std::size_t live, std::size_t capacity) {
    if (live == std::numeric_limits<std::size_t>::max()) {
        throw std::length_error("GUI.Forms storage cannot grow past the address space");
    }
    const std::size_t needed = live + 1U;
    std::size_t doubled = capacity;
    if (capacity <= std::numeric_limits<std::size_t>::max() / 2U) {
        doubled = capacity * 2U;
    }
    std::size_t grown = needed;
    if (doubled > grown) grown = doubled;
    if (minimum_growth_capacity > grown) grown = minimum_growth_capacity;
    return grown;
}

// Room in values for one element more than it holds, growing geometrically.
// No-op when the capacity already has room.
template <typename Element, typename Allocator>
void reserve_one_more(std::vector<Element, Allocator>& values) {
    if (values.size() < values.capacity()) {
        return;
    }
    const std::size_t capacity = grown_capacity(values.size(), values.capacity());
    values.reserve(capacity);
}

// Room in a vector for as many elements as another holds: used for a free
// list kept no shorter than the slots it can name, so its push_back never
// allocates. Grows geometrically, as reserve_one_more does.
template <typename Element, typename Allocator>
void reserve_at_least(std::vector<Element, Allocator>& values, std::size_t count) {
    if (count <= values.capacity()) {
        return;
    }
    std::size_t capacity = grown_capacity(values.capacity(), values.capacity());
    if (count > capacity) capacity = count;
    values.reserve(capacity);
}

// Buckets in map for one element more than it holds without a rehash at
// the next insertion, growing the bucket count geometrically.
template <typename Key, typename Value, typename Hash, typename Equal, typename Allocator>
void reserve_one_more(std::unordered_map<Key, Value, Hash, Equal, Allocator>& map) {
    const double limit = static_cast<double>(map.bucket_count()) * static_cast<double>(map.max_load_factor());
    if (static_cast<double>(map.size()) + 1.0 <= limit) {
        return;
    }
    const std::size_t elements = grown_capacity(map.size(), map.size());
    map.reserve(elements);
}

} // namespace gui_forms::detail
