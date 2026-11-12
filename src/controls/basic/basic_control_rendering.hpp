#pragma once

#include "gui_forms/binding_types.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/picture_box.hpp"

#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Source-private helpers shared by the basic retained controls. Keeping these
// renderer-neutral operations together avoids duplicating text approximation,
// content alignment, and legacy relief geometry across per-control files.
std::shared_ptr<const PropertyEnumDescriptor> picture_box_size_mode_enum();
[[nodiscard]] double estimated_text_width(std::string_view text,
                                          FontSpec font) noexcept;
[[nodiscard]] bool valid_content_alignment(
    ContentAlignment alignment) noexcept;
[[nodiscard]] bool valid_text_image_relation(
    TextImageRelation relation) noexcept;
// `text` cut at a grapheme boundary and ended with an ellipsis so that it
// measures at most `maximum_width` with `painter`; the whole text when it
// fits, empty when not even the ellipsis does. Measures once per grapheme
// removed: for short display strings painted at most once a frame.
[[nodiscard]] std::string elide_text_end(Painter& painter, std::string_view text, FontSpec font,
                                         double maximum_width);

[[nodiscard]] Rect aligned_rect(Rect bounds, Size size,
                                ContentAlignment alignment) noexcept;
// Zero admits all lines. A positive limit returns the exact unlimited prefix
// and stops resolving widths once that many complete lines are available.
// The resolver is borrowed synchronously and is never retained. It supplies
// stable metrics for identical bytes during a call; short repeated candidates
// may reuse an exact previously resolved width within the paragraph traversal.
[[nodiscard]] std::vector<std::string> label_lines(
    std::string_view text, FontSpec font, double width, TextWrapping wrapping,
    std::size_t maximum_lines = 0U);
using TextWidthResolver = std::function<double(std::string_view)>;
[[nodiscard]] std::vector<std::string> label_lines(
    std::string_view text, FontSpec font, double width, TextWrapping wrapping,
    const TextWidthResolver& resolve_width, std::size_t maximum_lines = 0U);
// The default ring, then the focus ring, drawn from a recipe the program
// authored (ButtonBase::set_visual_recipes), which the theme's engine does
// not resolve.
void paint_theme_cues(Painter& painter, Rect bounds,
                      const ControlVisualRecipe& recipe,
                      ControlVisualContext context);
} // namespace gui_forms
