// Path clip coverage masks (clip_mask.hpp): a scanline rasterizer over
// 1/256-pixel fixed point with exact horizontal coverage and sampled rows,
// and the intersection of two masks. Integer arithmetic throughout once the
// vertices are rounded.
#include "clip_mask.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::detail {
namespace {

// One device pixel in fixed point.
constexpr std::int64_t unit = 256;
// The same unit as a mask entry's full coverage.
constexpr std::uint32_t full_coverage = 256U;

bool point_in_range(const Point point) noexcept {
    const bool in_range = std::abs(point.x) <= clip_mask_coordinate_limit &&
                          std::abs(point.y) <= clip_mask_coordinate_limit;
    return in_range;
}

bool finite_point(const Point point) noexcept {
    const bool finite = std::isfinite(point.x) && std::isfinite(point.y);
    return finite;
}

std::int64_t fixed(const double value) noexcept {
    const std::int64_t rounded = std::llround(value * static_cast<double>(unit));
    return rounded;
}

// Floor of numerator / denominator for a positive denominator.
std::int64_t floor_quotient(const std::int64_t numerator, const std::int64_t denominator) noexcept {
    std::int64_t quotient = numerator / denominator;
    if (numerator % denominator != 0 && numerator < 0) --quotient;
    return quotient;
}

bool edge_starts_above(const ClipMaskEdge& first, const ClipMaskEdge& second) noexcept {
    return first.top_y < second.top_y;
}

bool crossing_before(const ClipMaskCrossing& first, const ClipMaskCrossing& second) noexcept {
    if (first.x != second.x) return first.x < second.x;
    return first.winding < second.winding;
}

// The edge from `from` to `to`, appended unless it is horizontal in fixed
// point (it crosses no sample row).
void append_edge(std::vector<ClipMaskEdge>& edges, const Point from, const Point to) {
    const std::int64_t from_x = fixed(from.x);
    const std::int64_t from_y = fixed(from.y);
    const std::int64_t to_x = fixed(to.x);
    const std::int64_t to_y = fixed(to.y);
    if (from_y == to_y) return;
    ClipMaskEdge edge{};
    if (from_y < to_y) {
        edge = ClipMaskEdge{from_x, from_y, to_x, to_y, 1};
    } else {
        edge = ClipMaskEdge{to_x, to_y, from_x, from_y, -1};
    }
    edges.push_back(edge);
}

// Adds the span [start, end) of one sample row, in fixed point relative to
// the mask's left edge and already held to [0, width * unit], to the row's
// accumulators.
void accumulate_span(ClipMaskWorkspace& workspace, const std::int64_t start, const std::int64_t end) noexcept {
    const std::int64_t first_pixel = start / unit;
    const std::int64_t last_pixel = end / unit;
    const std::size_t first = static_cast<std::size_t>(first_pixel);
    const std::size_t last = static_cast<std::size_t>(last_pixel);
    if (first == last) {
        workspace.partial[first] += static_cast<std::int32_t>(end - start);
        return;
    }
    workspace.partial[first] += static_cast<std::int32_t>(unit - (start - first_pixel * unit));
    workspace.whole_changes[first + 1U] += 1;
    workspace.whole_changes[last] -= 1;
    workspace.partial[last] += static_cast<std::int32_t>(end - last_pixel * unit);
}

// The checks before anything is written (rasterize_clip_mask).
ClipMaskStatus validate_mask_input(const std::span<const Point> points, const std::span<const FlattenedContour> contours,
                                   const PolygonFillRule rule, const ClipMaskBounds bounds,
                                   const std::span<std::uint16_t> coverage) noexcept {
    const bool known_rule = rule == PolygonFillRule::nonzero || rule == PolygonFillRule::even_odd;
    if (!known_rule || bounds.width > maximum_clip_mask_extent || bounds.height > maximum_clip_mask_extent) {
        return ClipMaskStatus::invalid_geometry;
    }
    // Both extents are at most 2^15, so the product fits.
    const std::size_t area = static_cast<std::size_t>(bounds.width) * static_cast<std::size_t>(bounds.height);
    if (area > coverage.size()) return ClipMaskStatus::invalid_geometry;
    const double limit = clip_mask_coordinate_limit;
    const double far_left = static_cast<double>(bounds.left);
    const double far_top = static_cast<double>(bounds.top);
    if (std::abs(far_left) > limit || std::abs(far_top) > limit ||
        std::abs(far_left + static_cast<double>(bounds.width)) > limit ||
        std::abs(far_top + static_cast<double>(bounds.height)) > limit) {
        return ClipMaskStatus::out_of_range;
    }
    std::size_t start = 0U;
    for (const FlattenedContour& contour : contours) {
        if (contour.end < start || contour.end > points.size()) return ClipMaskStatus::invalid_geometry;
        start = contour.end;
    }
    const std::span<const Point> used = points.first(start);
    for (const Point point : used) {
        if (!finite_point(point)) return ClipMaskStatus::invalid_geometry;
        if (!point_in_range(point)) return ClipMaskStatus::out_of_range;
    }
    return ClipMaskStatus::success;
}

} // namespace

ClipMaskStatus rasterize_clip_mask(const std::span<const Point> points, const std::span<const FlattenedContour> contours,
                                   const PolygonFillRule rule, const ClipMaskBounds bounds,
                                   ClipMaskWorkspace& workspace, const std::span<std::uint16_t> coverage) {
    const ClipMaskStatus valid = validate_mask_input(points, contours, rule, bounds, coverage);
    if (valid != ClipMaskStatus::success) return valid;
    if (bounds.width == 0U || bounds.height == 0U) return ClipMaskStatus::success;
    const std::size_t width = bounds.width;
    // The storage first: every growth happens before a byte is written.
    workspace.edges.clear();
    workspace.edges.reserve(points.size());
    workspace.active.clear();
    workspace.active.reserve(points.size());
    workspace.crossings.clear();
    workspace.crossings.reserve(points.size());
    workspace.partial.assign(width + 1U, 0);
    workspace.whole_changes.assign(width + 1U, 0);

    // Edges: each contour's chords, and the edge closing it (degenerate,
    // so dropped, for a closed contour, which repeats its start).
    std::size_t start = 0U;
    for (const FlattenedContour& contour : contours) {
        const std::size_t end = contour.end;
        if (end > start) {
            for (std::size_t index = start; index + 1U < end; ++index) {
                append_edge(workspace.edges, points[index], points[index + 1U]);
            }
            append_edge(workspace.edges, points[end - 1U], points[start]);
        }
        start = end;
    }
    std::sort(workspace.edges.begin(), workspace.edges.end(), &edge_starts_above);

    // An even-odd fill is inside where the winding count is odd, a nonzero
    // fill where any bit is set.
    const std::int32_t inside_bits = rule == PolygonFillRule::even_odd ? 1 : -1;
    const std::int64_t mask_left = bounds.left * unit;
    const std::int64_t mask_right = mask_left + static_cast<std::int64_t>(width) * unit;
    std::size_t next_edge = 0U;
    for (std::uint32_t row = 0U; row < bounds.height; ++row) {
        const std::int64_t row_top = (bounds.top + static_cast<std::int64_t>(row)) * unit;
        const std::int64_t row_bottom = row_top + unit;
        // Edges that begin above the row's end join; those ended by its top leave.
        while (next_edge < workspace.edges.size() && workspace.edges[next_edge].top_y < row_bottom) {
            workspace.active.push_back(next_edge);
            ++next_edge;
        }
        std::size_t kept = 0U;
        for (const std::size_t index : workspace.active) {
            if (workspace.edges[index].bottom_y <= row_top) continue;
            workspace.active[kept] = index;
            ++kept;
        }
        workspace.active.resize(kept);
        for (std::int64_t band = 0; band < clip_mask_sub_rows; ++band) {
            // The band's centre: (2 band + 1) / (2 sub_rows) of the pixel down.
            const std::int64_t sample_y = row_top + (unit * (2 * band + 1)) / (2 * clip_mask_sub_rows);
            workspace.crossings.clear();
            for (const std::size_t index : workspace.active) {
                const ClipMaskEdge& edge = workspace.edges[index];
                if (sample_y < edge.top_y || sample_y >= edge.bottom_y) continue;
                // Differences are within 2 * limit * unit < 2^31, so the
                // product stays inside 64 bits.
                const std::int64_t run = edge.bottom_x - edge.top_x;
                const std::int64_t rise = edge.bottom_y - edge.top_y;
                const std::int64_t x = edge.top_x + floor_quotient(run * (sample_y - edge.top_y), rise);
                workspace.crossings.push_back(ClipMaskCrossing{x, edge.winding});
            }
            std::sort(workspace.crossings.begin(), workspace.crossings.end(), &crossing_before);
            std::int32_t winding = 0;
            std::int64_t span_start = 0;
            for (const ClipMaskCrossing& crossing : workspace.crossings) {
                const bool was_inside = (winding & inside_bits) != 0;
                winding += crossing.winding;
                const bool is_inside = (winding & inside_bits) != 0;
                if (!was_inside && is_inside) span_start = crossing.x;
                if (!was_inside || is_inside) continue;
                const std::int64_t from = std::clamp(span_start, mask_left, mask_right) - mask_left;
                const std::int64_t to = std::clamp(crossing.x, mask_left, mask_right) - mask_left;
                if (to > from) accumulate_span(workspace, from, to);
            }
        }
        // Each pixel's sum over the bands is at most sub_rows * unit.
        const std::size_t row_offset = static_cast<std::size_t>(row) * width;
        std::int32_t whole = 0;
        for (std::size_t column = 0U; column < width; ++column) {
            whole += workspace.whole_changes[column];
            const std::int64_t sum = static_cast<std::int64_t>(workspace.partial[column]) +
                                     static_cast<std::int64_t>(whole) * unit;
            const std::int64_t mean = (sum + clip_mask_sub_rows / 2) / clip_mask_sub_rows;
            coverage[row_offset + column] = static_cast<std::uint16_t>(mean);
        }
        std::fill(workspace.partial.begin(), workspace.partial.end(), 0);
        std::fill(workspace.whole_changes.begin(), workspace.whole_changes.end(), 0);
    }
    return ClipMaskStatus::success;
}

bool intersect_clip_mask(const ClipMaskBounds enclosing_bounds, const std::span<const std::uint16_t> enclosing,
                         const ClipMaskBounds bounds, const std::span<std::uint16_t> coverage) noexcept {
    const std::uint64_t enclosing_area =
        static_cast<std::uint64_t>(enclosing_bounds.width) * static_cast<std::uint64_t>(enclosing_bounds.height);
    const std::uint64_t area = static_cast<std::uint64_t>(bounds.width) * static_cast<std::uint64_t>(bounds.height);
    if (enclosing_area > enclosing.size() || area > coverage.size()) return false;
    const std::size_t width = bounds.width;
    // The columns of the mask the enclosing one reaches: [first, end).
    const std::int64_t reach_left = enclosing_bounds.left - bounds.left;
    const std::int64_t reach_right = reach_left + static_cast<std::int64_t>(enclosing_bounds.width);
    const std::size_t first = static_cast<std::size_t>(std::clamp<std::int64_t>(reach_left, 0, static_cast<std::int64_t>(width)));
    const std::size_t end = static_cast<std::size_t>(
        std::clamp<std::int64_t>(reach_right, static_cast<std::int64_t>(first), static_cast<std::int64_t>(width)));
    for (std::uint32_t row = 0U; row < bounds.height; ++row) {
        std::uint16_t* const line = coverage.data() + static_cast<std::size_t>(row) * width;
        const std::int64_t enclosing_row = bounds.top + static_cast<std::int64_t>(row) - enclosing_bounds.top;
        if (enclosing_row < 0 || enclosing_row >= static_cast<std::int64_t>(enclosing_bounds.height) || first == end) {
            std::fill(line, line + width, std::uint16_t{0U});
            continue;
        }
        std::fill(line, line + first, std::uint16_t{0U});
        const std::size_t enclosing_offset =
            static_cast<std::size_t>(enclosing_row) * enclosing_bounds.width +
            static_cast<std::size_t>(static_cast<std::int64_t>(first) - reach_left);
        const std::uint16_t* const outer = enclosing.data() + enclosing_offset;
        for (std::size_t column = first; column < end; ++column) {
            const std::uint32_t inner = std::min<std::uint32_t>(line[column], full_coverage);
            const std::uint32_t enclosing_value = std::min<std::uint32_t>(outer[column - first], full_coverage);
            const std::uint32_t product = inner * enclosing_value + full_coverage / 2U;
            line[column] = static_cast<std::uint16_t>(product / full_coverage);
        }
        std::fill(line + end, line + width, std::uint16_t{0U});
    }
    return true;
}

} // namespace gui_forms::detail
