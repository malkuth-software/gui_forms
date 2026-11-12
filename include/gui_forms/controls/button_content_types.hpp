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

// `alignment` with left and right exchanged: how Windows Forms places a
// ContentAlignment in a control under RightToLeft. A value outside the
// enumeration is kept.
[[nodiscard]] constexpr ContentAlignment mirrored_content_alignment(ContentAlignment alignment) noexcept {
    switch (alignment) {
    case ContentAlignment::top_left: return ContentAlignment::top_right;
    case ContentAlignment::top_right: return ContentAlignment::top_left;
    case ContentAlignment::middle_left: return ContentAlignment::middle_right;
    case ContentAlignment::middle_right: return ContentAlignment::middle_left;
    case ContentAlignment::bottom_left: return ContentAlignment::bottom_right;
    case ContentAlignment::bottom_right: return ContentAlignment::bottom_left;
    case ContentAlignment::top_center: return alignment;
    case ContentAlignment::middle_center: return alignment;
    case ContentAlignment::bottom_center: return alignment;
    }
    // A value outside the enumeration is kept.
    return alignment;
}

// `relation` with before and after exchanged along the line: image before
// text puts the image at the right under RightToLeft. The vertical
// relations and overlay are kept.
[[nodiscard]] constexpr TextImageRelation mirrored_text_image_relation(TextImageRelation relation) noexcept {
    switch (relation) {
    case TextImageRelation::image_before_text: return TextImageRelation::text_before_image;
    case TextImageRelation::text_before_image: return TextImageRelation::image_before_text;
    case TextImageRelation::overlay: return relation;
    case TextImageRelation::image_above_text: return relation;
    case TextImageRelation::text_above_image: return relation;
    }
    // A value outside the enumeration is kept.
    return relation;
}

namespace types {

inline constexpr TypeTag<ContentAlignment> content_alignment{};
inline constexpr TypeTag<TextImageRelation> text_image_relation{};

} // namespace types

} // namespace gui_forms
