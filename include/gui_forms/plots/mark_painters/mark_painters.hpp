#pragma once

// The plot kinds' painters (IMGUI_IMPLOT_STUDY B.2 and B.3, P7), on the
// Painter's batched calls: each kind is one batch call per series per
// frame (stroke_polyline, fill_band, fill_rects, fill_rects_each,
// stroke_segments, draw_markers), recorded by the graphics recorder as one
// display command, so display traces see them.
//
// A SeriesMarkPainter owns the scratch buffers the kinds map into, reserved
// once at configuration (reserve) for a plot's device width and the
// largest series it draws without decimation; painting never allocates.
// What does not fit is left out and counted (MarkStatistics::truncated).
//
// | Kind              | Decimation                     | Call            |
// |-------------------|--------------------------------|-----------------|
// | line              | envelope (monotone) or pyramid | stroke_polyline |
// | stairs            | envelope, then stepped         | stroke_polyline |
// | scatter           | occupancy past the plot's      | draw_markers    |
// |                   | pixel count (opaque markers)   |                 |
// | shaded            | per-column extremes            | fill_band       |
// | bars, histogram   | none (bounded by the data)     | fill_rects      |
// | error bars, stems | none                           | stroke_segments |
// | inf lines, grid   | none                           | stroke_segments |
// | heatmap           | none (cells)                   | fill_rects_each |

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/plots/colormap/colormap.hpp"
#include "gui_forms/plots/decimation/decimation.hpp"
#include "gui_forms/plots/plot_transform/plot_transform.hpp"
#include "gui_forms/plots/tick_generator/tick_generator.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms::plots {

enum class LineDecimation : std::uint8_t {
    // The min-max envelope for monotone x, every point otherwise.
    envelope,
    // The pyramid's envelope (the caller passes a built pyramid).
    pyramid,
    // Every visible point.
    none,
};

enum class StairsMode : std::uint8_t {
    // Each value holds until the next x (ImPlot's default).
    post,
    // Each value holds from the previous x.
    pre,
};

// What one painter call drew.
struct GUI_FORMS_API MarkStatistics final {
    std::size_t points_in{};
    std::size_t points_drawn{};
    bool truncated{};
};

enum class InfiniteLineOrientation : std::uint8_t { vertical, horizontal };

class GUI_FORMS_API SeriesMarkPainter final {
public:
    // Reserves for a plot `columns` device columns wide and series of up to
    // `points` elements drawn without decimation (scatter, bars, error bars,
    // unordered lines). Allocates.
    void reserve(std::size_t columns, std::size_t points);
    [[nodiscard]] std::size_t point_capacity() const noexcept { return points_.size(); }

    // A line through `source`'s elements visible on `transform`'s x axis.
    template <typename Source>
    MarkStatistics paint_line(Painter& painter, const Source& source, XOrder order, const PlotTransform& transform,
                              const StrokeStyle& stroke, LineDecimation decimation,
                              const EnvelopePyramid* pyramid);
    // Stairs: the decimated line with a step inserted between consecutive
    // points.
    template <typename Source>
    MarkStatistics paint_stairs(Painter& painter, const Source& source, const PlotTransform& transform,
                                const StrokeStyle& stroke, StairsMode mode);
    // Markers at each element; past one element per device pixel of the
    // plot and with opaque markers, one per occupied pixel (`grid` reserved
    // at the plot's device size).
    template <typename Source>
    MarkStatistics paint_scatter(Painter& painter, const Source& source, const PlotTransform& transform,
                                 const MarkerStyle& marker, OccupancyGrid* grid);
    // The band between `upper` and `lower` (same x samples; monotone x),
    // decimated to each device column's highest upper and lowest lower.
    template <typename Upper, typename Lower>
    MarkStatistics paint_shaded(Painter& painter, const Upper& upper, const Lower& lower,
                                const PlotTransform& transform, Color fill);
    // Bars from `baseline` to each element's y, `width` data units wide
    // centred on its x (horizontal bars swap the roles of x and y).
    template <typename Source>
    MarkStatistics paint_bars(Painter& painter, const Source& source, const PlotTransform& transform, double width,
                              double baseline, bool horizontal, Color fill);
    // Vertical error bars at each element: from y - low to y + high, with
    // caps `cap` logical units wide (0: no caps). `low` and `high` are
    // sources whose y is the error (their x is ignored).
    template <typename Source, typename Errors>
    MarkStatistics paint_error_bars(Painter& painter, const Source& source, const Errors& low, const Errors& high,
                                    const PlotTransform& transform, double cap, const StrokeStyle& stroke);
    // Stems from `baseline` to each element, then a marker at each tip.
    template <typename Source>
    MarkStatistics paint_stems(Painter& painter, const Source& source, const PlotTransform& transform,
                               double baseline, const StrokeStyle& stroke, const MarkerStyle& marker);
    // A line across the whole plot at each value.
    MarkStatistics paint_infinite_lines(Painter& painter, std::span<const double> values,
                                        InfiniteLineOrientation orientation, const PlotTransform& transform,
                                        const StrokeStyle& stroke);
    // Grid lines at the ticks of an axis (vertical lines for an x axis),
    // majors and minors in two calls.
    MarkStatistics paint_grid(Painter& painter, const TickSet& ticks, bool x_axis, const PlotTransform& transform,
                              const StrokeStyle& major, const StrokeStyle& minor);
    // Tick marks of `length` logical units outward from the plot's edge on
    // `side`.
    MarkStatistics paint_ticks(Painter& painter, const TickSet& ticks, AxisSide side, const PlotTransform& transform,
                               double length, const StrokeStyle& stroke);
    // A heatmap: `values` row-major (`rows` × `columns`, row 0 at the top
    // of `area`, a data rectangle) coloured through `map` over `scale`'s
    // domain.
    MarkStatistics paint_heatmap(Painter& painter, std::span<const double> values, std::size_t rows,
                                 std::size_t columns, PlotRange area_x, PlotRange area_y, const Colormap& map,
                                 const Scale& scale, const PlotTransform& transform);
    // Histogram bars from bin counts over `bins`.
    MarkStatistics paint_histogram(Painter& painter, std::span<const std::uint64_t> counts, const BinSpec& bins,
                                   const PlotTransform& transform, Color fill);

private:
    std::vector<Point> points_{};
    std::vector<Point> lower_points_{};
    std::vector<PaintSegment> segments_{};
    std::vector<Rect> rects_{};
    std::vector<Color> colors_{};
    std::vector<double> values_{};
    std::size_t columns_{};
};

} // namespace gui_forms::plots

#include "gui_forms/plots/mark_painters/mark_painter_kernels.hpp"
