#pragma once

#include "gui_forms/surface_material/procedural_fill/procedural_fill.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gui_forms {

// A conic gradient (E9.6) sweeps its stops clockwise about `center`,
// starting `angle_degrees` from up (CSS `from`), over `period_degrees` of
// the turn: 360 for `conic-gradient`, less for `repeating-conic-gradient`
// with spread `repeat`. A radial gradient repeats or reflects its stops
// past its radii with spread `repeat` or `reflect` (E9.6).
//
// The engine's material kernel draws both exactly, and so does
// paint_surface_material wherever its painter composites part rasters (the
// recording and Skia painters bake through the kernel). A minimal painter's
// layers (Painter::draw_surface_material's base) draw a repeating radial
// gradient padded and a conic gradient as a solid of its first stop.
// How a fill layer joins the layers beneath it (E9.6, CSS
// `background-blend-mode`): source-over, multiply or screen, on
// premultiplied colour. A material whose fills all use `normal` draws them
// as before; one with another mode first gathers its fills into one face,
// apart from the shadows beneath, as CSS blends background layers only with
// one another. paint_surface_material blends them so too where it bakes
// through the kernel; a minimal painter's layers draw every layer
// source-over.
enum class MaterialBlendMode : std::uint8_t {
    normal,
    multiply,
    screen,
};

enum class MaterialFillKind : std::uint8_t {
    solid, linear_gradient, radial_gradient, image, conic_gradient,
    // A built-in pattern generated per display scale (E10): `procedural`.
    procedural,
};
// How an image fill covers its box: stretched over it; tiled from one tile
// (image_scale, image_anchor, image_offset); as a nine-patch; or `single`,
// that one tile alone, placed as a tile is, with nothing around it (E9.6,
// CSS `background-repeat: no-repeat`).
enum class MaterialImageMode : std::uint8_t {
    stretch, tile, nine_patch, single,
};
enum class MaterialCoordinateSpace : std::uint8_t {
    normalized, logical,
};
enum class MaterialLinearGeometry : std::uint8_t {
    endpoints, css_angle,
};

// One renderer-neutral material layer with explicit coordinate, spread,
// image sampling, scale, opacity, and nine-patch state.
struct MaterialFillLayer final {
    MaterialFillKind kind{MaterialFillKind::solid};
    MaterialCoordinateSpace coordinate_space{MaterialCoordinateSpace::normalized};
    MaterialLinearGeometry linear_geometry{MaterialLinearGeometry::endpoints};
    double angle_degrees{180.0};
    Color color{Color::rgba(255, 255, 255)};
    Point start{};
    Point end{1.0, 0.0};
    Point center{0.5, 0.5};
    Size radii{0.5, 0.5};
    std::vector<GradientStop> stops;
    GradientSpreadMode spread{GradientSpreadMode::pad};
    ImageId image{};
    Size image_pixel_size{};
    Insets image_slice{};
    double image_scale{1.0};
    double opacity{1.0};
    MaterialImageMode image_mode{MaterialImageMode::stretch};
    // A tiled image's position (E9.6, CSS `background-position`): one tile's
    // top-left corner lies at `image_anchor` of the room the box leaves
    // around one tile (fractions in [0, 1] across and down, as CSS
    // percentages: 0 the box's left or top, 1 a tile flush with its right
    // or bottom) plus `image_offset` in logical units, and the tiling runs
    // on from there both ways. Read in tile and single modes only: a
    // stretched image and a nine-patch cover the box.
    Point image_anchor{};
    Point image_offset{};
    double period_degrees{360.0};
    MaterialBlendMode blend{MaterialBlendMode::normal};
    // Read only by a procedural fill, with `opacity`.
    ProceduralFill procedural{};

    [[nodiscard]] static MaterialFillLayer solid(Color color);
    [[nodiscard]] static MaterialFillLayer linear(
        Point start, Point end, std::vector<GradientStop> stops,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized,
        GradientSpreadMode spread = GradientSpreadMode::pad);
    [[nodiscard]] static MaterialFillLayer linear_css_angle(
        double angle_degrees, std::vector<GradientStop> stops,
        GradientSpreadMode spread = GradientSpreadMode::pad);
    [[nodiscard]] static MaterialFillLayer linear_css_angle(
        double angle_degrees, const GradientStop* stops,
        std::size_t stop_count,
        GradientSpreadMode spread = GradientSpreadMode::pad);
    [[nodiscard]] static MaterialFillLayer linear(
        Point start, Point end, const GradientStop* stops,
        std::size_t stop_count,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized,
        GradientSpreadMode spread = GradientSpreadMode::pad);
    [[nodiscard]] static MaterialFillLayer repeating_linear(
        Point start, Point end, std::vector<GradientStop> stops,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::logical);
    [[nodiscard]] static MaterialFillLayer repeating_linear(
        Point start, Point end, const GradientStop* stops,
        std::size_t stop_count,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::logical);
    [[nodiscard]] static MaterialFillLayer radial(
        Point center, Size radii, std::vector<GradientStop> stops,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized);
    [[nodiscard]] static MaterialFillLayer radial(
        Point center, Size radii, const GradientStop* stops,
        std::size_t stop_count,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized);
    [[nodiscard]] static MaterialFillLayer repeating_radial(
        Point center, Size radii, std::vector<GradientStop> stops,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized);
    [[nodiscard]] static MaterialFillLayer conic(
        Point center, double from_degrees, std::vector<GradientStop> stops,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized);
    [[nodiscard]] static MaterialFillLayer repeating_conic(
        Point center, double from_degrees, double period_degrees,
        std::vector<GradientStop> stops,
        MaterialCoordinateSpace space = MaterialCoordinateSpace::normalized);
    [[nodiscard]] static MaterialFillLayer stretched_image(
        ImageId image, Size pixel_size, double opacity = 1.0);
    [[nodiscard]] static MaterialFillLayer tiled_image(
        ImageId image, Size pixel_size,
        double source_pixels_per_logical_pixel = 1.0,
        double opacity = 1.0);
    [[nodiscard]] static MaterialFillLayer nine_patch(
        ImageId image, Size pixel_size, Insets source_slice,
        double source_pixels_per_logical_pixel = 1.0,
        double opacity = 1.0);
    [[nodiscard]] static MaterialFillLayer procedural_pattern(
        const ProceduralFill& pattern, double opacity = 1.0);
    friend bool operator==(const MaterialFillLayer& left,
                           const MaterialFillLayer& right) noexcept(noexcept(
        left.kind == right.kind &&
        left.coordinate_space == right.coordinate_space &&
        left.linear_geometry == right.linear_geometry &&
        left.angle_degrees == right.angle_degrees &&
        left.color == right.color && left.start == right.start &&
        left.end == right.end && left.center == right.center &&
        left.radii == right.radii && left.stops == right.stops &&
        left.spread == right.spread && left.image == right.image &&
        left.image_pixel_size == right.image_pixel_size &&
        left.image_slice == right.image_slice &&
        left.image_scale == right.image_scale &&
        left.opacity == right.opacity && left.image_mode == right.image_mode &&
        left.image_anchor == right.image_anchor && left.image_offset == right.image_offset &&
        left.period_degrees == right.period_degrees &&
        left.blend == right.blend &&
        left.procedural == right.procedural)) {
        return left.kind == right.kind &&
               left.coordinate_space == right.coordinate_space &&
               left.linear_geometry == right.linear_geometry &&
               left.angle_degrees == right.angle_degrees &&
               left.color == right.color && left.start == right.start &&
               left.end == right.end && left.center == right.center &&
               left.radii == right.radii && left.stops == right.stops &&
               left.spread == right.spread && left.image == right.image &&
               left.image_pixel_size == right.image_pixel_size &&
               left.image_slice == right.image_slice &&
               left.image_scale == right.image_scale &&
               left.opacity == right.opacity &&
               left.image_mode == right.image_mode &&
               left.image_anchor == right.image_anchor &&
               left.image_offset == right.image_offset &&
               left.period_degrees == right.period_degrees &&
               left.blend == right.blend &&
               left.procedural == right.procedural;
    }
};

} // namespace gui_forms
