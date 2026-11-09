#pragma once

// The plot controls' enumerations and event payloads (IMGUI_IMPLOT_STUDY
// B.4): PlotView and its item children, PlotAxisItem, PlotSeriesItem and
// PlotAnnotationItem.

#include "gui_forms/plots/plot_types/plot_types.hpp"
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
struct PlotPointPick final {
    // The series' position among the plot's series items, and the element.
    std::size_t series{};
    std::size_t element{};
    double x{};
    double y{};
};

// The plot's visible ranges after a gesture or a programmatic change
// (PlotView::view_changed): the first x and y axes'.
struct PlotViewRanges final {
    plots::PlotRange x{};
    plots::PlotRange y{};
};

// A query rectangle (Ctrl + right drag, owner ruling 3a) in the first
// axes' data units (PlotView::selection_changed); `present` false when the
// selection was cleared.
struct PlotQuery final {
    bool present{};
    plots::PlotRange x{};
    plots::PlotRange y{};
};

} // namespace gui_forms
