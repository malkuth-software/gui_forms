#pragma once

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

} // namespace gui_forms
