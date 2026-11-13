#pragma once

// The plot controls' enumerations and event payloads (IMGUI_IMPLOT_STUDY
// B.4): PlotView and its item children, PlotAxisItem, PlotSeriesItem and
// PlotAnnotationItem.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/plot_types/plot_types.hpp"
#include "gui_forms/events/drag_events/drag_events.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>

namespace gui_forms {

// What a series draws (the P1 kinds; the study's later kinds follow).
enum class PlotSeriesKind : std::uint8_t {
    line,     // a polyline, decimated to the plot's width
    stairs,   // steps holding each value until the next x
    scatter,  // a marker at each point
    shaded,   // the band between the series and zero
    bars,     // a bar from zero to each value
    stems,    // a stem from zero to each value, with a marker
};

// Which way an axis runs.
enum class PlotAxisDirection : std::uint8_t {
    horizontal,  // an x axis, below the plot (the first) or above it
    vertical,    // a y axis, left of the plot (the first) or right of it
};

// How an axis maps values (plots::ScaleKind, named for forms). Time axes
// are seconds since the Unix epoch, labelled in UTC (owner ruling 6a).
enum class PlotAxisScale : std::uint8_t { linear, log10, symlog, time, category };

// A point the pointer or the keyboard cursor picked (PlotView::point_picked).
struct GUI_FORMS_API PlotPointPick final {
    // The series' position among the plot's series items, and the element.
    std::size_t series{};
    std::size_t element{};
    double x{};
    double y{};
};

// The plot's visible ranges after a gesture or a programmatic change
// (PlotView::view_changed): the first x and y axes', as plain numbers so the
// C ABI carries them as fields.
struct GUI_FORMS_API PlotViewRanges final {
    double x_minimum{};
    double x_maximum{1.0};
    double y_minimum{};
    double y_maximum{1.0};
};

// A query rectangle (Ctrl + right drag, owner ruling 3a) in the first
// axes' data units (PlotView::selection_changed); `present` false when the
// selection was cleared.
struct GUI_FORMS_API PlotQuery final {
    bool present{};
    double x_minimum{};
    double x_maximum{};
    double y_minimum{};
    double y_maximum{};
};

// Draggable handles (PlotView::set_handles; the ImPlot-class
// demonstrator's P3): a point moved on both axes, or a line across the plot
// at an x (vertical) or a y (horizontal), moved across it.
enum class PlotHandleKind : std::uint8_t { point, vertical_line, horizontal_line };

// The most handles a PlotView holds.
inline constexpr std::size_t maximum_plot_handles = 256U;

// One handle, in the data coordinates of the plot's first axes: `x` and `y`
// for a point, `x` for a vertical line and `y` for a horizontal one (the
// other is kept and ignored). `id` is the program's name for it, unique in
// the plot. A disabled handle is drawn but not dragged.
struct GUI_FORMS_API PlotHandle final {
    std::uint32_t id{};
    PlotHandleKind kind{PlotHandleKind::point};
    double x{};
    double y{};
    bool enabled{true};

    friend bool operator==(const PlotHandle& left, const PlotHandle& right) noexcept {
        return left.id == right.id && left.kind == right.kind && left.x == right.x && left.y == right.y &&
               left.enabled == right.enabled;
    }
};

// A handle's position during or after a drag (PlotView::handle_moved and
// handle_committed): `end` is DragEnd::none while dragging, the commit's
// end (dropped or free) in handle_committed, and the end that restored the
// handle (escaped, capture_lost and the rest) in the last handle_moved,
// whose position is then the position before the drag.
struct GUI_FORMS_API PlotHandleMove final {
    std::uint32_t id{};
    double x{};
    double y{};
    DragEnd end{DragEnd::none};
};

} // namespace gui_forms
