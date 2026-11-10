#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "grid_lines.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {
namespace {

// sin 60° and cos 45°: the asterisk's slanted strokes and the cross's
// diagonals reach the marker's half size along their own direction.
constexpr double sine_60_degrees = 0.86602540378443864676;
constexpr double cosine_45_degrees = 0.70710678118654752440;

std::uint8_t mixed_channel(std::uint8_t from, std::uint8_t to, double amount) noexcept {
    const double value = static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * amount;
    const long rounded = std::lround(value);
    const long bounded = std::clamp(rounded, 0L, 255L);
    return static_cast<std::uint8_t>(bounded);
}

} // namespace

bool valid_stroke_style(const StrokeStyle& style) noexcept {
    const bool known_join = style.join == StrokeJoin::miter || style.join == StrokeJoin::round ||
                            style.join == StrokeJoin::bevel;
    const bool known_cap = style.cap == StrokeCap::butt || style.cap == StrokeCap::round ||
                           style.cap == StrokeCap::square;
    if (!known_join || !known_cap || !std::isfinite(style.width) || style.width <= 0.0 ||
        !std::isfinite(style.miter_limit) || style.miter_limit < 1.0 || !std::isfinite(style.dash_phase) ||
        style.dash_count > maximum_dash_intervals || style.dash_count % 2U != 0U) {
        return false;
    }
    double total = 0.0;
    for (std::size_t index = 0U; index < style.dash_count; ++index) {
        const double interval = style.dashes[index];
        if (!std::isfinite(interval) || interval < 0.0) return false;
        total = total + interval;
    }
    const bool solid = style.dash_count == 0U;
    return solid || (std::isfinite(total) && total > 0.0);
}

bool valid_marker_style(const MarkerStyle& style) noexcept {
    const bool known_shape = static_cast<std::uint8_t>(style.shape) <= static_cast<std::uint8_t>(MarkerShape::asterisk);
    return known_shape && std::isfinite(style.size) && style.size > 0.0 && std::isfinite(style.edge_width) &&
           style.edge_width >= 0.0;
}

std::size_t marker_outline(MarkerShape shape, Point center, double size,
                           std::array<Point, maximum_marker_vertices>& vertices) noexcept {
    if (!std::isfinite(size) || size <= 0.0) return 0U;
    const double half = size * 0.5;
    const double left = center.x - half;
    const double right = center.x + half;
    const double top = center.y - half;
    const double bottom = center.y + half;
    switch (shape) {
    case MarkerShape::square:
        vertices = {Point{left, top}, Point{right, top}, Point{right, bottom}, Point{left, bottom}};
        return 4U;
    case MarkerShape::diamond:
        vertices = {Point{center.x, top}, Point{right, center.y}, Point{center.x, bottom}, Point{left, center.y}};
        return 4U;
    case MarkerShape::triangle_up:
        vertices = {Point{center.x, top}, Point{right, bottom}, Point{left, bottom}, Point{}};
        return 3U;
    case MarkerShape::triangle_down:
        vertices = {Point{left, top}, Point{right, top}, Point{center.x, bottom}, Point{}};
        return 3U;
    case MarkerShape::triangle_left:
        vertices = {Point{left, center.y}, Point{right, top}, Point{right, bottom}, Point{}};
        return 3U;
    case MarkerShape::triangle_right:
        vertices = {Point{left, top}, Point{right, center.y}, Point{left, bottom}, Point{}};
        return 3U;
    case MarkerShape::circle:
    case MarkerShape::cross:
    case MarkerShape::plus:
    case MarkerShape::asterisk:
        return 0U;
    }
    // A shape outside the enumeration has no outline.
    return 0U;
}

std::size_t marker_lines(MarkerShape shape, Point center, double size,
                         std::array<PaintSegment, maximum_marker_lines>& lines) noexcept {
    if (!std::isfinite(size) || size <= 0.0) return 0U;
    const double half = size * 0.5;
    switch (shape) {
    case MarkerShape::plus:
        lines[0] = PaintSegment{Point{center.x - half, center.y}, Point{center.x + half, center.y}};
        lines[1] = PaintSegment{Point{center.x, center.y - half}, Point{center.x, center.y + half}};
        return 2U;
    case MarkerShape::cross: {
        const double reach = half * cosine_45_degrees;
        lines[0] = PaintSegment{Point{center.x - reach, center.y - reach}, Point{center.x + reach, center.y + reach}};
        lines[1] = PaintSegment{Point{center.x - reach, center.y + reach}, Point{center.x + reach, center.y - reach}};
        return 2U;
    }
    case MarkerShape::asterisk: {
        const double across = half * sine_60_degrees;
        const double along = half * 0.5;
        lines[0] = PaintSegment{Point{center.x, center.y - half}, Point{center.x, center.y + half}};
        lines[1] = PaintSegment{Point{center.x - across, center.y - along}, Point{center.x + across, center.y + along}};
        lines[2] = PaintSegment{Point{center.x - across, center.y + along}, Point{center.x + across, center.y - along}};
        return 3U;
    }
    case MarkerShape::circle:
    case MarkerShape::square:
    case MarkerShape::diamond:
    case MarkerShape::triangle_up:
    case MarkerShape::triangle_down:
    case MarkerShape::triangle_left:
    case MarkerShape::triangle_right:
        return 0U;
    }
    // A shape outside the enumeration has no lines.
    return 0U;
}

bool valid_grid_pattern(const GridPattern& pattern) noexcept {
    return std::isfinite(pattern.origin.x) && std::isfinite(pattern.origin.y) && std::isfinite(pattern.spacing) &&
           pattern.spacing >= minimum_grid_spacing && pattern.major_every >= 1U &&
           std::isfinite(pattern.line_width) && pattern.line_width > 0.0;
}

GridLevel select_grid_level(double world_spacing, double zoom, GridLevelBand band) noexcept {
    GridLevel fallback{};
    fallback.level = 0;
    fallback.minor_spacing = world_spacing * zoom;
    fallback.fade = 1.0;
    const bool valid_band = std::isfinite(band.minimum_spacing) && band.minimum_spacing > 0.0 &&
                            band.maximum_spacing == band.minimum_spacing * grid_level_ratio;
    const double start = world_spacing * zoom;
    if (!valid_band || !std::isfinite(world_spacing) || world_spacing <= 0.0 || !std::isfinite(zoom) ||
        zoom <= 0.0 || !std::isfinite(start) || start <= 0.0) {
        return fallback;
    }
    // Multiplying and dividing by 4 is exact in binary floating point, so a
    // level's spacing is exactly the start times a power of 4.
    constexpr std::int32_t maximum_steps = 64;
    double spacing = start;
    std::int32_t level = 0;
    for (std::int32_t step = 0; step < maximum_steps && spacing < band.minimum_spacing; ++step) {
        spacing = spacing * grid_level_ratio;
        level = level + 1;
    }
    for (std::int32_t step = 0; step < maximum_steps && spacing >= band.maximum_spacing; ++step) {
        spacing = spacing / grid_level_ratio;
        level = level - 1;
    }
    if (spacing < band.minimum_spacing || spacing >= band.maximum_spacing) return fallback;
    GridLevel result{};
    result.level = level;
    result.minor_spacing = spacing;
    result.fade = (spacing - band.minimum_spacing) / (band.maximum_spacing - band.minimum_spacing);
    return result;
}

GridPattern grid_pattern_at_level(const GridLevel& level, Point origin, Color minor, Color major,
                                  double line_width) noexcept {
    const double fade = std::isfinite(level.fade) ? std::clamp(level.fade, 0.0, 1.0) : 1.0;
    Color shown = minor;
    if (fade < 0.5) {
        shown.alpha = mixed_channel(0U, minor.alpha, fade * 2.0);
    } else {
        const double toward = fade * 2.0 - 1.0;
        shown.red = mixed_channel(minor.red, major.red, toward);
        shown.green = mixed_channel(minor.green, major.green, toward);
        shown.blue = mixed_channel(minor.blue, major.blue, toward);
        shown.alpha = mixed_channel(minor.alpha, major.alpha, toward);
    }
    GridPattern pattern{};
    pattern.origin = origin;
    pattern.spacing = level.minor_spacing;
    pattern.major_every = 4U;
    pattern.minor = shown;
    pattern.major = major;
    pattern.line_width = line_width;
    return pattern;
}

namespace detail {

GridLineRange grid_line_range(double low, double high, double origin, double spacing, double half_width) noexcept {
    GridLineRange range{};
    const double first = std::ceil((low - half_width - origin) / spacing);
    const double last = std::floor((high + half_width - origin) / spacing);
    if (!std::isfinite(first) || !std::isfinite(last) || last < first) return range;
    const double span = last - first + 1.0;
    // Indices beyond 2^62 would not convert exactly (or at all).
    constexpr double index_bound = 4611686018427387904.0;
    if (span > static_cast<double>(maximum_grid_lines) || std::abs(first) > index_bound ||
        std::abs(last) > index_bound) {
        return range;
    }
    range.first = static_cast<std::int64_t>(first);
    range.last = static_cast<std::int64_t>(last);
    range.count = range.last - range.first + 1;
    return range;
}

bool grid_line_is_major(std::int64_t index, std::uint32_t major_every) noexcept {
    if (major_every <= 1U) return true;
    const std::int64_t every = static_cast<std::int64_t>(major_every);
    const std::int64_t remainder = index % every;
    return remainder == 0;
}

} // namespace detail

} // namespace gui_forms
