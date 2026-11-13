#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace gui_forms {

// Batched marks and patterns of the Painter vocabulary (IMGUI_IMPLOT_STUDY
// P7; NODE_ENGINE_AND_VALUE_BAR_STUDY 1A, pieces C7, C9 and C17). Each batch
// call draws a whole span the caller owns in one command: the painter reads
// the span during the call only, and a recording copies it once (one
// allocation per batch, never one per element).

// How two stroked segments of a polyline meet. `miter` falls back to `bevel`
// where the miter would reach further than `miter_limit` times half the
// stroke width.
enum class StrokeJoin : std::uint8_t { miter, round, bevel };

// How an open stroke ends: `butt` at the end point, `square` half the width
// beyond it, `round` with a half disc.
enum class StrokeCap : std::uint8_t { butt, round, square };

// The most on/off intervals a stroke's dash pattern holds.
inline constexpr std::size_t maximum_dash_intervals = 8U;

// A stroke's colour, width, joins, caps and dashes, in the painter's current
// coordinates. `dash_count` of the `dashes` are live, alternately on and off
// lengths; 0 (or an odd count, which is refused as solid) draws solid.
// `dash_phase` shifts the pattern's start along the stroke (an effect moving
// it on the motion clock animates the dashes, piece C13).
struct GUI_FORMS_API StrokeStyle final {
    Color color{};
    double width{1.0};
    StrokeJoin join{StrokeJoin::miter};
    double miter_limit{4.0};
    StrokeCap cap{StrokeCap::butt};
    std::array<double, maximum_dash_intervals> dashes{};
    std::size_t dash_count{};
    double dash_phase{};

    friend bool operator==(const StrokeStyle& left, const StrokeStyle& right) noexcept {
        if (!(left.color == right.color) || left.width != right.width || left.join != right.join ||
            left.miter_limit != right.miter_limit || left.cap != right.cap ||
            left.dash_count != right.dash_count || left.dash_phase != right.dash_phase) {
            return false;
        }
        for (std::size_t index = 0U; index < maximum_dash_intervals; ++index) {
            if (left.dashes[index] != right.dashes[index]) return false;
        }
        return true;
    }
};

// Whether `style` can be drawn: a finite positive width, a finite miter
// limit of at least 1, at most maximum_dash_intervals dashes, an even dash
// count whose entries are finite, non-negative and not all zero, and a
// finite phase.
[[nodiscard]] GUI_FORMS_API bool valid_stroke_style(const StrokeStyle& style) noexcept;

// One independent segment of stroke_segments (no joins between segments).
struct GUI_FORMS_API PaintSegment final {
    Point from{};
    Point to{};

    friend constexpr bool operator==(const PaintSegment& left, const PaintSegment& right) noexcept {
        return left.from == right.from && left.to == right.to;
    }
};

// How a filled polygon decides its inside where its edges cross.
enum class PolygonFillRule : std::uint8_t { nonzero, even_odd };

// Marker glyph shapes (the plots' scatter markers, ImPlot's set, and the
// small decorators of the node engine, piece C15). Filled shapes are
// circle through triangle_right; cross, plus and asterisk are strokes only.
enum class MarkerShape : std::uint8_t {
    circle,
    square,
    diamond,
    triangle_up,
    triangle_down,
    triangle_left,
    triangle_right,
    cross,
    plus,
    asterisk,
};

// A marker drawn centred on each point: `size` its full width, `fill` its
// inside (ignored by the stroked shapes), `edge` its outline (or its lines,
// for the stroked shapes) `edge_width` wide. A transparent colour or a zero
// edge width draws nothing of that part.
struct GUI_FORMS_API MarkerStyle final {
    MarkerShape shape{MarkerShape::circle};
    double size{6.0};
    Color fill{};
    Color edge{};
    double edge_width{1.0};

    friend constexpr bool operator==(const MarkerStyle& left, const MarkerStyle& right) noexcept {
        return left.shape == right.shape && left.size == right.size && left.fill == right.fill &&
               left.edge == right.edge && left.edge_width == right.edge_width;
    }
};

[[nodiscard]] GUI_FORMS_API bool valid_marker_style(const MarkerStyle& style) noexcept;

// The most vertices a filled marker's outline has (the diamond's and the
// square's four, the triangles' three).
inline constexpr std::size_t maximum_marker_vertices = 4U;

// Fills `vertices` with the outline of a filled, non-circular marker of
// `size` centred on `center`, clockwise in y-down coordinates, and returns
// how many it wrote; 0 for the circle and the stroked shapes, or when
// `size` is not finite and positive.
[[nodiscard]] GUI_FORMS_API std::size_t marker_outline(MarkerShape shape, Point center, double size,
                                         std::array<Point, maximum_marker_vertices>& vertices) noexcept;

// The most lines a stroked marker draws (the asterisk's three).
inline constexpr std::size_t maximum_marker_lines = 3U;

// Fills `lines` with the strokes of a stroked marker (cross, plus,
// asterisk) and returns how many it wrote; 0 for the filled shapes.
[[nodiscard]] GUI_FORMS_API std::size_t marker_lines(MarkerShape shape, Point center, double size,
                                       std::array<PaintSegment, maximum_marker_lines>& lines) noexcept;

// An alpha checkerboard (piece C7): square cells `cell` units wide,
// alternating `first` and `second`, with the corner of a `first` cell at
// `anchor`, so the pattern stays put while the filled rectangle moves.
struct GUI_FORMS_API CheckerPattern final {
    double cell{8.0};
    Color first{};
    Color second{};
    Point anchor{};

    friend constexpr bool operator==(const CheckerPattern& left, const CheckerPattern& right) noexcept {
        return left.cell == right.cell && left.first == right.first && left.second == right.second &&
               left.anchor == right.anchor;
    }
};

// A world-aligned grid (piece C17; the node canvas's blueprint background):
// lines `spacing` units apart through `origin`, the point where world
// (0, 0) lies in the painter's current coordinates, so the lines move with
// a pan; every `major_every`-th line counted from the origin (1: every
// line) is a major line in `major`, the others are minor lines in `minor`.
// Lines are `line_width` wide, centred on their positions.
struct GUI_FORMS_API GridPattern final {
    Point origin{};
    double spacing{16.0};
    std::uint32_t major_every{4U};
    Color minor{};
    Color major{};
    double line_width{1.0};

    friend constexpr bool operator==(const GridPattern& left, const GridPattern& right) noexcept {
        return left.origin == right.origin && left.spacing == right.spacing &&
               left.major_every == right.major_every && left.minor == right.minor &&
               left.major == right.major && left.line_width == right.line_width;
    }
};

// The fewest units between two grid lines a painter draws: a finer grid
// is refused (draws nothing), which bounds a fill to its area divided by
// this squared.
inline constexpr double minimum_grid_spacing = 2.0;

[[nodiscard]] GUI_FORMS_API bool valid_grid_pattern(const GridPattern& pattern) noexcept;

// Seamless deep zoom for the grid (NODE_ENGINE_AND_VALUE_BAR_STUDY 2.10):
// the grid has levels of world spacing `world_spacing * 4^k`; the level
// shown is the one whose spacing in view units (times `zoom`) lies in
// [minimum_spacing, maximum_spacing), which must span exactly the factor 4.
struct GUI_FORMS_API GridLevelBand final {
    double minimum_spacing{12.0};
    double maximum_spacing{48.0};
};

inline constexpr double grid_level_ratio = 4.0;

// The level chosen for one zoom: `level` is k, `minor_spacing` the level's
// spacing in view units, and `fade` the spacing's position across the band,
// 0 at its minimum and 1 at its maximum (linear in the spacing).
struct GUI_FORMS_API GridLevel final {
    std::int32_t level{};
    double minor_spacing{};
    double fade{};
};

// Chooses the level for `zoom` (view units per world unit). `world_spacing`
// and `zoom` must be finite and positive and the band's maximum exactly 4
// times its minimum; otherwise, or where no level within 64 steps of 0
// fits, the result is level 0 at `world_spacing * zoom` with fade 1. Exact:
// a level's spacing is `world_spacing * zoom` times a power of 4.
[[nodiscard]] GUI_FORMS_API GridLevel select_grid_level(double world_spacing, double zoom, GridLevelBand band) noexcept;

// The pattern for a chosen level, majors every 4 minor lines, so a level's
// major lines are the next coarser level's minor lines. Minor lines fade in
// over the first half of the band (alpha scaled by 2 × fade) and turn
// toward `major` over the second (colour interpolated by 2 × fade − 1), so
// at fade 1 they look as the majors they become at the next level: nothing
// pops as the zoom crosses a level.
[[nodiscard]] GUI_FORMS_API GridPattern grid_pattern_at_level(const GridLevel& level, Point origin, Color minor, Color major,
                                                double line_width) noexcept;

} // namespace gui_forms
