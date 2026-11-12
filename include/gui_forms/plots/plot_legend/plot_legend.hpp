#pragma once

// Legend and annotation layout (IMGUI_IMPLOT_STUDY B.3, P11). Pure
// geometry: the control measures the entries' text and paints the parts;
// these place them.
//
// - Legend: entries (swatch, gap, name) in a column or a row, placed at one
//   of nine positions inside the plot or outside it beside the plot.
// - Annotations and axis tags: labels anchored at data points (already
//   mapped to device), clamped inside the plot, laid out to avoid overlap
//   in one bounded pass: each label tries four placements around its anchor
//   in order and takes the first that overlaps no label placed before it
//   (else the first placement). At most maximum_annotations labels:
//   O(n²) with n bounded.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::plots {

enum class LegendPlace : std::uint8_t {
    top_left,
    top,
    top_right,
    left,
    center,
    right,
    bottom_left,
    bottom,
    bottom_right,
};

enum class LegendFlow : std::uint8_t { vertical, horizontal };

struct GUI_FORMS_API LegendStyle final {
    LegendPlace place{LegendPlace::top_right};
    // Inside the plot rectangle (true) or beside it (false: outside the
    // plot's side the place names; center inside always).
    bool inside{true};
    LegendFlow flow{LegendFlow::vertical};
    double padding{6.0};
    double swatch{12.0};
    double swatch_gap{6.0};
    double entry_gap{4.0};
    double line_height{16.0};
    // The legend's distance from the plot's edge.
    double margin{8.0};
};

// One entry's laid-out geometry, in the plot's coordinates.
struct GUI_FORMS_API LegendEntryLayout final {
    Rect bounds{};
    Rect swatch{};
    Rect label{};
};

struct GUI_FORMS_API LegendLayout final {
    Rect frame{};
    std::size_t entry_count{};
};

// Lays out entries whose label widths are `label_widths` into `entries`
// (at least as many) and returns the frame. No entries: an empty frame.
GUI_FORMS_API LegendLayout layout_legend(const LegendStyle& style, Rect plot, std::span<const double> label_widths,
                           std::span<LegendEntryLayout> entries) noexcept;

// The index of the entry at `point`, or entries.size() for none.
[[nodiscard]] GUI_FORMS_API std::size_t legend_entry_at(std::span<const LegendEntryLayout> entries, Point point) noexcept;

inline constexpr std::size_t maximum_annotations = 64U;

struct GUI_FORMS_API AnnotationRequest final {
    // The anchor in device (logical) coordinates.
    Point anchor{};
    Size size{};
};

// Places labels of `requests` (at most maximum_annotations; the excess is
// left unplaced) into `placed`, clamped inside `plot`; returns the count
// placed.
GUI_FORMS_API std::size_t layout_annotations(Rect plot, std::span<const AnnotationRequest> requests,
                               std::span<Rect> placed) noexcept;

} // namespace gui_forms::plots
