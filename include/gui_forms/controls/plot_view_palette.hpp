#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

// The colours PlotView draws with, resolved from a control's effective
// theme as PlotView resolves its own (the plot demonstrator's gap P5), so a
// custom plot kind follows set_theme_override like the stock plots. The
// rules come from the editor recipe's border (its top edge, else its text),
// the selection from its focus ring.
struct GUI_FORMS_API PlotPalette final {
    // The grid's major and minor lines (the rule colour at reduced alpha).
    Color grid_major{};
    Color grid_minor{};
    // The axis lines and ticks.
    Color axis{};
    // Tick labels, readouts and other plot text (the recipe's text).
    Color text{};
    // A zoom box or kept selection: its fill and its edge.
    Color selection_fill{};
    Color selection_edge{};

    friend bool operator==(const PlotPalette& left, const PlotPalette& right) noexcept {
        return left.grid_major == right.grid_major && left.grid_minor == right.grid_minor &&
               left.axis == right.axis && left.text == right.text &&
               left.selection_fill == right.selection_fill && left.selection_edge == right.selection_edge;
    }
};

// The palette for `owner` in its current anatomy states. Reads only.
[[nodiscard]] GUI_FORMS_API PlotPalette resolve_plot_palette(const Control& owner);
// The opaque colour PlotView gives the series at `position` among its
// series items (the series cycle through the theme's accents). Reads only.
[[nodiscard]] GUI_FORMS_API Color plot_series_color(const Control& owner, std::size_t position) noexcept;
// The theme accent for the category `key` (plots::category_key of a name,
// or the application's own identity; the flame graph demonstrator's F7):
// plot_series_color at plots::category_position(key, 256), so a key keeps
// its colour in a theme and follows a theme change. Reads only.
[[nodiscard]] GUI_FORMS_API Color plot_category_color(const Control& owner, std::uint64_t key) noexcept;

} // namespace gui_forms
