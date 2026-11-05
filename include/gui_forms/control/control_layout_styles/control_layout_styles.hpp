#pragma once

#include "gui_forms/binding/type_tag/type_tag.hpp"

#include <cstdint>

namespace gui_forms {

// Control's layout participation values (Windows Forms' DockStyle,
// AnchorStyles and AutoSizeMode, with their exact flag values), apart from
// Control so its property capabilities can name them.

enum class DockStyle : std::uint8_t {
    none,
    top,
    bottom,
    left,
    right,
    fill,
};

enum class AnchorStyles : std::uint8_t {
    none = 0U,
    top = 1U << 0U,
    bottom = 1U << 1U,
    left = 1U << 2U,
    right = 1U << 3U,
};

// How a child sits inside the region its parent's layout gives it, on one
// axis (CSS justify-self and align-self over start, center, end and
// stretch). `automatic` takes the parent's own default for its items; a
// layout that places children by other rules (dock, anchor, flow) ignores
// the value.
enum class SelfAlignment : std::uint8_t {
    automatic,
    start,
    center,
    end,
    stretch,
};

enum class AutoSizeMode : std::uint8_t {
    grow_and_shrink = 0U,
    grow_only = 1U,
};

[[nodiscard]] constexpr AnchorStyles operator|(AnchorStyles left,
                                                AnchorStyles right) noexcept {
    return static_cast<AnchorStyles>(static_cast<std::uint8_t>(left) |
                                     static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr AnchorStyles operator&(AnchorStyles left,
                                                AnchorStyles right) noexcept {
    return static_cast<AnchorStyles>(static_cast<std::uint8_t>(left) &
                                     static_cast<std::uint8_t>(right));
}

constexpr AnchorStyles& operator|=(AnchorStyles& left,
                                   AnchorStyles right) noexcept {
    left = left | right;
    return left;
}

[[nodiscard]] constexpr bool has_anchor(AnchorStyles value,
                                        AnchorStyles flag) noexcept {
    return (value & flag) != AnchorStyles::none;
}

namespace types {

inline constexpr TypeTag<DockStyle> dock_style{};
inline constexpr TypeTag<AnchorStyles> anchor_styles{};
inline constexpr TypeTag<AutoSizeMode> auto_size_mode{};
inline constexpr TypeTag<SelfAlignment> self_alignment{};

} // namespace types

} // namespace gui_forms
