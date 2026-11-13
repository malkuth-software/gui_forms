#pragma once

#include "gui_forms/layout/types/layout_types.hpp"

#include <cstdint>

namespace gui_forms {

// The text placement and styling vocabulary of Label (and the text layout
// it shares), apart from the class so its capabilities can name it.

enum class HorizontalAlignment : std::uint8_t {
    near,
    center,
    far,
};

// The alignment a control resolving `direction` places: Windows Forms
// mirrors a horizontal alignment under RightToLeft, so `near` is the right
// edge and `far` the left in right to left (Control::
// effective_layout_direction). A value outside the enumeration is kept.
[[nodiscard]] constexpr HorizontalAlignment directed_alignment(HorizontalAlignment alignment,
                                                               LayoutDirection direction) noexcept {
    if (direction != LayoutDirection::right_to_left) return alignment;
    if (alignment == HorizontalAlignment::near) return HorizontalAlignment::far;
    if (alignment == HorizontalAlignment::far) return HorizontalAlignment::near;
    return alignment;
}

enum class VerticalAlignment : std::uint8_t {
    near,
    center,
    far,
};

enum class TextWrapping : std::uint8_t {
    no_wrap,
    word,
};

enum class TextStyleRole : std::uint8_t {
    body,
    control,
    caption,
    heading,
    title,
    monospace,
};

enum class TextCaseTransform : std::uint8_t {
    none,
    uppercase_ascii,
    lowercase_ascii,
};

} // namespace gui_forms
