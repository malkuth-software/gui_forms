#pragma once

#include "gui_forms/drawing/color/color.hpp"
#include "gui_forms/drawing/image_snapshot/image_snapshot.hpp"
#include "gui_forms/drawing/matrix/matrix.hpp"
#include "gui_forms/drawing/types/pixel_format/pixel_format.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_drawing {

enum class DashStyle : std::uint8_t { solid, dash, dot, dash_dot, dash_dot_dot, custom };
enum class FontStyle : std::uint32_t {
    regular = 0,
    bold = 1U << 0U,
    italic = 1U << 1U,
    underline = 1U << 2U,
    strikeout = 1U << 3U,
};

enum class GraphicsUnit : std::uint8_t { world, display, pixel, point, inch, document, millimeter };
enum class StringAlignment : std::uint8_t { near, center, far };
enum class StringTrimming : std::uint8_t { none, character, word, ellipsis_character, ellipsis_word, ellipsis_path };
enum class SmoothingMode : std::uint8_t { default_mode, high_speed, high_quality, none, anti_alias };
enum class InterpolationMode : std::uint8_t { default_mode, low, high, nearest, bilinear, bicubic, high_quality_bilinear, high_quality_bicubic };
enum class PixelOffsetMode : std::uint8_t { default_mode, high_speed, high_quality, none, half };
enum class CompositingMode : std::uint8_t { source_over, source_copy };
enum class CompositingQuality : std::uint8_t { default_mode, high_speed, high_quality, gamma_corrected, assume_linear };
enum class FillMode : std::uint8_t { alternate, winding };
enum class WrapMode : std::uint8_t { tile, tile_flip_x, tile_flip_y, tile_flip_xy, clamp };
enum class HatchStyle : std::uint8_t {
    horizontal, vertical, forward_diagonal, backward_diagonal,
    cross, diagonal_cross,
};
enum class BrushKind : std::uint8_t {
    solid,
    hatch,
    linear_gradient,
    path_gradient,
    texture,
    conic_gradient,
    checker,
};

// How a pen's segments meet (in the compatibility order: miter, bevel,
// round) and how its open ends are capped (flat at the end point, square
// half the width beyond it, round with a half disc).
enum class LineJoin : std::uint8_t { miter, bevel, round };
enum class LineCap : std::uint8_t { flat, square, round };

// Marker glyphs for GraphicsRecorder::draw_markers (the GUI.Forms Painter's
// MarkerShape, in its order). circle through triangle_right are filled;
// cross, plus and asterisk are strokes in the edge colour.
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

// A marker centred on each point: `size` its full width, `fill` its inside,
// `edge` its outline (or lines) `edge_width` wide. An empty or transparent
// colour, or a zero edge width, draws nothing of that part.
struct MarkerStyle final {
    MarkerShape shape{MarkerShape::circle};
    double size{6.0};
    Color fill;
    Color edge;
    double edge_width{1.0};
};

// A world-aligned grid for GraphicsRecorder::fill_grid_pattern: lines
// `spacing` apart through `origin` (the recorder's coordinates), every
// `major_every`-th line from the origin in `major`, the rest in `minor`,
// each `line_width` wide.
struct GridPatternStyle final {
    PointF origin;
    double spacing{16.0};
    std::uint32_t major_every{4U};
    Color minor;
    Color major;
    double line_width{1.0};
};

// A rounded rectangle's corner radii: top-left, top-right, bottom-right,
// bottom-left. Each is held to half the rectangle's shorter side.
struct CornerRadii final {
    double top_left{};
    double top_right{};
    double bottom_right{};
    double bottom_left{};
};

// The edge a proportional fill grows from.
enum class FillDirection : std::uint8_t { left_to_right, right_to_left, bottom_to_top, top_to_bottom };

struct ColorBlend final {
    std::vector<Color> colors;
    std::vector<double> positions;
};

struct BrushSnapshot final {
    BrushKind kind{BrushKind::solid};
    Color primary;
    Color secondary;
    HatchStyle hatch_style{HatchStyle::horizontal};
    WrapMode wrap_mode{WrapMode::tile};
    RectF bounds;
    PointF center;
    double angle{};
    std::vector<PointF> points;
    std::vector<Color> colors;
    std::vector<double> positions;
    ImageSnapshot image;
    Matrix transform;
    // A checker brush's cell width; 0 for every other kind.
    double cell_size{};
};

} // namespace gui_drawing
