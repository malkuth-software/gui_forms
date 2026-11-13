#pragma once
// The embedded stock icon records (stock_icon_data.inc, generated at build
// time by tools/stock_icon_codec.py from the Heroicons v2.2.0 SVG files in
// assets/icons/heroicons/; the format is described there). The build puts
// it in <build>/generated/stock_icons/, which gui_forms_core has on its
// private include path. Included by stock_icons.cpp only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms::detail {

// One icon file: its name's index in names, its style (0 outline, 1 solid),
// its grid (16, 20 or 24) and its run of layers.
struct StockIconVariantRecord final {
    std::uint16_t name;
    std::uint8_t style;
    std::uint8_t grid;
    std::uint32_t first_layer;
    std::uint16_t layer_count;
};

// One path element: its run of verbs (PaintPathVerb values), the index of
// its first coordinate (x, y pairs in thousandths of a grid unit), its paint
// (StockIconPaint) and, for a stroke, its width in thousandths and its cap
// (StrokeCap).
struct StockIconLayerRecord final {
    std::uint32_t first_verb;
    std::uint32_t verb_count;
    std::uint32_t first_coordinate;
    std::uint8_t paint;
    std::uint16_t stroke_width;
    std::uint8_t cap;
};

} // namespace gui_forms::detail

#include "stock_icon_data.inc"
