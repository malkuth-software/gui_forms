#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <span>

namespace gui_forms::detail {

struct GUI_FORMS_API AscendingLess final {
    template <typename Left, typename Right>
    [[nodiscard]] constexpr bool operator()(const Left& left,
                                            const Right& right) const {
        return left < right;
    }
};

template <typename Element, std::size_t Extent, typename Value, typename Less>
[[nodiscard]] constexpr std::size_t lower_bound_index(
    std::span<const Element, Extent> sorted,
    const Value& value,
    const Less& less) {
    std::size_t first = 0U;
    std::size_t count = sorted.size();
    while (count != 0U) {
        const std::size_t step = count / 2U;
        const std::size_t middle = first + step;
        if (less(sorted[middle], value)) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

template <typename Element, std::size_t Extent, typename Value>
[[nodiscard]] constexpr std::size_t lower_bound_index(
    std::span<const Element, Extent> sorted,
    const Value& value) {
    return lower_bound_index(sorted, value, AscendingLess{});
}

template <typename Element, std::size_t Extent, typename Value, typename Less>
[[nodiscard]] constexpr std::size_t upper_bound_index(
    std::span<const Element, Extent> sorted,
    const Value& value,
    const Less& less) {
    std::size_t first = 0U;
    std::size_t count = sorted.size();
    while (count != 0U) {
        const std::size_t step = count / 2U;
        const std::size_t middle = first + step;
        if (!less(value, sorted[middle])) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    return first;
}

template <typename Element, std::size_t Extent, typename Value>
[[nodiscard]] constexpr std::size_t upper_bound_index(
    std::span<const Element, Extent> sorted,
    const Value& value) {
    return upper_bound_index(sorted, value, AscendingLess{});
}

template <typename Element, std::size_t Extent, typename Value, typename Less>
[[nodiscard]] constexpr bool binary_search_contains(
    std::span<const Element, Extent> sorted,
    const Value& value,
    const Less& less) {
    const std::size_t position = lower_bound_index(sorted, value, less);
    if (position == sorted.size()) {
        return false;
    }
    return !less(sorted[position], value) && !less(value, sorted[position]);
}

template <typename Element, std::size_t Extent, typename Value>
[[nodiscard]] constexpr bool binary_search_contains(
    std::span<const Element, Extent> sorted,
    const Value& value) {
    return binary_search_contains(sorted, value, AscendingLess{});
}

} // namespace gui_forms::detail
