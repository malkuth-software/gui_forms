#include "gui_forms/content/editing/content_text_layout.hpp"

#include "gui_forms/text/text_store/text_store.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace gui_forms {

namespace {

constexpr std::size_t no_line = std::numeric_limits<std::size_t>::max();

[[nodiscard]] bool valid_box(const Rect& box) noexcept {
    const bool result = box.finite() && box.width >= 0.0 && box.height >= 0.0;
    return result;
}

// Whether `range` lies within [0, size] in order, with both ends grapheme
// boundaries of `text` (checked separately, so the caller reports which).
[[nodiscard]] bool range_in_text(const Utf8Range& range, Utf8Offset size) noexcept {
    const bool result = range.start <= range.end && range.end <= size;
    return result;
}

[[nodiscard]] bool range_on_boundaries(const Utf8Range& range, const TextStore& text) noexcept {
    const bool result = text.is_grapheme_boundary(range.start) && text.is_grapheme_boundary(range.end);
    return result;
}

[[nodiscard]] ContentLayoutResult validate_lines(const ContentTextLayout& layout, const TextStore& text) noexcept {
    const Utf8Offset size = text.utf8_size();
    Utf8Offset previous_end(0U);
    for (const ContentTextLine& line : layout.lines) {
        if (!range_in_text(line.range, size) || line.range.start < previous_end) {
            return ContentLayoutResult::refused_range;
        }
        if (!range_on_boundaries(line.range, text)) return ContentLayoutResult::refused_boundary;
        if (!valid_box(line.bounds)) return ContentLayoutResult::refused_geometry;
        previous_end = line.range.end;
    }
    return ContentLayoutResult::accepted;
}

// Every line with text is covered by its clusters, from its start to its
// end without gaps, and an empty line has none. Lines are already checked
// to be in order and within the text, so a cluster that passes lies within
// the text too. Lines are walked once: `open` is the line the clusters are
// covering and `cursor` where its next cluster must start.
[[nodiscard]] ContentLayoutResult validate_clusters(const ContentTextLayout& layout, const TextStore& text) noexcept {
    const std::size_t line_count = layout.lines.size();
    std::size_t open = no_line;
    Utf8Offset cursor(0U);
    for (const ContentTextCluster& cluster : layout.clusters) {
        const std::size_t line = cluster.line;
        if (line >= line_count || cluster.range.start >= cluster.range.end) {
            return ContentLayoutResult::refused_cluster;
        }
        if (line != open) {
            if (open != no_line) {
                if (line < open) return ContentLayoutResult::refused_range;
                if (cursor != layout.lines[open].range.end) return ContentLayoutResult::refused_cluster;
            }
            const std::size_t first_skipped = open == no_line ? 0U : open + 1U;
            for (std::size_t skipped = first_skipped; skipped < line; ++skipped) {
                if (!layout.lines[skipped].range.empty()) return ContentLayoutResult::refused_cluster;
            }
            open = line;
            cursor = layout.lines[open].range.start;
        }
        if (cluster.range.start != cursor) return ContentLayoutResult::refused_cluster;
        if (cluster.range.end > layout.lines[open].range.end) return ContentLayoutResult::refused_cluster;
        if (!range_on_boundaries(cluster.range, text)) return ContentLayoutResult::refused_boundary;
        if (!valid_box(cluster.bounds)) return ContentLayoutResult::refused_geometry;
        cursor = cluster.range.end;
    }
    if (open != no_line && cursor != layout.lines[open].range.end) return ContentLayoutResult::refused_cluster;
    const std::size_t first_trailing = open == no_line ? 0U : open + 1U;
    for (std::size_t trailing = first_trailing; trailing < line_count; ++trailing) {
        if (!layout.lines[trailing].range.empty()) return ContentLayoutResult::refused_cluster;
    }
    return ContentLayoutResult::accepted;
}

[[nodiscard]] ContentLayoutResult validate_carets(const ContentTextLayout& layout, const TextStore& text) noexcept {
    const Utf8Offset size = text.utf8_size();
    bool first = true;
    Utf8Offset previous(0U);
    for (const ContentCaretStop& stop : layout.carets) {
        if (stop.offset > size) return ContentLayoutResult::refused_range;
        if (!first && stop.offset <= previous) return ContentLayoutResult::refused_caret;
        if (!text.is_grapheme_boundary(stop.offset)) return ContentLayoutResult::refused_boundary;
        if (!valid_box(stop.caret)) return ContentLayoutResult::refused_geometry;
        previous = stop.offset;
        first = false;
    }
    return ContentLayoutResult::accepted;
}

// Whether `transform` is finite and keeps the plane (a determinant that is
// finite and not zero), so its inverse exists.
[[nodiscard]] bool valid_transform(const ContentLayoutTransform& transform) noexcept {
    const bool finite = std::isfinite(transform.xx) && std::isfinite(transform.xy) && std::isfinite(transform.yx) &&
                        std::isfinite(transform.yy) && std::isfinite(transform.dx) && std::isfinite(transform.dy);
    if (!finite) return false;
    const double determinant = transform.determinant();
    const bool result = std::isfinite(determinant) && determinant != 0.0;
    return result;
}

} // namespace

Point ContentLayoutTransform::map(Point point) const noexcept {
    const double x = xx * point.x + xy * point.y + dx;
    const double y = yx * point.x + yy * point.y + dy;
    const Point mapped{x, y};
    return mapped;
}

double ContentLayoutTransform::determinant() const noexcept {
    const double result = xx * yy - xy * yx;
    return result;
}

bool ContentLayoutTransform::axis_aligned() const noexcept {
    const bool result = xy == 0.0 && yx == 0.0;
    return result;
}

Rect ContentQuad::bounds() const noexcept {
    double left = corners[0].x;
    double right = corners[0].x;
    double top = corners[0].y;
    double bottom = corners[0].y;
    for (const Point& corner : corners) {
        left = std::min(left, corner.x);
        right = std::max(right, corner.x);
        top = std::min(top, corner.y);
        bottom = std::max(bottom, corner.y);
    }
    const Rect result{left, top, right - left, bottom - top};
    return result;
}

ContentQuad map_content_box(const ContentLayoutTransform& transform, Rect box) noexcept {
    ContentQuad quad{};
    quad.corners[0] = transform.map(Point{box.x, box.y});
    quad.corners[1] = transform.map(Point{box.x + box.width, box.y});
    quad.corners[2] = transform.map(Point{box.x + box.width, box.y + box.height});
    quad.corners[3] = transform.map(Point{box.x, box.y + box.height});
    return quad;
}

std::string_view content_layout_result_name(ContentLayoutResult result) noexcept {
    switch (result) {
    case ContentLayoutResult::accepted: return "accepted";
    case ContentLayoutResult::stale_revision: return "stale_revision";
    case ContentLayoutResult::refused_too_large: return "refused_too_large";
    case ContentLayoutResult::refused_range: return "refused_range";
    case ContentLayoutResult::refused_boundary: return "refused_boundary";
    case ContentLayoutResult::refused_cluster: return "refused_cluster";
    case ContentLayoutResult::refused_geometry: return "refused_geometry";
    case ContentLayoutResult::refused_caret: return "refused_caret";
    }
    return "unknown";
}

ContentLayoutResult validate_content_text_layout(const ContentTextLayout& layout, const TextStore& text,
                                                 std::uint64_t revision) {
    // The counts come first, so nothing larger than the limit is walked.
    if (layout.lines.size() > maximum_content_layout_entries ||
        layout.clusters.size() > maximum_content_layout_entries ||
        layout.carets.size() > maximum_content_layout_entries) {
        return ContentLayoutResult::refused_too_large;
    }
    if (layout.text_revision != revision) return ContentLayoutResult::stale_revision;
    const ContentLayoutTransform* const mapping = layout.transform.value();
    if (mapping != nullptr && !valid_transform(*mapping)) return ContentLayoutResult::refused_geometry;
    const ContentLayoutResult lines = validate_lines(layout, text);
    if (lines != ContentLayoutResult::accepted) return lines;
    const ContentLayoutResult clusters = validate_clusters(layout, text);
    if (clusters != ContentLayoutResult::accepted) return clusters;
    const ContentLayoutResult carets = validate_carets(layout, text);
    return carets;
}

} // namespace gui_forms
