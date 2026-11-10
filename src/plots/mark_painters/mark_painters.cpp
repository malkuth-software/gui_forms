#include "gui_forms/plots/mark_painters/mark_painters.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::plots {

void SeriesMarkPainter::reserve(std::size_t columns, std::size_t points) {
    columns_ = columns;
    // Stairs double the envelope; the undecimated kinds need `points`.
    const std::size_t envelope = envelope_capacity(columns);
    const std::size_t line_points = std::max(envelope * 2U, points);
    points_.assign(line_points, Point{});
    lower_points_.assign(std::max(envelope, points), Point{});
    // Error bars take three segments per element.
    segments_.assign(std::max(points * 3U, maximum_major_ticks + maximum_minor_ticks), PaintSegment{});
    rects_.assign(points, Rect{});
    colors_.assign(points, Color{});
    values_.assign(points, 0.0);
}

MarkStatistics SeriesMarkPainter::paint_infinite_lines(Painter& painter, std::span<const double> values,
                                                       InfiniteLineOrientation orientation,
                                                       const PlotTransform& transform, const StrokeStyle& stroke) {
    MarkStatistics statistics{};
    statistics.points_in = values.size();
    const Rect plot = transform.plot();
    std::size_t written = 0U;
    for (const double value : values) {
        const bool vertical = orientation == InfiniteLineOrientation::vertical;
        const double device = vertical ? transform.x().forward(value) : transform.y().forward(value);
        if (!std::isfinite(device)) continue;
        if (written == segments_.size()) {
            statistics.truncated = true;
            break;
        }
        const double snapped = transform.snap_line(device, stroke.width);
        segments_[written] = vertical ? PaintSegment{Point{snapped, plot.top()}, Point{snapped, plot.bottom()}}
                                      : PaintSegment{Point{plot.left(), snapped}, Point{plot.right(), snapped}};
        written = written + 1U;
    }
    statistics.points_drawn = written;
    if (written == 0U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.stroke_segments(std::span<const PaintSegment>(segments_.data(), written), stroke);
    return statistics;
}

MarkStatistics SeriesMarkPainter::paint_grid(Painter& painter, const TickSet& ticks, bool x_axis,
                                             const PlotTransform& transform, const StrokeStyle& major,
                                             const StrokeStyle& minor) {
    MarkStatistics statistics{};
    const Rect plot = transform.plot();
    statistics.points_in = ticks.major_count + ticks.minor_count;
    if (segments_.size() < maximum_minor_ticks) {
        statistics.truncated = true;
        return statistics;
    }
    const detail::PlotClip clip(painter, transform);
    // Minors first, so majors draw over them.
    std::size_t written = 0U;
    for (const double device : ticks.minors()) {
        const double snapped = transform.snap_line(device, minor.width);
        segments_[written] = x_axis ? PaintSegment{Point{snapped, plot.top()}, Point{snapped, plot.bottom()}}
                                    : PaintSegment{Point{plot.left(), snapped}, Point{plot.right(), snapped}};
        written = written + 1U;
    }
    if (written != 0U) painter.stroke_segments(std::span<const PaintSegment>(segments_.data(), written), minor);
    statistics.points_drawn = written;
    written = 0U;
    for (const MajorTick& tick : ticks.majors()) {
        const double snapped = transform.snap_line(tick.device, major.width);
        segments_[written] = x_axis ? PaintSegment{Point{snapped, plot.top()}, Point{snapped, plot.bottom()}}
                                    : PaintSegment{Point{plot.left(), snapped}, Point{plot.right(), snapped}};
        written = written + 1U;
    }
    if (written != 0U) painter.stroke_segments(std::span<const PaintSegment>(segments_.data(), written), major);
    statistics.points_drawn = statistics.points_drawn + written;
    return statistics;
}

MarkStatistics SeriesMarkPainter::paint_ticks(Painter& painter, const TickSet& ticks, AxisSide side,
                                              const PlotTransform& transform, double length,
                                              const StrokeStyle& stroke) {
    MarkStatistics statistics{};
    const Rect plot = transform.plot();
    statistics.points_in = ticks.major_count;
    if (segments_.size() < maximum_major_ticks) {
        statistics.truncated = true;
        return statistics;
    }
    std::size_t written = 0U;
    for (const MajorTick& tick : ticks.majors()) {
        const double snapped = transform.snap_line(tick.device, stroke.width);
        PaintSegment segment{};
        switch (side) {
        case AxisSide::bottom:
            segment = PaintSegment{Point{snapped, plot.bottom()}, Point{snapped, plot.bottom() + length}};
            break;
        case AxisSide::top:
            segment = PaintSegment{Point{snapped, plot.top() - length}, Point{snapped, plot.top()}};
            break;
        case AxisSide::left:
            segment = PaintSegment{Point{plot.left() - length, snapped}, Point{plot.left(), snapped}};
            break;
        case AxisSide::right:
            segment = PaintSegment{Point{plot.right(), snapped}, Point{plot.right() + length, snapped}};
            break;
        default:
            // An enumerator outside the declared set (a corrupt value) does nothing.
            break;
        }
        segments_[written] = segment;
        written = written + 1U;
    }
    statistics.points_drawn = written;
    if (written != 0U) painter.stroke_segments(std::span<const PaintSegment>(segments_.data(), written), stroke);
    return statistics;
}

MarkStatistics SeriesMarkPainter::paint_heatmap(Painter& painter, std::span<const double> values, std::size_t rows,
                                                std::size_t columns, PlotRange area_x, PlotRange area_y,
                                                const Colormap& map, const Scale& scale,
                                                const PlotTransform& transform) {
    MarkStatistics statistics{};
    if (rows == 0U || columns == 0U || values.size() / columns < rows) return statistics;
    const std::size_t cells = rows * columns;
    statistics.points_in = cells;
    if (cells > rects_.size()) {
        statistics.truncated = true;
        return statistics;
    }
    if (!sample_colormap_span(map, scale, values.subspan(0U, cells), std::span<Color>(colors_.data(), cells))) {
        return statistics;
    }
    const double cell_width = area_x.extent() / static_cast<double>(columns);
    const double cell_height = area_y.extent() / static_cast<double>(rows);
    for (std::size_t row = 0U; row < rows; ++row) {
        // Edges as integer multiples of the cell size, so neighbours share
        // their edges exactly.
        const double top = area_y.maximum - cell_height * static_cast<double>(row);
        const double bottom = area_y.maximum - cell_height * static_cast<double>(row + 1U);
        for (std::size_t column = 0U; column < columns; ++column) {
            const double left = area_x.minimum + cell_width * static_cast<double>(column);
            const double right = area_x.minimum + cell_width * static_cast<double>(column + 1U);
            const Point first = transform.to_device(left, top);
            const Point second = transform.to_device(right, bottom);
            rects_[row * columns + column] = detail::rect_between(first, second);
        }
    }
    statistics.points_drawn = cells;
    const detail::PlotClip clip(painter, transform);
    painter.fill_rects_each(std::span<const Rect>(rects_.data(), cells), std::span<const Color>(colors_.data(), cells));
    return statistics;
}

MarkStatistics SeriesMarkPainter::paint_histogram(Painter& painter, std::span<const std::uint64_t> counts,
                                                  const BinSpec& bins, const PlotTransform& transform, Color fill) {
    MarkStatistics statistics{};
    const std::size_t count = std::min(counts.size(), bins.count);
    statistics.points_in = count;
    if (count == 0U || !(bins.maximum > bins.minimum)) return statistics;
    std::size_t written = 0U;
    const double extent = bins.maximum - bins.minimum;
    for (std::size_t bin = 0U; bin < count; ++bin) {
        if (counts[bin] == 0U) continue;
        if (written == rects_.size()) {
            statistics.truncated = true;
            break;
        }
        const double left = bins.minimum + extent * static_cast<double>(bin) / static_cast<double>(bins.count);
        const double right = bins.minimum + extent * static_cast<double>(bin + 1U) / static_cast<double>(bins.count);
        const Point first = transform.to_device(left, 0.0);
        const Point second = transform.to_device(right, static_cast<double>(counts[bin]));
        if (!detail::finite_point(first) || !detail::finite_point(second)) continue;
        rects_[written] = detail::rect_between(first, second);
        written = written + 1U;
    }
    statistics.points_drawn = written;
    if (written == 0U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.fill_rects(std::span<const Rect>(rects_.data(), written), fill);
    return statistics;
}

} // namespace gui_forms::plots
