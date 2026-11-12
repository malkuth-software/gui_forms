#pragma once

#include "gui_forms/binding/type_tag/type_tag.hpp"

#include <cstdint>

namespace gui_forms {

// Where a button's text and image sit and how they relate (Windows Forms'
// ContentAlignment and TextImageRelation), apart from ButtonBase so its
// property capabilities can name them.

enum class ContentAlignment : std::uint8_t {
    top_left,
    top_center,
    top_right,
    middle_left,
    middle_center,
    middle_right,
    bottom_left,
    bottom_center,
    bottom_right,
};

enum class TextImageRelation : std::uint8_t {
    overlay,
    image_before_text,
    text_before_image,
    image_above_text,
    text_above_image,
};

namespace types {

inline constexpr TypeTag<ContentAlignment> content_alignment{};
inline constexpr TypeTag<TextImageRelation> text_image_relation{};

} // namespace types

} // namespace gui_forms
