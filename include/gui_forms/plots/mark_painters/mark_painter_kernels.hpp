#pragma once

// SeriesMarkPainter's template members (mark_painters.hpp declares and
// documents them). Included at the end of mark_painters.hpp only.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gui_forms::plots {

namespace detail {

// Saves the painter and clips to the plot for one batch; restores when
// destroyed.
class PlotClip final {
public:
    PlotClip(Painter& painter, const PlotTransform& transform) : painter_(painter) {
        painter_.save();
        painter_.clip_rect(transform.clip());
    }
    ~PlotClip() { painter_.restore(); }
    PlotClip(const PlotClip&) = delete;
    PlotClip& operator=(const PlotClip&) = delete;

private:
    Painter& painter_;
};

// The elements of `source` the plot's x axis shows (all of an unordered
// source).
template <typename Source>
ElementRange plotted_range(const Source& source, XOrder order, const PlotTransform& transform) noexcept {
    if (order != XOrder::monotone) {
        const ElementRange all{0U, source.size()};
        return all;
    }
    const PlotRange domain = transform.x().scale().domain;
    const ElementRange range = visible_range(source, domain.minimum, domain.maximum);
    return range;
}

inline bool opaque_marker(const MarkerStyle& marker) noexcept {
    const bool fill_opaque = marker.fill.alpha == 255U || marker.fill.alpha == 0U;
    const bool edge_opaque = marker.edge.alpha == 255U || marker.edge.alpha == 0U || marker.edge_width == 0.0;
    return fill_opaque && edge_opaque;
}

inline Rect rect_between(Point first, Point second) noexcept {
    const double left = std::min(first.x, second.x);
    const double top = std::min(first.y, second.y);
    const Rect rect{left, top, std::fabs(second.x - first.x), std::fabs(second.y - first.y)};
    return rect;
}

} // namespace detail

template <typename Source>
MarkStatistics SeriesMarkPainter::paint_line(Painter& painter, const Source& source, XOrder order,
                                             const PlotTransform& transform, const StrokeStyle& stroke,
                                             LineDecimation decimation, const EnvelopePyramid* pyramid) {
    MarkStatistics statistics{};
    const ElementRange range = detail::plotted_range(source, order, transform);
    const std::span<Point> out(points_.data(), points_.size());
    DecimationStatistics decimated{};
    const bool monotone = order == XOrder::monotone;
    if (monotone && decimation == LineDecimation::pyramid && pyramid != nullptr && (*pyramid).size() == source.size()) {
        decimated = (*pyramid).envelope(source, range, transform, out);
    } else if (monotone && decimation != LineDecimation::none) {
        decimated = decimate_envelope(source, range, transform, out);
    } else {
        decimated = map_all(source, range, transform, out);
    }
    statistics.points_in = decimated.points_in;
    statistics.points_drawn = decimated.points_out;
    statistics.truncated = decimated.truncated;
    if (decimated.points_out < 2U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.stroke_polyline(std::span<const Point>(points_.data(), decimated.points_out), stroke);
    return statistics;
}

template <typename Source>
MarkStatistics SeriesMarkPainter::paint_stairs(Painter& painter, const Source& source, const PlotTransform& transform,
                                               const StrokeStyle& stroke, StairsMode mode) {
    MarkStatistics statistics{};
    const ElementRange range = detail::plotted_range(source, XOrder::monotone, transform);
    const std::span<Point> decimated_out(lower_points_.data(), lower_points_.size());
    const DecimationStatistics decimated = decimate_envelope(source, range, transform, decimated_out);
    statistics.points_in = decimated.points_in;
    statistics.truncated = decimated.truncated;
    // Each point after the first adds one step point before it.
    std::size_t written = 0U;
    for (std::size_t index = 0U; index < decimated.points_out; ++index) {
        const Point point = lower_points_[index];
        const bool step = index != 0U && std::isfinite(point.x) && std::isfinite(lower_points_[index - 1U].x);
        if (step) {
            const Point previous = lower_points_[index - 1U];
            const Point corner = mode == StairsMode::post ? Point{point.x, previous.y} : Point{previous.x, point.y};
            if (written == points_.size()) {
                statistics.truncated = true;
                break;
            }
            points_[written] = corner;
            written = written + 1U;
        }
        if (written == points_.size()) {
            statistics.truncated = true;
            break;
        }
        points_[written] = point;
        written = written + 1U;
    }
    statistics.points_drawn = written;
    if (written < 2U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.stroke_polyline(std::span<const Point>(points_.data(), written), stroke);
    return statistics;
}

template <typename Source>
MarkStatistics SeriesMarkPainter::paint_scatter(Painter& painter, const Source& source, const PlotTransform& transform,
                                                const MarkerStyle& marker, OccupancyGrid* grid) {
    MarkStatistics statistics{};
    const std::span<Point> out(points_.data(), points_.size());
    const Rect plot = transform.plot();
    const double pixels = std::ceil(plot.width * transform.device_scale()) *
                          std::ceil(plot.height * transform.device_scale());
    DecimationStatistics thinned{};
    const bool occupancy = grid != nullptr && detail::opaque_marker(marker) &&
                           static_cast<double>(source.size()) > pixels;
    if (occupancy) {
        thinned = (*grid).thin(source, transform, out);
    } else {
        const ElementRange all{0U, source.size()};
        thinned = map_all(source, all, transform, out);
    }
    statistics.points_in = thinned.points_in;
    statistics.points_drawn = thinned.points_out;
    statistics.truncated = thinned.truncated;
    if (thinned.points_out == 0U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.draw_markers(std::span<const Point>(points_.data(), thinned.points_out), marker);
    return statistics;
}

template <typename Upper, typename Lower>
MarkStatistics SeriesMarkPainter::paint_shaded(Painter& painter, const Upper& upper, const Lower& lower,
                                               const PlotTransform& transform, Color fill) {
    MarkStatistics statistics{};
    const std::size_t size = std::min(upper.size(), lower.size());
    const ElementRange range = detail::plotted_range(upper, XOrder::monotone, transform);
    const std::size_t end = std::min(range.end, size);
    statistics.points_in = end > range.first ? end - range.first : 0U;
    const detail::PlotClip clip(painter, transform);
    // Runs of finite pairs are filled one band each; a device column holding
    // several elements keeps its outermost upper and lower.
    std::size_t written = 0U;
    DeviceColumn open_column{};
    bool open = false;
    for (std::size_t index = range.first; index <= end; ++index) {
        bool finite = false;
        Point top{};
        Point bottom{};
        if (index < end) {
            top = transform.to_device(upper.x(index), upper.y(index));
            bottom = transform.to_device(upper.x(index), lower.y(index));
            finite = detail::finite_point(top) && detail::finite_point(bottom);
        }
        if (!finite) {
            if (written >= 2U) {
                painter.fill_band(std::span<const Point>(points_.data(), written),
                                  std::span<const Point>(lower_points_.data(), written), fill);
                statistics.points_drawn = statistics.points_drawn + written;
            }
            written = 0U;
            open = false;
            continue;
        }
        const DeviceColumn column = transform.column_of(top.x);
        if (open && column == open_column) {
            Point& kept_top = points_[written - 1U];
            Point& kept_bottom = lower_points_[written - 1U];
            // The band's outer edges in device y, whichever way y runs.
            const double highest = std::min({kept_top.y, kept_bottom.y, top.y, bottom.y});
            const double lowest = std::max({kept_top.y, kept_bottom.y, top.y, bottom.y});
            const bool upper_above = kept_top.y <= kept_bottom.y;
            kept_top.y = upper_above ? highest : lowest;
            kept_bottom.y = upper_above ? lowest : highest;
            continue;
        }
        if (written == points_.size() || written == lower_points_.size()) {
            statistics.truncated = true;
            continue;
        }
        points_[written] = top;
        lower_points_[written] = bottom;
        written = written + 1U;
        open = true;
        open_column = column;
    }
    return statistics;
}

template <typename Source>
MarkStatistics SeriesMarkPainter::paint_bars(Painter& painter, const Source& source, const PlotTransform& transform,
                                             double width, double baseline, bool horizontal, Color fill) {
    MarkStatistics statistics{};
    statistics.points_in = source.size();
    std::size_t written = 0U;
    const double half = width * 0.5;
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const double x = source.x(index);
        const double y = source.y(index);
        if (std::isnan(x) || std::isnan(y)) continue;
        const Point first = horizontal ? transform.to_device(baseline, x - half) : transform.to_device(x - half, baseline);
        const Point second = horizontal ? transform.to_device(y, x + half) : transform.to_device(x + half, y);
        if (!detail::finite_point(first) || !detail::finite_point(second)) continue;
        if (written == rects_.size()) {
            statistics.truncated = true;
            break;
        }
        rects_[written] = detail::rect_between(first, second);
        written = written + 1U;
    }
    statistics.points_drawn = written;
    if (written == 0U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.fill_rects(std::span<const Rect>(rects_.data(), written), fill);
    return statistics;
}

template <typename Source, typename Errors>
MarkStatistics SeriesMarkPainter::paint_error_bars(Painter& painter, const Source& source, const Errors& low,
                                                   const Errors& high, const PlotTransform& transform, double cap,
                                                   const StrokeStyle& stroke) {
    MarkStatistics statistics{};
    const std::size_t size = std::min({source.size(), low.size(), high.size()});
    statistics.points_in = size;
    std::size_t written = 0U;
    const std::size_t per_bar = cap > 0.0 ? 3U : 1U;
    for (std::size_t index = 0U; index < size; ++index) {
        const double x = source.x(index);
        const double y = source.y(index);
        const Point bottom = transform.to_device(x, y - low.y(index));
        const Point top = transform.to_device(x, y + high.y(index));
        if (!detail::finite_point(bottom) || !detail::finite_point(top)) continue;
        if (segments_.size() - written < per_bar) {
            statistics.truncated = true;
            break;
        }
        segments_[written] = PaintSegment{bottom, top};
        written = written + 1U;
        if (per_bar == 1U) continue;
        const double half = cap * 0.5;
        segments_[written] = PaintSegment{Point{bottom.x - half, bottom.y}, Point{bottom.x + half, bottom.y}};
        segments_[written + 1U] = PaintSegment{Point{top.x - half, top.y}, Point{top.x + half, top.y}};
        written = written + 2U;
    }
    statistics.points_drawn = written;
    if (written == 0U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.stroke_segments(std::span<const PaintSegment>(segments_.data(), written), stroke);
    return statistics;
}

template <typename Source>
MarkStatistics SeriesMarkPainter::paint_stems(Painter& painter, const Source& source, const PlotTransform& transform,
                                              double baseline, const StrokeStyle& stroke, const MarkerStyle& marker) {
    MarkStatistics statistics{};
    statistics.points_in = source.size();
    std::size_t written = 0U;
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const double x = source.x(index);
        const Point tip = transform.to_device(x, source.y(index));
        const Point foot = transform.to_device(x, baseline);
        if (!detail::finite_point(tip) || !detail::finite_point(foot)) continue;
        if (written == segments_.size() || written == points_.size()) {
            statistics.truncated = true;
            break;
        }
        segments_[written] = PaintSegment{foot, tip};
        points_[written] = tip;
        written = written + 1U;
    }
    statistics.points_drawn = written;
    if (written == 0U) return statistics;
    const detail::PlotClip clip(painter, transform);
    painter.stroke_segments(std::span<const PaintSegment>(segments_.data(), written), stroke);
    painter.draw_markers(std::span<const Point>(points_.data(), written), marker);
    return statistics;
}

} // namespace gui_forms::plots
