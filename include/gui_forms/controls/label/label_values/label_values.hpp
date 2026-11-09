#pragma once

#include <cstdint>

namespace gui_forms {

// The text placement and styling vocabulary of Label (and the text layout
// it shares), apart from the class so its capabilities can name it.

enum class HorizontalAlignment : std::uint8_t {
    near,
    center,
    far,
};

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
