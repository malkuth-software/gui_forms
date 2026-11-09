#include "gui_forms/types/paint_recipes/paint_recipes.hpp"
#include "../../theme/contrast/theme_contrast.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gui_forms {
namespace {

// Draws `text` once in `color` under clip_rect(`band`); an empty band draws
// nothing.
void draw_text_in_band(Painter& painter, Rect band, Point origin, std::string_view text, FontSpec font,
                       Color color) {
    if (band.empty()) return;
    painter.save();
    painter.clip_rect(band);
    painter.draw_text_utf8(origin, text, font, color);
    painter.restore();
}

} // namespace

Rect proportion_rect(Rect track, double proportion, FillDirection direction) noexcept {
    const double amount = std::isnan(proportion) ? 0.0 : std::clamp(proportion, 0.0, 1.0);
    const double width = track.width * amount;
    const double height = track.height * amount;
    switch (direction) {
    case FillDirection::left_to_right: return Rect{track.x, track.y, width, track.height};
    case FillDirection::right_to_left: return Rect{track.right() - width, track.y, width, track.height};
    case FillDirection::top_to_bottom: return Rect{track.x, track.y, track.width, height};
    case FillDirection::bottom_to_top: return Rect{track.x, track.bottom() - height, track.width, height};
    }
    // A direction outside the enumeration covers nothing.
    return Rect{track.x, track.y, 0.0, 0.0};
}

void paint_masked_fill(Painter& painter, Rect track, PaintCornerRadii track_radii, double proportion,
                       FillDirection direction, Color fill) {
    const Rect portion = proportion_rect(track, proportion, direction);
    if (portion.empty() || !portion.finite()) return;
    painter.save();
    painter.clip_rounded_rect_corners(track, track_radii);
    painter.fill_rect(portion, fill);
    painter.restore();
}

double wcag_contrast_ratio(Color first, Color second) noexcept {
    return detail::contrast_ratio(first, second);
}

Color contrasting_text_color(Color background, Color light, Color dark) noexcept {
    const double light_ratio = detail::contrast_ratio(background, light);
    const double dark_ratio = detail::contrast_ratio(background, dark);
    return light_ratio > dark_ratio ? light : dark;
}

void paint_text_over_fill(Painter& painter, Rect track, Rect fill, Point origin, std::string_view text,
                          FontSpec font, Color text_color, Color on_fill_color) {
    if (track.empty() || !track.finite()) return;
    const Rect covered = Rect::intersection(track, fill);
    if (covered.empty()) {
        draw_text_in_band(painter, track, origin, text, font, text_color);
        return;
    }
    draw_text_in_band(painter, covered, origin, text, font, on_fill_color);
    // The rest of the track: full-width bands above and below the covered
    // part, and bands left and right of it across its rows. Each shares the
    // covered part's edge values exactly.
    const Rect above{track.x, track.y, track.width, covered.y - track.y};
    const Rect below{track.x, covered.bottom(), track.width, track.bottom() - covered.bottom()};
    const Rect left{track.x, covered.y, covered.x - track.x, covered.height};
    const Rect right{covered.right(), covered.y, track.right() - covered.right(), covered.height};
    const std::array<Rect, 4> bands{above, below, left, right};
    for (const Rect band : bands) {
        draw_text_in_band(painter, band, origin, text, font, text_color);
    }
}

JoinedSegmentPosition joined_segment_position(std::size_t index, std::size_t count) noexcept {
    if (count <= 1U) return JoinedSegmentPosition::only;
    if (index == 0U) return JoinedSegmentPosition::first;
    if (index + 1U >= count) return JoinedSegmentPosition::last;
    return JoinedSegmentPosition::middle;
}

PaintCornerRadii joined_segment_radii(JoinedSegmentPosition position, JoinAxis axis, double radius) noexcept {
    PaintCornerRadii radii{};
    const bool leading = position == JoinedSegmentPosition::only || position == JoinedSegmentPosition::first;
    const bool trailing = position == JoinedSegmentPosition::only || position == JoinedSegmentPosition::last;
    if (axis == JoinAxis::row) {
        radii.top_left = leading ? radius : 0.0;
        radii.bottom_left = leading ? radius : 0.0;
        radii.top_right = trailing ? radius : 0.0;
        radii.bottom_right = trailing ? radius : 0.0;
        return radii;
    }
    radii.top_left = leading ? radius : 0.0;
    radii.top_right = leading ? radius : 0.0;
    radii.bottom_left = trailing ? radius : 0.0;
    radii.bottom_right = trailing ? radius : 0.0;
    return radii;
}

void paint_joined_segments(Painter& painter, std::span<const Rect> segments, std::span<const Color> faces,
                           JoinAxis axis, const JoinedSegmentsStyle& style) {
    const bool valid_style = std::isfinite(style.radius) && style.radius >= 0.0 &&
                             std::isfinite(style.edge_width) && style.edge_width >= 0.0;
    if (segments.empty() || segments.size() != faces.size() || !valid_style) return;
    Rect bounds = segments[0];
    for (std::size_t index = 0U; index < segments.size(); ++index) {
        const JoinedSegmentPosition position = joined_segment_position(index, segments.size());
        const PaintCornerRadii radii = joined_segment_radii(position, axis, style.radius);
        painter.fill_rounded_rect_corners(segments[index], radii, faces[index]);
        bounds = Rect::united(bounds, segments[index]);
    }
    if (style.edge_width <= 0.0) return;
    // One seam centred in each gap (or on each shared edge).
    if (axis == JoinAxis::row) {
        for (std::size_t index = 0U; index + 1U < segments.size(); ++index) {
            const double x = (segments[index].right() + segments[index + 1U].x) * 0.5;
            painter.draw_line(Point{x, bounds.y}, Point{x, bounds.bottom()}, style.seam, style.edge_width);
        }
    } else {
        for (std::size_t index = 0U; index + 1U < segments.size(); ++index) {
            const double y = (segments[index].bottom() + segments[index + 1U].y) * 0.5;
            painter.draw_line(Point{bounds.x, y}, Point{bounds.right(), y}, style.seam, style.edge_width);
        }
    }
    painter.stroke_rounded_rect(bounds, style.radius, style.edge, style.edge_width);
}

} // namespace gui_forms
