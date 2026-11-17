#pragma once

#include "gui_forms/layout/types/layout_types.hpp"

#include <cstdint>

namespace gui_forms {

// The text placement and styling vocabulary of Label (and the text layout
// it shares), apart from the class so its capabilities can name it.

enum class HorizontalAlignment : std::uint8_t {
    start,
    center,
    end,
};

// The alignment a control resolving `direction` places. `start` and `end`
// are CSS's logical values (text-align, box alignment): under right to left
// `start` is the right edge and `end` the left (Control::
// effective_layout_direction), as Windows Forms mirrors a horizontal
// alignment under RightToLeft. A value outside the enumeration is kept.
[[nodiscard]] constexpr HorizontalAlignment directed_alignment(HorizontalAlignment alignment,
                                                               LayoutDirection direction) noexcept {
    if (direction != LayoutDirection::right_to_left) return alignment;
    if (alignment == HorizontalAlignment::start) return HorizontalAlignment::end;
    if (alignment == HorizontalAlignment::end) return HorizontalAlignment::start;
    return alignment;
}

enum class VerticalAlignment : std::uint8_t {
    start,
    center,
    end,
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
