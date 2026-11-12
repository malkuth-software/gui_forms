#pragma once

// Ribbon geometry and command-kind tests shared by the ribbon's sources
// (ribbon.cpp and ribbon_scaling.cpp). Private to the ribbon.

#include "gui_forms/controls/ribbon.hpp"

#include <cmath>
#include <cstddef>

namespace gui_forms::ribbon_detail {

// Ribbon geometry in logical units at text scale 1. The command plane holds
// three 22-unit rows of small commands (or one 66-unit large command) over
// a 17-unit caption row.
inline constexpr double tab_row_extent = 24.0;
inline constexpr double tab_padding = 12.0;
inline constexpr double tab_gap = 2.0;
inline constexpr double application_minimum_width = 56.0;
inline constexpr double plane_top_padding = 3.0;
inline constexpr double row_extent = 22.0;
inline constexpr std::size_t rows_per_column = 3U;
inline constexpr double caption_extent = 17.0;
inline constexpr double plane_bottom_padding = 2.0;
inline constexpr double group_padding = 4.0;
inline constexpr double column_gap = 2.0;
inline constexpr double divider_width = 1.0;
inline constexpr double caption_padding = 8.0;
inline constexpr double launcher_size = 13.0;
inline constexpr double launcher_gap = 4.0;
inline constexpr double launcher_glyph_inset = 3.0;
inline constexpr double large_icon = 32.0;
inline constexpr double small_icon = 16.0;
inline constexpr double large_minimum_width = 42.0;
inline constexpr double large_label_padding = 10.0;
inline constexpr double label_extent = 16.0;
inline constexpr double icon_gap = 3.0;
inline constexpr double small_label_padding = 6.0;
inline constexpr double arrow_zone = 12.0;
inline constexpr double arrow_glyph = 7.0;
inline constexpr double default_combo_box_width = 110.0;
inline constexpr double default_numeric_width = 64.0;
inline constexpr double default_text_box_width = 100.0;
inline constexpr double check_box_mark_width = 24.0;
// Where a hosted CheckBox's content starts: after its 15-unit well (30 for
// the toggle) and a unit either side (check_box.cpp, check_box_well).
inline constexpr double check_box_text_start = 17.0;
inline constexpr double check_box_toggle_text_start = 32.0;
inline constexpr double maximum_hosted_width = 1024.0;
// The gap between a labelled field's label and its field, when the label
// sits at the field's left.
inline constexpr double field_label_gap = 4.0;
// The gap between commands side by side in one row (beside_previous).
inline constexpr double row_item_gap = 2.0;
// The scroll buttons at the plane's ends while it scrolls (owner decision 35).
inline constexpr double scroll_button_width = 13.0;
// The group overlay's gap below the ribbon.
inline constexpr double overlay_gap = 1.0;

[[nodiscard]] inline bool hosted(RibbonCommandKind kind) noexcept {
    const bool result = kind == RibbonCommandKind::combo_box || kind == RibbonCommandKind::numeric_up_down ||
                        kind == RibbonCommandKind::check_box || kind == RibbonCommandKind::text_box ||
                        kind == RibbonCommandKind::gallery || kind == RibbonCommandKind::font_control ||
                        kind == RibbonCommandKind::color_picker;
    return result;
}

// Whether a kind holds a control the application made (and the ribbon only
// adds and places), rather than one the ribbon makes.
[[nodiscard]] inline bool authored_kind(RibbonCommandKind kind) noexcept {
    const bool result = kind == RibbonCommandKind::gallery || kind == RibbonCommandKind::font_control ||
                        kind == RibbonCommandKind::color_picker;
    return result;
}

// The application's control a command holds, or null.
[[nodiscard]] inline Control::Ptr authored_control(const RibbonCommandSpec& command) {
    if (command.kind == RibbonCommandKind::gallery) return command.gallery;
    if (command.kind == RibbonCommandKind::font_control) return command.font_control;
    if (command.kind == RibbonCommandKind::color_picker) return command.color_picker;
    return {};
}

// A text or number field, which shows its command's text as a label part
// beside or above the field, as Office's ribbon fields do.
[[nodiscard]] inline bool labelled_field_kind(RibbonCommandKind kind) noexcept {
    const bool result = kind == RibbonCommandKind::text_box || kind == RibbonCommandKind::numeric_up_down;
    return result;
}

// Whether a kind is plain text (a label): drawn, never hit, focused or run.
[[nodiscard]] inline bool text_only(RibbonCommandKind kind) noexcept {
    const bool result = kind == RibbonCommandKind::label;
    return result;
}

[[nodiscard]] inline bool has_menu(RibbonCommandKind kind) noexcept {
    const bool result = kind == RibbonCommandKind::split || kind == RibbonCommandKind::drop_down;
    return result;
}

[[nodiscard]] inline bool needs_command(RibbonCommandKind kind) noexcept {
    const bool result = kind == RibbonCommandKind::button || kind == RibbonCommandKind::toggle ||
                        kind == RibbonCommandKind::split;
    return result;
}

// A square glyph of `side` centred in `area`, on whole units.
[[nodiscard]] inline Rect centred_square(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect square{std::floor(area.x + (area.width - side) * 0.5),
                      std::floor(area.y + (area.height - side) * 0.5), side, side};
    return square;
}

} // namespace gui_forms::ribbon_detail
