#pragma once

#include <string_view>

namespace gui_forms::detail {

// The rules for parts that show content colours (design language 8.5,
// owner decision W-7: the colours a colour picker shows are data, drawn the
// same in every theme). The default theme compiles them after its light
// sheet and each familiar theme after its parts sheet, as one sheet (their
// diagnostics name the line within the joined text). Each part sets
// PartPaint::content_color, which the rules' currentcolor names: the
// field's pure hue, the alpha ramp's colour made opaque and the swatch's
// value. The checkerboard behind a translucent colour has 5-unit squares.
// The field and the hue strip are drawn by the picker itself, pixel by pixel
// from HSV (ColorPicker::paint_hsv_areas): a picker shows the exact colour
// under the pointer, which a gradient mixing in linear light would not. Their
// rules leave the background transparent, so the well's frame and edges draw
// over those pixels.
inline constexpr std::string_view content_parts_sheet = R"css(
/* ColorPicker's content (W-7). The saturation-brightness field and the hue
   spectrum are the picker's own pixels, drawn from HSV under the well. */
color-picker::part(field) {
  background: transparent;
}
color-picker::part(hue-track) {
  background: transparent;
}
/* The alpha ramp: the colour, opaque at the top, over the checkerboard. */
color-picker::part(alpha-track) {
  background: linear-gradient(to bottom, currentcolor, transparent), pattern(checker, #e8e8e8, #b2b2b2, 10px);
}
/* The swatch: the value, translucent or not, over the checkerboard. */
color-picker::part(swatch) {
  background: linear-gradient(currentcolor, currentcolor), pattern(checker, #e8e8e8, #b2b2b2, 10px);
}
)css";

} // namespace gui_forms::detail
