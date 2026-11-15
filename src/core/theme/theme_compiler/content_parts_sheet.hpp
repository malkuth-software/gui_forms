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
inline constexpr std::string_view content_parts_sheet = R"css(
/* ColorPicker's content (W-7). The saturation-brightness field: white to
   the hue across, then transparent to black down. */
color-picker::part(field) {
  background: linear-gradient(to bottom, transparent, black), linear-gradient(to right, white, currentcolor);
}
/* The hue spectrum: 0 degrees at the top, 360 at the bottom. */
color-picker::part(hue-track) {
  background: linear-gradient(to bottom, #ff0000, #ffff00, #00ff00, #00ffff, #0000ff, #ff00ff, #ff0000);
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
