#pragma once

#include <cstdint>
#include <type_traits>

namespace gui_forms {

// The axes of a container's own size along which its children's layout
// changes (the owner's per-axis resize model, 2026-11-03): a resize of the
// container along an axis it does not respond to re-lays out none of its
// children. Containers declare it (Control::layout_resize_response);
// children never do. A set of flags: `both` is horizontal and vertical.
enum class ResizeAxes : std::uint8_t {
    none = 0,
    horizontal = 1U << 0U,
    vertical = 1U << 1U,
    both = (1U << 0U) | (1U << 1U),
};

[[nodiscard]] constexpr ResizeAxes operator|(ResizeAxes left, ResizeAxes right) noexcept {
    using Value = std::underlying_type_t<ResizeAxes>;
    return static_cast<ResizeAxes>(static_cast<Value>(left) | static_cast<Value>(right));
}

[[nodiscard]] constexpr ResizeAxes operator&(ResizeAxes left, ResizeAxes right) noexcept {
    using Value = std::underlying_type_t<ResizeAxes>;
    return static_cast<ResizeAxes>(static_cast<Value>(left) & static_cast<Value>(right));
}

constexpr ResizeAxes& operator|=(ResizeAxes& left, ResizeAxes right) noexcept {
    left = left | right;
    return left;
}

// Whether `axes` holds every axis of `wanted`.
[[nodiscard]] constexpr bool has_axes(ResizeAxes axes, ResizeAxes wanted) noexcept {
    return (axes & wanted) == wanted;
}

// Whether `axes` holds any axis of `wanted`.
[[nodiscard]] constexpr bool shares_axis(ResizeAxes axes, ResizeAxes wanted) noexcept {
    return (axes & wanted) != ResizeAxes::none;
}

} // namespace gui_forms
