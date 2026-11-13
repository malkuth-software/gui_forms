#pragma once

// ListView's value types, apart from the control so its capabilities header
// can name them.

#include <cstdint>

namespace gui_forms {

// How a ListView arranges its items (Windows Forms' View).
//
// - details: one row per item, with the columns' headers above and each
//   column's text (the item's text, then its sub-items) in the row;
// - list: small-icon rows filled top to bottom, then in further columns to
//   the right; the view scrolls sideways;
// - small_icon: small-icon cells filled left to right, then in further rows;
// - large_icon: large-icon cells with the text under the icon, filled left to
//   right, then in further rows;
// - tile: tiles of `tile_size` filled left to right, then in further rows,
//   each with the large icon at its left and, beside it, the item's text over
//   one line per column after the first holding a sub-item, as far as the
//   tile's height allows.
enum class ListViewView : std::uint8_t { details, list, small_icon, large_icon, tile };

} // namespace gui_forms
