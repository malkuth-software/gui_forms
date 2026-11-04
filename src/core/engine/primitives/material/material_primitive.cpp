#include "material_primitive.hpp"

#include "material_bake_policy.hpp"
#include "material_image.hpp"
#include "../kernel/box_blur.hpp"
#include "../kernel/pixel_kernel.hpp"
#include "../procedural/procedural_pattern.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/presets/material_presets.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <algorithm>
#include <limits>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// -- the descriptor ----------------------------------------------------------------

// The recipe roles in ControlVisualRole's order, then `family`: the recipe
// of the part's own family (PartPaint::family, T3 EC1), so that a theme's
// material fields on any part keep that part's recipe as their floor.
constexpr std::array<std::string_view, 15> recipe_names{
    "window", "panel", "card", "button", "accent_button", "command_button", "choice", "editor",
    "menu_item", "selection", "progress", "tool_tip", "link", "visited_link", "family"};
constexpr double family_recipe = 14.0;
static_assert(recipe_names.size() == static_cast<std::size_t>(ControlVisualRole::count) + 1U,
              "one choice per recipe role, then the family's");

const PartRole material_roles[] = {PartRole::region, PartRole::face,  PartRole::actuator, PartRole::well,
                                   PartRole::track,  PartRole::thumb, PartRole::chip};

// The states part_visual_context reads to choose the recipe's state. The
// raster is also redrawn whenever the material lent for the state differs.
const AnatomyState material_states[] = {
    AnatomyState::hot,         AnatomyState::pressed,       AnatomyState::captured,  AnatomyState::selected,
    AnatomyState::checked,     AnatomyState::current,       AnatomyState::open,      AnatomyState::active_tool,
    AnatomyState::focus_visible, AnatomyState::preselect,   AnatomyState::disabled,  AnatomyState::unavailable};

enum MaterialParameter : std::size_t {
    material_recipe,
    material_shape,
    material_parameter_count,
};

// The default recipe is the part's family's (EC1; the button's, position 3,
// before engine 0.3). The shape (E9.6) is the box's outline; a rounded
// rectangle takes its radii from the material.
const PrimitiveParameterDeclaration material_parameters[] = {
    {"recipe", PrimitiveParameterKind::choice, 0.0, 0.0, family_recipe, {}, false, recipe_names},
    {"shape", PrimitiveParameterKind::choice, 0.0, 0.0, 0.0, {}, false, part_shape_names},
};
static_assert(std::size(material_parameters) == material_parameter_count, "one declaration per material parameter");

// The declared cost is the most the load check admits; a material's real
// bake is bounded by its effort instead (accepts, material_bake_policy.hpp).
const PrimitiveDescriptor material_descriptor{
    "material",
    EngineVersion{0, 2},
    material_roles,
    material_states,
    material_parameters,
    PrimitiveCost{512U, 64U, 0U},
    Insets{material_outset, material_outset, material_outset, material_outset},
    0U};

// -- the look: every layer resolved to device pixels, before the loops -----------

struct ResolvedShadow final {
    // The shadow's rectangle (an outer shadow's shape, an inset shadow's
    // hole), the outline drawn in it, the largest of its corners' radii
    // (where it differs from the rectangle: see beyond_straight), and 1 / σ'.
    Rect shape{};
    PartShape outline{};
    double radius{};
    double inverse_sigma{};
    Color colour{};
    // An inset shadow whose hole is empty covers the whole box.
    bool solid{};
    // A glow (MaterialGlow): an outer shadow at no offset whose colour is
    // added to what lies beneath it rather than composited over it.
    bool additive{};
    // An over glow: its coverage is kept while the outer layers are
    // evaluated and added after the face's layers, within the box.
    bool over{};
};

// The outer shadows, then the glows (under and over).
constexpr std::size_t maximum_outer = SurfaceMaterial::maximum_shadows + SurfaceMaterial::maximum_glows;

// The glow's reach is within the material's outsets, so that the
// destination holds all of it (valid_surface_material).
static_assert(SurfaceMaterial::maximum_glow_reach <= material_outset, "a glow stays within the material's outsets");

struct ResolvedFill final {
    MaterialFillKind kind{MaterialFillKind::solid};
    Color colour{};
    // A linear gradient's t = (x − origin_x) × step_x + (y − origin_y) ×
    // step_y; a radial gradient's centre and inverse radii.
    double origin_x{};
    double origin_y{};
    double step_x{};
    double step_y{};
    double inverse_radius_x{};
    double inverse_radius_y{};
    // A conic gradient's centre is the origin; t is the turn clockwise from
    // up past `from_turn` (in [0, 1)), times the turns to one period.
    double from_turn{};
    double periods_per_turn{1.0};
    GradientSpreadMode spread{GradientSpreadMode::pad};
    MaterialBlendMode blend{MaterialBlendMode::normal};
    // Borrowed from the material for the bake.
    std::span<const GradientStop> stops{};
    // An image fill's opacity and its resampled layer (MaterialImages).
    double opacity{1.0};
    std::size_t image{};
};

struct ResolvedBand final {
    Rect band{};
    Color colour{};
    bool clipped{};
};

constexpr std::size_t maximum_bands = 4U + SurfaceMaterial::maximum_keylines;

constexpr double quarter_turn = 3.14159265358979323846264338327950288 * 0.5;

// A dashed or dotted border's path (E9.6): the border's centre line, half
// its width inside the box, walked clockwise from the end of the top-left
// corner (from the top, for a circle). `starts` holds the arc length at
// which each of the line's eight pieces begins: the top edge, the top-right
// arc, the right edge, the bottom-right arc, the bottom edge, the
// bottom-left arc, the left edge and the top-left arc. `period` is one dash
// and its gap (or one dot and its gap), stretched so that a whole number go
// round; `dash` is a dash's length.
struct BorderPath final {
    MaterialBorderStyle style{MaterialBorderStyle::solid};
    bool circle{};
    Rect line{};
    PartCornerRadii radii{};
    std::array<double, 8> starts{};
    double period{};
    double dash{};
    double width{};
};

struct MaterialLook final {
    Rect box{};
    PartShape box_shape{};
    // Two bands of pixel centres the box covers fully, avoiding its rounded
    // corners: there its coverage is exactly 1 without its signed distance.
    Rect wide_interior{};
    Rect tall_interior{};
    std::array<ResolvedShadow, maximum_outer> outer{};
    std::size_t outer_count{};
    std::array<ResolvedShadow, SurfaceMaterial::maximum_shadows> inset{};
    std::size_t inset_count{};
    // The fills that draw: a solid fill of alpha 0 composites nothing and is
    // left out (blending a zero unit over a pixel leaves its bytes as they
    // are). Each keeps its index among the material's fills (`image`).
    std::array<ResolvedFill, SurfaceMaterial::maximum_fill_layers> fills{};
    std::size_t fill_count{};
    // Whether any layer reads the box's coverage; when none does, the pixel
    // loop does not compute it.
    bool reads_inside{};
    // Whether a fill blends other than source-over: the fills are then
    // gathered into one face before it meets the shadows (MaterialBlendMode).
    bool face_blends{};
    // The colour filter, applied to each finished pixel (E9.6).
    bool filtered{};
    MaterialColourMatrix filter{};
    bool border{};
    Color border_colour{};
    Rect border_inner{};
    PartShape border_inner_shape{};
    BorderPath border_path{};
    // The border's single edges, then the keylines.
    std::array<ResolvedBand, maximum_bands> bands{};
    std::size_t band_count{};
    // An alpha mask (E9.6), evaluated at each pixel centre moved into the
    // box, so that outside the box it extends at its edge value; an image
    // mask's variant is MaterialImages' mask_layer.
    bool masked{};
    ResolvedFill mask{};
};

// An image mask's variant slot (E9.6), after the fills'.
constexpr std::size_t mask_layer = image_mask_variant_layer;
static_assert(mask_layer == SurfaceMaterial::maximum_fill_layers, "one variant per fill, then the mask's");

// The material a bake draws (E9.6): for a finish above 0, the material it
// describes (finished_surface_material), made into `storage`, which the
// caller keeps through its use; any other material as it is. Storage that
// cannot be allocated leaves the material drawn unfinished: the bakes are
// noexcept and still write every pixel.
const SurfaceMaterial& baked_material(const SurfaceMaterial& material,
                                      std::optional<SurfaceMaterial>& storage) noexcept {
    if (!(material.finish > 0.0)) return material;
    try {
        storage = finished_surface_material(material);
    } catch (const std::bad_alloc&) {
        return material;
    }
    return *storage;
}

double inverse_sigma_of(double blur, double scale) noexcept {
    const double sigma = blur * 0.5 * scale;
    // The pixel's own box filter adds a variance of 1/12.
    const double widened = std::sqrt(sigma * sigma + 1.0 / 12.0);
    const double inverse = 1.0 / widened;
    return inverse;
}

PartShape rounded(double radius) noexcept {
    PartShape shape{};
    shape.kind = PartShapeKind::rounded_rectangle;
    shape.corner_radius = radius;
    return shape;
}

Rect material_box(const PrimitiveTarget& target, double outset) noexcept {
    const double scale = target.scale.value;
    const Rect box{outset * scale, outset * scale, target.logical_size.width * scale,
                   target.logical_size.height * scale};
    return box;
}

double material_radius(const SurfaceMaterial& material, const PrimitiveTarget& target) noexcept {
    const double logical = target.corner_radius ? std::max(0.0, *target.corner_radius) : material.corner_radius;
    const double radius = logical * target.scale.value;
    return radius;
}

// The material's outline in device pixels (E9.6). A uniform outline is
// the rounded rectangle of one radius that every material had before E9.6;
// its layers keep that arithmetic exactly. Otherwise it is the parameter's
// shape: a rounded rectangle with the material's four corners, a pill, a
// circle or a capped end.
struct MaterialOutline final {
    PartShapeKind kind{PartShapeKind::rounded_rectangle};
    bool uniform{true};
    double radius{};
    PartCornerRadii corners{};
};

MaterialOutline material_outline(const SurfaceMaterial& material, const PrimitiveTarget& target,
                                 PartShapeKind kind) noexcept {
    MaterialOutline outline{};
    outline.kind = kind;
    outline.radius = material_radius(material, target);
    // A part's own radius overrides the material's corners.
    const bool own_radius = target.corner_radius.has_value();
    if (kind == PartShapeKind::rounded_rectangle && (own_radius || !material.corner_radii)) return outline;
    outline.uniform = false;
    if (kind == PartShapeKind::rounded_rectangle) {
        const double scale = target.scale.value;
        const MaterialCornerRadii& corners = *material.corner_radii;
        outline.corners = PartCornerRadii{corners.top_left * scale, corners.top_right * scale,
                                          corners.bottom_right * scale, corners.bottom_left * scale};
    }
    return outline;
}

// The outline drawn in `rect`, its corners grown by `by` device pixels (a
// negative `by` shrinks them; a pill's, a circle's and a cap's follow the
// rectangle), and the largest of its drawn corners' radii. A circle's
// largest radius is half the rectangle's longer side, so that every pixel
// beyond its straight parts (there are none) is measured.
struct GrownOutline final {
    PartShape shape{};
    double radius{};
};

GrownOutline grown(const MaterialOutline& outline, Rect rect, double by) noexcept {
    GrownOutline result{};
    if (outline.uniform) {
        result.radius = std::max(0.0, outline.radius + by);
        result.shape = rounded(result.radius);
        return result;
    }
    result.shape.kind = outline.kind;
    if (outline.kind == PartShapeKind::circle) {
        result.radius = std::max(rect.width, rect.height) * 0.5;
        return result;
    }
    if (outline.kind == PartShapeKind::rounded_rectangle) {
        const PartCornerRadii& corners = outline.corners;
        result.shape.per_corner = true;
        result.shape.corners =
            PartCornerRadii{std::max(0.0, corners.top_left + by), std::max(0.0, corners.top_right + by),
                            std::max(0.0, corners.bottom_right + by), std::max(0.0, corners.bottom_left + by)};
    }
    const PartCornerRadii drawn = part_corner_radii(result.shape, rect);
    const double top = std::max(drawn.top_left, drawn.top_right);
    const double bottom = std::max(drawn.bottom_right, drawn.bottom_left);
    result.radius = std::max(top, bottom);
    return result;
}

Point resolved_point(Point point, MaterialCoordinateSpace space, Rect box, double scale) noexcept {
    if (space == MaterialCoordinateSpace::normalized) {
        const Point normalized{box.x + point.x * box.width, box.y + point.y * box.height};
        return normalized;
    }
    const Point logical{box.x + point.x * scale, box.y + point.y * scale};
    return logical;
}

// A CSS angle's unit direction: 0 degrees points up, 90 right (as
// resolve_css_linear_gradient, with the engine's sine and cosine).
Point css_direction(double angle_degrees) noexcept {
    double angle = std::fmod(angle_degrees, 360.0);
    if (angle < 0.0) angle = angle + 360.0;
    if (angle == 0.0 || angle == 360.0) return Point{0.0, -1.0};
    if (angle == 90.0) return Point{1.0, 0.0};
    if (angle == 180.0) return Point{0.0, 1.0};
    if (angle == 270.0) return Point{-1.0, 0.0};
    constexpr double degrees_to_radians = 3.14159265358979323846264338327950288 / 180.0;
    const EngineSineCosine turned = engine_sine_cosine(angle * degrees_to_radians);
    const Point direction{turned.sine, -turned.cosine};
    return direction;
}

ResolvedFill resolve_fill(const MaterialFillLayer& layer, Rect box, double scale) noexcept {
    ResolvedFill fill{};
    fill.kind = layer.kind;
    fill.colour = layer.color;
    fill.spread = layer.spread;
    fill.blend = layer.blend;
    fill.stops = std::span<const GradientStop>(layer.stops.data(), layer.stops.size());
    fill.opacity = layer.opacity;
    // A procedural fill draws its ground until its pattern is lent (E10).
    if (layer.kind == MaterialFillKind::procedural) fill.colour = layer.procedural.ground;
    if (layer.kind == MaterialFillKind::linear_gradient) {
        Point start{};
        Point end{};
        if (layer.linear_geometry == MaterialLinearGeometry::css_angle) {
            const Point direction = css_direction(layer.angle_degrees);
            const Point centre{box.x + box.width * 0.5, box.y + box.height * 0.5};
            const double half = (std::abs(direction.x) * box.width + std::abs(direction.y) * box.height) * 0.5;
            start = Point{centre.x - direction.x * half, centre.y - direction.y * half};
            end = Point{centre.x + direction.x * half, centre.y + direction.y * half};
        } else {
            start = resolved_point(layer.start, layer.coordinate_space, box, scale);
            end = resolved_point(layer.end, layer.coordinate_space, box, scale);
        }
        const double across = end.x - start.x;
        const double down = end.y - start.y;
        const double length_squared = across * across + down * down;
        fill.origin_x = start.x;
        fill.origin_y = start.y;
        if (length_squared > 0.0) {
            fill.step_x = across / length_squared;
            fill.step_y = down / length_squared;
        }
    }
    if (layer.kind == MaterialFillKind::radial_gradient) {
        const Point centre = resolved_point(layer.center, layer.coordinate_space, box, scale);
        const bool normalized = layer.coordinate_space == MaterialCoordinateSpace::normalized;
        const double radius_x = normalized ? layer.radii.width * box.width : layer.radii.width * scale;
        const double radius_y = normalized ? layer.radii.height * box.height : layer.radii.height * scale;
        fill.origin_x = centre.x;
        fill.origin_y = centre.y;
        if (radius_x > 0.0) fill.inverse_radius_x = 1.0 / radius_x;
        if (radius_y > 0.0) fill.inverse_radius_y = 1.0 / radius_y;
    }
    if (layer.kind == MaterialFillKind::conic_gradient) {
        const Point centre = resolved_point(layer.center, layer.coordinate_space, box, scale);
        const double turns = layer.angle_degrees / 360.0;
        fill.origin_x = centre.x;
        fill.origin_y = centre.y;
        fill.from_turn = turns - std::floor(turns);
        fill.periods_per_turn = 360.0 / layer.period_degrees;
    }
    return fill;
}

void add_band(MaterialLook& look, Rect band, Color colour, bool clipped) noexcept {
    if (band.width <= 0.0 || band.height <= 0.0 || look.band_count == maximum_bands) return;
    look.bands[look.band_count] = ResolvedBand{band, colour, clipped};
    look.band_count = look.band_count + 1U;
}

// A keyline's band (as paint_material_keyline places it), in device pixels;
// none when the keyline does not fit across the box.
void add_keyline(MaterialLook& look, const MaterialKeyline& keyline, double scale, bool clipped) noexcept {
    const Rect box = look.box;
    const double inset = keyline.inset * scale;
    const double width = keyline.width * scale;
    const double reach = (inset + width * 0.5) * 2.0;
    switch (keyline.edge) {
    case MaterialEdge::top:
        if (reach > box.height) return;
        add_band(look, Rect{box.x + inset, box.y + inset, box.width - inset * 2.0, width}, keyline.color, clipped);
        return;
    case MaterialEdge::right:
        if (reach > box.width) return;
        add_band(look, Rect{box.right() - inset - width, box.y + inset, width, box.height - inset * 2.0},
                 keyline.color, clipped);
        return;
    case MaterialEdge::bottom:
        if (reach > box.height) return;
        add_band(look, Rect{box.x + inset, box.bottom() - inset - width, box.width - inset * 2.0, width},
                 keyline.color, clipped);
        return;
    case MaterialEdge::left:
        if (reach > box.width) return;
        add_band(look, Rect{box.x + inset, box.y + inset, width, box.height - inset * 2.0}, keyline.color, clipped);
        return;
    }
    // An edge outside the enumeration draws no keyline.
    return;
}

// A dashed or dotted border's path in `box`, `width` device pixels wide; a
// solid one where the line has no length.
BorderPath border_path(MaterialBorderStyle style, const MaterialOutline& outline, Rect box, double width) noexcept {
    BorderPath path{};
    if (style == MaterialBorderStyle::solid) return path;
    const double half = width * 0.5;
    path.line = Rect{box.x + half, box.y + half, box.width - width, box.height - width};
    if (path.line.width <= 0.0 || path.line.height <= 0.0) return path;
    double length = 0.0;
    if (outline.kind == PartShapeKind::circle && !outline.uniform) {
        path.circle = true;
        length = std::min(path.line.width, path.line.height) * 0.5 * quarter_turn * 4.0;
    } else {
        const GrownOutline line = grown(outline, path.line, -half);
        const PartCornerRadii radii = part_corner_radii(line.shape, path.line);
        const Rect& at = path.line;
        path.radii = radii;
        path.starts[1] = (at.right() - radii.top_right) - (at.x + radii.top_left);
        path.starts[2] = path.starts[1] + quarter_turn * radii.top_right;
        path.starts[3] = path.starts[2] + (at.bottom() - radii.bottom_right) - (at.y + radii.top_right);
        path.starts[4] = path.starts[3] + quarter_turn * radii.bottom_right;
        path.starts[5] = path.starts[4] + (at.right() - radii.bottom_right) - (at.x + radii.bottom_left);
        path.starts[6] = path.starts[5] + quarter_turn * radii.bottom_left;
        path.starts[7] = path.starts[6] + (at.bottom() - radii.bottom_left) - (at.y + radii.top_left);
        length = path.starts[7] + quarter_turn * radii.top_left;
    }
    // A dash of three widths and a gap of two; a dot one width across, one
    // every two widths.
    const bool dashed = style == MaterialBorderStyle::dashed;
    const double natural = dashed ? width * 5.0 : width * 2.0;
    const double count = std::max(1.0, std::floor(length / natural + 0.5));
    path.style = style;
    path.width = width;
    path.period = length / count;
    path.dash = dashed ? path.period * 0.6 : 0.0;
    return path;
}

// The look with the box's top-left corner at `box_origin`, in the
// destination's device pixels.
MaterialLook resolve_look_at(const SurfaceMaterial& material, const PrimitiveTarget& target,
                             Point box_origin, PartShapeKind kind) noexcept {
    const double scale = target.scale.value;
    MaterialLook look{};
    look.box = Rect{box_origin.x, box_origin.y, target.logical_size.width * scale,
                    target.logical_size.height * scale};
    const MaterialOutline outline = material_outline(material, target, kind);
    const GrownOutline drawn = grown(outline, look.box, 0.0);
    const double radius = drawn.radius;
    look.box_shape = drawn.shape;
    const Rect box = look.box;
    // A centre at least half a pixel inside the straight edges, and clear of
    // the corners' radius along the other axis, has a signed distance of at
    // most −0.5, where shape_coverage gives exactly 1.
    const double clear = std::max(radius, 0.5);
    look.wide_interior = Rect{box.x + 0.5, box.y + clear, box.width - 1.0, box.height - clear * 2.0};
    look.tall_interior = Rect{box.x + clear, box.y + 0.5, box.width - clear * 2.0, box.height - 1.0};
    for (const MaterialShadow& shadow : material.shadows) {
        const double spread = shadow.spread * scale;
        const double offset_x = shadow.offset.x * scale;
        const double offset_y = shadow.offset.y * scale;
        ResolvedShadow resolved{};
        resolved.inverse_sigma = inverse_sigma_of(shadow.blur_radius, scale);
        resolved.colour = shadow.color;
        if (!shadow.inset) {
            resolved.shape = Rect{box.x + offset_x - spread, box.y + offset_y - spread, box.width + spread * 2.0,
                                  box.height + spread * 2.0};
            const GrownOutline grown_outline = grown(outline, resolved.shape, spread);
            resolved.outline = grown_outline.shape;
            resolved.radius = grown_outline.radius;
            if (resolved.shape.width <= 0.0 || resolved.shape.height <= 0.0) continue;
            look.outer[look.outer_count] = resolved;
            look.outer_count = look.outer_count + 1U;
            continue;
        }
        resolved.shape = Rect{box.x + offset_x + spread, box.y + offset_y + spread, box.width - spread * 2.0,
                              box.height - spread * 2.0};
        const GrownOutline hole = grown(outline, resolved.shape, -spread);
        resolved.outline = hole.shape;
        resolved.radius = hole.radius;
        resolved.solid = resolved.shape.width <= 0.0 || resolved.shape.height <= 0.0;
        look.inset[look.inset_count] = resolved;
        look.inset_count = look.inset_count + 1U;
    }
    for (const MaterialGlow& glow : material.glows) {
        const double spread = glow.spread * scale;
        ResolvedShadow resolved{};
        resolved.inverse_sigma = inverse_sigma_of(glow.blur_radius, scale);
        resolved.colour = glow.color;
        resolved.additive = true;
        resolved.over = glow.placement == MaterialGlowPlacement::over;
        resolved.shape = Rect{box.x - spread, box.y - spread, box.width + spread * 2.0, box.height + spread * 2.0};
        // A uniform glow of negative spread may have a negative radius: as
        // zero, it bounds nothing (outer_coverage).
        const GrownOutline grown_outline = grown(outline, resolved.shape, spread);
        resolved.outline = grown_outline.shape;
        resolved.radius = outline.uniform ? radius + spread : grown_outline.radius;
        if (resolved.shape.width <= 0.0 || resolved.shape.height <= 0.0) continue;
        look.outer[look.outer_count] = resolved;
        look.outer_count = look.outer_count + 1U;
    }
    for (std::size_t index = 0U; index < material.fills.size(); ++index) {
        const MaterialFillLayer& layer = material.fills[index];
        if (layer.kind == MaterialFillKind::solid && layer.color.alpha == 0U) continue;
        look.fills[look.fill_count] = resolve_fill(layer, box, scale);
        look.fills[look.fill_count].image = index;
        look.fill_count = look.fill_count + 1U;
        if (layer.blend != MaterialBlendMode::normal) look.face_blends = true;
    }
    if (material.mask) {
        look.mask = resolve_fill(*material.mask, box, scale);
        look.mask.image = mask_layer;
        look.masked = true;
    }
    if (material.border) {
        const double width = (*material.border).width * scale;
        look.border = true;
        look.border_colour = (*material.border).color;
        look.border_inner = Rect{box.x + width, box.y + width, std::max(0.0, box.width - width * 2.0),
                                 std::max(0.0, box.height - width * 2.0)};
        look.border_inner_shape = grown(outline, look.border_inner, -width).shape;
        look.border_path = border_path((*material.border).style, outline, box, width);
    }
    const MaterialBorderEdges& edges = material.border_edges;
    if (edges.top) {
        const double width = (*edges.top).width * scale;
        add_band(look, Rect{box.x, box.y, box.width, width}, (*edges.top).color, false);
    }
    if (edges.right) {
        const double width = (*edges.right).width * scale;
        add_band(look, Rect{box.right() - width, box.y, width, box.height}, (*edges.right).color, false);
    }
    if (edges.bottom) {
        const double width = (*edges.bottom).width * scale;
        add_band(look, Rect{box.x, box.bottom() - width, box.width, width}, (*edges.bottom).color, false);
    }
    if (edges.left) {
        const double width = (*edges.left).width * scale;
        add_band(look, Rect{box.x, box.y, width, box.height}, (*edges.left).color, false);
    }
    const bool clip_keylines = radius > 0.0;
    for (const MaterialKeyline& keyline : material.keylines) add_keyline(look, keyline, scale, clip_keylines);
    if (material.colour_filter) {
        look.filtered = true;
        look.filter = *material.colour_filter;
    }
    bool over_glows = false;
    for (std::size_t index = 0U; index < look.outer_count; ++index) {
        if (look.outer[index].over) over_glows = true;
    }
    look.reads_inside = look.fill_count > 0U || look.inset_count > 0U || look.border || over_glows;
    for (std::size_t index = 0U; index < look.band_count; ++index) {
        if (look.bands[index].clipped) look.reads_inside = true;
    }
    return look;
}

MaterialLook resolve_look(const SurfaceMaterial& material, const PrimitiveTarget& target, double outset,
                          PartShapeKind kind) noexcept {
    const Rect box = material_box(target, outset);
    const MaterialLook look = resolve_look_at(material, target, Point{box.x, box.y}, kind);
    return look;
}

// -- image fills -------------------------------------------------------------------

// Each image fill's lent variant and where its pixels show, by fill index:
// for destination column x and row y, the variant's byte at rows[y] +
// columns[x], or none where either is outside_offset (beyond a single
// image's tile). `variant` is null for a fill that is not an image, or whose
// variant was not lent or whose maps could not be stored (it then draws
// nothing). Borrowed from the target for the bake.
// The offset of a column or row that shows no image.
constexpr std::size_t outside_offset = std::numeric_limits<std::size_t>::max();

// A variant pixel index as a byte offset at `stride` bytes per index.
std::size_t image_offset(std::uint32_t index, std::size_t stride) noexcept {
    if (index == outside_image) return outside_offset;
    const std::size_t offset = static_cast<std::size_t>(index) * stride;
    return offset;
}

struct MaterialImageLayer final {
    const PartRaster* variant{};
    std::vector<std::size_t> columns;
    std::vector<std::size_t> rows;
};

struct MaterialImages final {
    std::array<MaterialImageLayer, maximum_image_variants> layers{};
};

// Maps a procedural fill's lent pattern onto the destination (E10): a
// tile repeats from the box's first device pixel, one tile pixel to one
// destination pixel; the fresco is stretched over the box as an image is.
// A pattern made for another extent is not composited.
void map_pattern(const ProceduralFill& pattern, Rect box, double scale, const PartRaster& variant,
                 DeviceExtent extent, MaterialImageLayer& mapped) noexcept {
    const DeviceExtent wanted = procedural_extent(pattern, box, scale);
    if (!(variant.extent() == wanted)) return;
    const bool stretched = pattern.pattern == ProceduralPattern::fresco;
    try {
        std::vector<std::uint32_t> map{};
        if (stretched) {
            ImageAxisPlacement across{};
            across.start = box.x;
            across.length = box.width;
            across.source_length = wanted.width;
            map_image_variant(across, wanted.width, 0U, 0U, extent.width, map);
        } else {
            map_procedural_tile(box.x, wanted.width, extent.width, map);
        }
        mapped.columns.resize(map.size());
        for (std::size_t column = 0U; column < map.size(); ++column) {
            mapped.columns[column] = static_cast<std::size_t>(map[column]) * part_raster_bytes_per_pixel;
        }
        if (stretched) {
            ImageAxisPlacement down{};
            down.start = box.y;
            down.length = box.height;
            down.source_length = wanted.height;
            map_image_variant(down, wanted.height, 0U, 0U, extent.height, map);
        } else {
            map_procedural_tile(box.y, wanted.height, extent.height, map);
        }
        mapped.rows.resize(map.size());
        for (std::size_t row = 0U; row < map.size(); ++row) {
            mapped.rows[row] = static_cast<std::size_t>(map[row]) * variant.row_bytes();
        }
        mapped.variant = &variant;
    } catch (const std::bad_alloc&) {
        mapped = MaterialImageLayer{};
    }
}

// Maps image layer `layer`'s lent variant, the variant slot `index`'s,
// onto the destination. A variant made for another size is not mapped, and
// storage that cannot be allocated leaves `mapped` without a variant.
void map_image(const MaterialFillLayer& layer, std::size_t index, Rect box, double scale, const PartRaster& variant,
               DeviceExtent extent, MaterialImageLayer& mapped) noexcept {
    const ImageVariantRequest wanted = image_variant_request(layer, index, box, scale);
    if (!(variant.extent() == wanted.extent)) return;
    try {
        std::vector<std::uint32_t> map{};
        const ImageAxisPlacement across =
            image_placement_across(layer, box.x, box.width, scale, layer.image_pixel_size.width);
        map_image_variant(across, wanted.extent.width, wanted.fixed[0], wanted.fixed[2], extent.width, map);
        mapped.columns.resize(map.size());
        for (std::size_t column = 0U; column < map.size(); ++column) {
            mapped.columns[column] = image_offset(map[column], part_raster_bytes_per_pixel);
        }
        const ImageAxisPlacement down =
            image_placement_down(layer, box.y, box.height, scale, layer.image_pixel_size.height);
        map_image_variant(down, wanted.extent.height, wanted.fixed[1], wanted.fixed[3], extent.height, map);
        mapped.rows.resize(map.size());
        for (std::size_t row = 0U; row < map.size(); ++row) {
            mapped.rows[row] = image_offset(map[row], variant.row_bytes());
        }
        mapped.variant = &variant;
    } catch (const std::bad_alloc&) {
        mapped = MaterialImageLayer{};
    }
}

// Gives a map's outputs whose centres lie before `start` the entry of the
// first output whose centre does not, and those past `end` the last such
// output's: a mask extends outward at its edge value (E9.6).
void extend_at_box(std::vector<std::size_t>& map, double start, double end) noexcept {
    if (map.empty()) return;
    const double last_output = static_cast<double>(map.size() - 1U);
    const double first = std::clamp(std::ceil(start - 0.5), 0.0, last_output);
    const double last = std::clamp(std::floor(end - 0.5), 0.0, last_output);
    if (first > last) return;
    const std::size_t first_index = static_cast<std::size_t>(first);
    const std::size_t last_index = static_cast<std::size_t>(last);
    const std::size_t first_entry = map[first_index];
    const std::size_t last_entry = map[last_index];
    for (std::size_t output = 0U; output < first_index; ++output) map[output] = first_entry;
    for (std::size_t output = last_index + 1U; output < map.size(); ++output) map[output] = last_entry;
}

// Maps each image fill's lent variant, each procedural fill's lent
// pattern and an image mask's lent variant onto the destination. Storage
// that cannot be allocated leaves that layer without a variant: fill() is
// noexcept and still writes every pixel.
void prepare_images(const SurfaceMaterial& material, const PrimitiveTarget& target, const MaterialLook& look,
                    DeviceExtent extent, MaterialImages& images) noexcept {
    if (target.variants == nullptr) return;
    const double scale = target.scale.value;
    for (std::size_t index = 0U; index < material.fills.size(); ++index) {
        const MaterialFillLayer& layer = material.fills[index];
        if (layer.kind != MaterialFillKind::image && layer.kind != MaterialFillKind::procedural) continue;
        const std::shared_ptr<const PartRaster>& lent = (*target.variants).layers[index];
        if (!lent) continue;
        const PartRaster& variant = *lent;
        if (layer.kind == MaterialFillKind::procedural) {
            map_pattern(layer.procedural, look.box, scale, variant, extent, images.layers[index]);
            continue;
        }
        map_image(layer, index, look.box, scale, variant, extent, images.layers[index]);
    }
    if (!material.mask || (*material.mask).kind != MaterialFillKind::image) return;
    const std::shared_ptr<const PartRaster>& lent_mask = (*target.variants).layers[mask_layer];
    if (!lent_mask) return;
    MaterialImageLayer& mask = images.layers[mask_layer];
    map_image(*material.mask, mask_layer, look.box, scale, *lent_mask, extent, mask);
    if (mask.variant == nullptr) return;
    extend_at_box(mask.columns, look.box.x, look.box.right());
    extend_at_box(mask.rows, look.box.y, look.box.bottom());
}

// -- the pixel ---------------------------------------------------------------------

// A shadow's coverage is separable but for its rounded corners: the blurred
// box's integral across, which depends only on the pixel's column, times
// its integral down, which depends only on its row, bounded by the blurred
// distance to the rounded shape.
double across_integral(const ResolvedShadow& shadow, double centre_x) noexcept {
    const Rect& shape = shadow.shape;
    const double inverse = shadow.inverse_sigma;
    const double across =
        gaussian_integral((shape.right() - centre_x) * inverse) - gaussian_integral((shape.x - centre_x) * inverse);
    return across;
}

double down_integral(const ResolvedShadow& shadow, double centre_y) noexcept {
    const Rect& shape = shadow.shape;
    const double inverse = shadow.inverse_sigma;
    const double down =
        gaussian_integral((shape.bottom() - centre_y) * inverse) - gaussian_integral((shape.y - centre_y) * inverse);
    return down;
}

// The product bounded by the blurred distance to the rounded shape. A
// product of zero or less is returned as it is: the bound is never
// negative, so the minimum would be the product.
double rounded_bound(const ResolvedShadow& shadow, double product, double centre_x, double centre_y) noexcept {
    if (product <= 0.0) return product;
    const double distance = shape_signed_distance(shadow.outline, shadow.shape, Point{centre_x, centre_y});
    const double edge = gaussian_integral(-distance * shadow.inverse_sigma);
    const double coverage = std::min(product, edge);
    return coverage;
}

// An inset shadow's hole: the product, bounded everywhere when rounded.
double blurred_coverage(const ResolvedShadow& shadow, double centre_x, double centre_y) noexcept {
    const double product = across_integral(shadow, centre_x) * down_integral(shadow, centre_y);
    if (shadow.radius <= 0.0) return product;
    const double coverage = rounded_bound(shadow, product, centre_x, centre_y);
    return coverage;
}

// Whether a centre lies beyond the straight part of the shape's edges
// along one axis: within `radius` of its start or end, or outside it. A
// rounded shape differs from its rectangle only where a centre is beyond
// both axes' straight parts (its corner squares, and outward from them).
// Elsewhere the rounded bound is the bound of the rectangle, a half-plane
// through its nearest edge, which a blurred rectangle never exceeds; so an
// outer shadow's coverage is the bare product there and only its corners
// evaluate the signed distance.
bool beyond_straight(double start, double end, double radius, double centre) noexcept {
    const bool beyond = centre < start + radius || centre > end - radius;
    return beyond;
}

double outer_coverage(const ResolvedShadow& shadow, double across, double down, bool corner_column, bool corner_row,
                      double centre_x, double centre_y) noexcept {
    const double product = across * down;
    if (shadow.radius <= 0.0 || !corner_column || !corner_row) return product;
    const double coverage = rounded_bound(shadow, product, centre_x, centre_y);
    return coverage;
}

// The outer shadows' separable terms for a bake: for every column of the
// destination (shadow-major, `width` per shadow) the integral across and
// whether it is a corner column, filled once per bake; for the row being
// baked the integral down and whether it is a corner row, filled once per
// row. Empty `across` (its storage could not be allocated) makes each pixel
// evaluate its terms itself, to the same bytes.
struct OuterIntegrals final {
    std::vector<double> across;
    std::vector<std::uint8_t> corner_columns;
    std::size_t width{};
    std::array<double, maximum_outer> down{};
    std::array<bool, maximum_outer> corner_rows{};
};

void fill_columns(const MaterialLook& look, std::size_t width, OuterIntegrals& integrals) noexcept {
    integrals.width = width;
    if (look.outer_count == 0U) return;
    try {
        integrals.across.assign(look.outer_count * width, 0.0);
        integrals.corner_columns.assign(look.outer_count * width, std::uint8_t{0U});
    } catch (...) {
        integrals.across.clear();
        integrals.corner_columns.clear();
        return;
    }
    for (std::size_t index = 0U; index < look.outer_count; ++index) {
        const ResolvedShadow& shadow = look.outer[index];
        const std::size_t first = index * width;
        for (std::size_t column = 0U; column < width; ++column) {
            const double centre_x = static_cast<double>(column) + 0.5;
            integrals.across[first + column] = across_integral(shadow, centre_x);
            const bool corner = beyond_straight(shadow.shape.x, shadow.shape.right(), shadow.radius, centre_x);
            integrals.corner_columns[first + column] = corner ? std::uint8_t{1U} : std::uint8_t{0U};
        }
    }
}

void fill_row(const MaterialLook& look, std::uint32_t row, OuterIntegrals& integrals) noexcept {
    const double centre_y = static_cast<double>(row) + 0.5;
    for (std::size_t index = 0U; index < look.outer_count; ++index) {
        const ResolvedShadow& shadow = look.outer[index];
        integrals.down[index] = down_integral(shadow, centre_y);
        integrals.corner_rows[index] = beyond_straight(shadow.shape.y, shadow.shape.bottom(), shadow.radius, centre_y);
    }
}

double spread_position(double t, GradientSpreadMode spread) noexcept {
    switch (spread) {
    case GradientSpreadMode::pad: {
        const double padded = std::clamp(t, 0.0, 1.0);
        return padded;
    }
    case GradientSpreadMode::repeat: {
        const double repeated = t - std::floor(t);
        return repeated;
    }
    case GradientSpreadMode::reflect: {
        const double period = t - 2.0 * std::floor(t * 0.5);
        const double reflected = period > 1.0 ? 2.0 - period : period;
        return reflected;
    }
    }
    return std::clamp(t, 0.0, 1.0);
}

// The gradient's colour at `t` in [0, 1], premultiplied and scaled by
// `coverage`; the stops are valid (valid_gradient_stops: at least two, from
// 0 to 1, increasing). The stops are interpolated premultiplied, as CSS
// gradients and the Skia painter's are: a stop fading to transparent black
// fades the other stop's colour, never towards grey.
Unit stop_colour(std::span<const GradientStop> stops, double t, double coverage) noexcept {
    std::size_t next = 1U;
    while (next + 1U < stops.size() && t > stops[next].offset) next = next + 1U;
    const GradientStop& low = stops[next - 1U];
    const GradientStop& high = stops[next];
    const double span = high.offset - low.offset;
    const double f = std::clamp((t - low.offset) / span, 0.0, 1.0);
    const Unit a = premultiply(low.color, coverage);
    const Unit b = premultiply(high.color, coverage);
    const Unit colour{a.red + (b.red - a.red) * f, a.green + (b.green - a.green) * f,
                      a.blue + (b.blue - a.blue) * f, a.alpha + (b.alpha - a.alpha) * f};
    return colour;
}

Unit fill_colour(const ResolvedFill& fill, const MaterialImages& images, std::size_t x, std::size_t y,
                 double centre_x, double centre_y, double coverage) noexcept {
    switch (fill.kind) {
    case MaterialFillKind::solid: {
        const Unit solid = premultiply(fill.colour, coverage);
        return solid;
    }
    case MaterialFillKind::linear_gradient: {
        const double t = (centre_x - fill.origin_x) * fill.step_x + (centre_y - fill.origin_y) * fill.step_y;
        const Unit linear = stop_colour(fill.stops, spread_position(t, fill.spread), coverage);
        return linear;
    }
    case MaterialFillKind::radial_gradient: {
        const double across = (centre_x - fill.origin_x) * fill.inverse_radius_x;
        const double down = (centre_y - fill.origin_y) * fill.inverse_radius_y;
        const double t = std::sqrt(across * across + down * down);
        const Unit radial = stop_colour(fill.stops, spread_position(t, fill.spread), coverage);
        return radial;
    }
    case MaterialFillKind::conic_gradient: {
        // The angle clockwise from up, with y growing downward, as a turn
        // past the from-angle in [0, 1).
        constexpr double inverse_whole_turn = 1.0 / (2.0 * 3.14159265358979323846264338327950288);
        const double angle = engine_arctangent2(centre_x - fill.origin_x, fill.origin_y - centre_y);
        const double turn = angle * inverse_whole_turn - fill.from_turn;
        const double past = turn - std::floor(turn);
        const double t = past * fill.periods_per_turn;
        const Unit conic = stop_colour(fill.stops, spread_position(t, fill.spread), coverage);
        return conic;
    }
    case MaterialFillKind::image: {
        const MaterialImageLayer& layer = images.layers[fill.image];
        if (layer.variant == nullptr) return Unit{};
        if (layer.rows[y] == outside_offset || layer.columns[x] == outside_offset) return Unit{};
        const std::span<const std::uint8_t> pixels = (*layer.variant).pixels();
        const std::size_t at = layer.rows[y] + layer.columns[x];
        // The variant's bytes to [0, 1], weighted by coverage and opacity.
        const double weight = coverage * fill.opacity / 255.0;
        const Unit image{static_cast<double>(pixels[at + 2U]) * weight, static_cast<double>(pixels[at + 1U]) * weight,
                         static_cast<double>(pixels[at]) * weight, static_cast<double>(pixels[at + 3U]) * weight};
        return image;
    }
    case MaterialFillKind::procedural: {
        const MaterialImageLayer& layer = images.layers[fill.image];
        // Not ready: the ground, as a solid (E10).
        if (layer.variant == nullptr) {
            const Unit ground = premultiply(fill.colour, coverage * fill.opacity);
            return ground;
        }
        const std::span<const std::uint8_t> pixels = (*layer.variant).pixels();
        const std::size_t at = layer.rows[y] + layer.columns[x];
        const double weight = coverage * fill.opacity / 255.0;
        const Unit pattern{static_cast<double>(pixels[at + 2U]) * weight, static_cast<double>(pixels[at + 1U]) * weight,
                           static_cast<double>(pixels[at]) * weight,
                           static_cast<double>(pixels[at + 3U]) * weight};
        return pattern;
    }
    }
    return Unit{};
}

// Adds `light` to `target`, each premultiplied channel at most 1: a glow
// brightens the shadow beneath it instead of covering it.
void add_light(Unit& target, Unit light) noexcept {
    target.red = std::min(target.red + light.red, 1.0);
    target.green = std::min(target.green + light.green, 1.0);
    target.blue = std::min(target.blue + light.blue, 1.0);
    target.alpha = std::min(target.alpha + light.alpha, 1.0);
}

bool centre_within(Rect band, double x, double y) noexcept {
    const bool within = x >= band.x && x <= band.right() && y >= band.y && y <= band.bottom();
    return within;
}

// The arc length along a border's path (BorderPath) of the point on it
// nearest to (x, y).
double path_position(const BorderPath& path, double x, double y) noexcept {
    const Rect& line = path.line;
    const double dx = x - (line.x + line.width * 0.5);
    const double dy = y - (line.y + line.height * 0.5);
    if (path.circle) {
        // Clockwise from up, with y growing downward.
        const double angle = engine_arctangent2(dx, -dy);
        const double turned = angle < 0.0 ? angle + quarter_turn * 4.0 : angle;
        const double position = turned * std::min(line.width, line.height) * 0.5;
        return position;
    }
    const PartCornerRadii& radii = path.radii;
    const bool left = dx < 0.0;
    const bool top = dy < 0.0;
    double radius = 0.0;
    if (left) {
        radius = top ? radii.top_left : radii.bottom_left;
    } else {
        radius = top ? radii.top_right : radii.bottom_right;
    }
    const double qx = std::fabs(dx) - (line.width * 0.5 - radius);
    const double qy = std::fabs(dy) - (line.height * 0.5 - radius);
    if (qx > 0.0 && qy > 0.0) {
        // On the quadrant's arc: the angle about its centre, clockwise from
        // where the arc begins.
        const double ax = x - (left ? line.x + radius : line.right() - radius);
        const double ay = y - (top ? line.y + radius : line.bottom() - radius);
        double angle = 0.0;
        std::size_t piece = 0U;
        if (top && !left) {
            angle = engine_arctangent2(ax, -ay);
            piece = 1U;
        } else if (!top && !left) {
            angle = engine_arctangent2(ay, ax);
            piece = 3U;
        } else if (!top && left) {
            angle = engine_arctangent2(-ax, ay);
            piece = 5U;
        } else {
            angle = engine_arctangent2(-ay, -ax);
            piece = 7U;
        }
        const double position = path.starts[piece] + std::clamp(angle, 0.0, quarter_turn) * radius;
        return position;
    }
    if (qy >= qx) {
        const double along_top = x - (line.x + radii.top_left);
        const double along_bottom = path.starts[4] + (line.right() - radii.bottom_right) - x;
        const double position = top ? along_top : along_bottom;
        return position;
    }
    const double along_right = path.starts[2] + y - (line.y + radii.top_right);
    const double along_left = path.starts[6] + (line.bottom() - radii.bottom_left) - y;
    const double position = left ? along_left : along_right;
    return position;
}

// How much of a border's ring, covering `ring` of pixel (x, y), its dashes
// or dots cover: a dash by the pixel's span along the path (one device
// pixel) within it, a dot by the pixel's distance from its centre.
double patterned_ring(const MaterialLook& look, double ring, double x, double y) noexcept {
    const BorderPath& path = look.border_path;
    const double position = path_position(path, x, y);
    const double phase = position - std::floor(position / path.period) * path.period;
    if (path.style == MaterialBorderStyle::dashed) {
        // This dash, and the next one where the pixel spans the period's end.
        const double here = interval_overlap(phase - 0.5, 0.0, path.dash);
        const double next = interval_overlap(phase - 0.5, path.period, path.period + path.dash);
        const double covered = std::min(1.0, here + next) * ring;
        return covered;
    }
    const double along = phase - path.period * 0.5;
    const double across = shape_signed_distance(look.box_shape, look.box, Point{x, y}) + path.width * 0.5;
    const double distance = std::sqrt(along * along + across * across) - path.width * 0.5;
    const double dot = std::clamp(0.5 - distance, 0.0, 1.0);
    const double covered = std::min(dot, ring);
    return covered;
}

// One fill layer joined to the face beneath it by its blend mode.
void join_layer(Unit& face, Unit layer, MaterialBlendMode blend) noexcept {
    switch (blend) {
    case MaterialBlendMode::normal:
        blend_over(face, layer);
        return;
    case MaterialBlendMode::multiply:
        blend_multiply(face, layer);
        return;
    case MaterialBlendMode::screen:
        blend_screen(face, layer);
        return;
    }
    // A mode outside the enumeration is drawn source-over.
    blend_over(face, layer);
}

// A finished pixel through the material's colour filter, if it has one.
Unit finished(const MaterialLook& look, Unit pixel) noexcept {
    if (!look.filtered) return pixel;
    const Unit filtered = colour_filtered(look.filter, pixel);
    return filtered;
}

// How much of pixel (x, y) the box covers.
double box_coverage(const MaterialLook& look, std::int64_t x, std::int64_t y, double centre_x,
                    double centre_y) noexcept {
    if (centre_within(look.wide_interior, centre_x, centre_y) || centre_within(look.tall_interior, centre_x, centre_y)) {
        return 1.0;
    }
    const double coverage = shape_coverage(look.box_shape, look.box, 1.0, x, y);
    return coverage;
}

Unit material_pixel(const MaterialLook& look, const MaterialImages& images, const OuterIntegrals& integrals,
                    std::int64_t x, std::int64_t y) noexcept {
    Unit result{};
    const double centre_x = static_cast<double>(x) + 0.5;
    const double centre_y = static_cast<double>(y) + 0.5;
    const bool cached = !integrals.across.empty();
    // The over glows' coverage, by their index among the outer layers.
    std::array<double, maximum_outer> over_coverage{};
    for (std::size_t index = 0U; index < look.outer_count; ++index) {
        const ResolvedShadow& shadow = look.outer[index];
        double coverage = 0.0;
        if (cached) {
            const std::size_t at = index * integrals.width + static_cast<std::size_t>(x);
            coverage = outer_coverage(shadow, integrals.across[at], integrals.down[index],
                                      integrals.corner_columns[at] != 0U, integrals.corner_rows[index], centre_x,
                                      centre_y);
        } else {
            const Rect& shape = shadow.shape;
            const bool corner_column = beyond_straight(shape.x, shape.right(), shadow.radius, centre_x);
            const bool corner_row = beyond_straight(shape.y, shape.bottom(), shadow.radius, centre_y);
            coverage = outer_coverage(shadow, across_integral(shadow, centre_x), down_integral(shadow, centre_y),
                                      corner_column, corner_row, centre_x, centre_y);
        }
        if (shadow.over) {
            over_coverage[index] = coverage;
        } else if (shadow.additive) {
            add_light(result, premultiply(shadow.colour, coverage));
        } else {
            blend_over(result, premultiply(shadow.colour, coverage));
        }
    }
    if (!look.reads_inside) return finished(look, result);
    const double inside = box_coverage(look, x, y, centre_x, centre_y);
    if (inside > 0.0) {
        if (look.face_blends) {
            // The fills gathered into one face, then over the shadows.
            Unit face{};
            for (std::size_t index = 0U; index < look.fill_count; ++index) {
                const ResolvedFill& fill = look.fills[index];
                const Unit layer = fill_colour(fill, images, static_cast<std::size_t>(x),
                                               static_cast<std::size_t>(y), centre_x, centre_y, inside);
                join_layer(face, layer, fill.blend);
            }
            blend_over(result, face);
        } else {
            for (std::size_t index = 0U; index < look.fill_count; ++index) {
                blend_over(result, fill_colour(look.fills[index], images, static_cast<std::size_t>(x),
                                               static_cast<std::size_t>(y), centre_x, centre_y, inside));
            }
        }
        for (std::size_t index = 0U; index < look.inset_count; ++index) {
            const ResolvedShadow& shadow = look.inset[index];
            const double shaded = shadow.solid ? 1.0 : 1.0 - blurred_coverage(shadow, centre_x, centre_y);
            blend_over(result, premultiply(shadow.colour, shaded * inside));
        }
        if (look.border) {
            const double inner = shape_coverage(look.border_inner_shape, look.border_inner, 1.0, x, y);
            const double ring = std::max(0.0, inside - inner);
            const bool patterned = look.border_path.style != MaterialBorderStyle::solid && ring > 0.0;
            const double drawn = patterned ? patterned_ring(look, ring, centre_x, centre_y) : ring;
            blend_over(result, premultiply(look.border_colour, drawn));
        }
    }
    const double px = static_cast<double>(x);
    const double py = static_cast<double>(y);
    for (std::size_t index = 0U; index < look.band_count; ++index) {
        const ResolvedBand& band = look.bands[index];
        const double across = interval_overlap(px, band.band.x, band.band.right());
        const double down = interval_overlap(py, band.band.y, band.band.bottom());
        const double clip = band.clipped ? inside : 1.0;
        blend_over(result, premultiply(band.colour, across * down * clip));
    }
    // Over glows: light added to the finished face, within the box.
    if (inside > 0.0) {
        for (std::size_t index = 0U; index < look.outer_count; ++index) {
            const ResolvedShadow& shadow = look.outer[index];
            if (!shadow.over) continue;
            add_light(result, premultiply(shadow.colour, over_coverage[index] * inside));
        }
    }
    return finished(look, result);
}

// -- the part filters (E9.6) ----------------------------------------------------------

// The drop shadow's and the blur's box halves (box_blur_half) and the
// shadow's offset, in device pixels. The offset is rounded to whole pixels.
struct PartFilters final {
    bool shadow{};
    std::uint32_t shadow_half{};
    std::int64_t shadow_x{};
    std::int64_t shadow_y{};
    Color shadow_colour{};
    std::uint32_t blur_half{};
};

PartFilters part_filters(const SurfaceMaterial& material, double scale) noexcept {
    PartFilters filters{};
    if (material.drop_shadow) {
        const MaterialDropShadow& shadow = *material.drop_shadow;
        filters.shadow = true;
        filters.shadow_half = box_blur_half(shadow.blur_radius, scale);
        filters.shadow_x = static_cast<std::int64_t>(std::floor(shadow.offset.x * scale + 0.5));
        filters.shadow_y = static_cast<std::int64_t>(std::floor(shadow.offset.y * scale + 0.5));
        filters.shadow_colour = shadow.color;
    }
    filters.blur_half = box_blur_half(material.filter_blur, scale);
    return filters;
}

// How far, in device pixels, the filters carry a change of the layers
// beneath them: three box halves per blur, and the shadow's offset.
double filter_reach_pixels(const PartFilters& filters) noexcept {
    double reach = static_cast<double>(filters.blur_half) * 3.0;
    if (filters.shadow) {
        const double moved = static_cast<double>(std::max(std::abs(filters.shadow_x), std::abs(filters.shadow_y)));
        reach = reach + moved + static_cast<double>(filters.shadow_half) * 3.0;
    }
    return reach;
}

// The destination's pixels copied into `grown`, an image grown by `margin`
// transparent pixels on every side: every channel, four lanes a pixel, or
// with `lanes` 1 the alpha alone.
void load_grown(const PartRasterView& destination, std::size_t margin, std::size_t lanes,
                std::span<std::uint32_t> grown) noexcept {
    const std::size_t width = destination.extent.width;
    const std::size_t height = destination.extent.height;
    const std::size_t stride = (width + margin * 2U) * lanes;
    const std::size_t skipped = part_raster_bytes_per_pixel - lanes;
    for (std::size_t row = 0U; row < height; ++row) {
        const std::size_t read = row * destination.row_bytes;
        const std::size_t write = (row + margin) * stride + margin * lanes;
        for (std::size_t column = 0U; column < width; ++column) {
            const std::size_t from = read + column * part_raster_bytes_per_pixel + skipped;
            const std::size_t to = write + column * lanes;
            for (std::size_t lane = 0U; lane < lanes; ++lane) grown[to + lane] = destination.pixels[from + lane];
        }
    }
}

// Prepares `work` for a blur of `half` over the destination, `lanes` lanes
// a pixel; false when its storage cannot be held.
bool make_room(const PartRasterView& destination, std::uint32_t half, std::size_t lanes, BoxBlurWork& work) noexcept {
    const std::size_t margin = static_cast<std::size_t>(half) * 3U;
    const std::size_t columns = static_cast<std::size_t>(destination.extent.width) + margin * 2U;
    const std::size_t rows = static_cast<std::size_t>(destination.extent.height) + margin * 2U;
    try {
        prepare_box_blur(work, columns, rows, lanes);
    } catch (const std::exception&) {
        // std::bad_alloc or std::length_error: the step is left undone.
        return false;
    }
    return true;
}

// Draws the drop shadow beneath the destination's pixels: their alpha,
// blurred, moved by the offset, in the shadow's colour.
void add_drop_shadow(const PartFilters& filters, const PartRasterView& destination, BoxBlurWork& work) noexcept {
    if (!make_room(destination, filters.shadow_half, 1U, work)) return;
    const std::size_t width = destination.extent.width;
    const std::size_t height = destination.extent.height;
    const std::size_t margin = static_cast<std::size_t>(filters.shadow_half) * 3U;
    const std::size_t columns = width + margin * 2U;
    const std::size_t rows = height + margin * 2U;
    load_grown(destination, margin, 1U, work.first);
    blur_three_boxes(work, columns, rows, 1U, filters.shadow_half);
    // The blurred alpha is width × height pixels at the start of `first`,
    // one lane a pixel.
    const std::span<const std::uint32_t> first = work.first;
    const std::size_t stride = columns;
    const std::int64_t wide = static_cast<std::int64_t>(width);
    const std::int64_t high = static_cast<std::int64_t>(height);
    const std::int64_t shift_x = filters.shadow_x;
    const std::int64_t shift_y = filters.shadow_y;
    // Only pixels whose shifted source lies inside the blurred alpha can be
    // covered: rows [first_row, end_row) and columns [first_column,
    // end_column). Every other pixel has no shadow and is left as it is.
    const std::int64_t first_row = std::max<std::int64_t>(0, shift_y);
    const std::int64_t end_row = std::min<std::int64_t>(high, high + shift_y);
    const std::int64_t first_column = std::max<std::int64_t>(0, shift_x);
    const std::int64_t end_column = std::min<std::int64_t>(wide, wide + shift_x);
    for (std::int64_t row = first_row; row < end_row; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        const std::size_t source_start = static_cast<std::size_t>(row - shift_y) * stride;
        for (std::int64_t column = first_column; column < end_column; ++column) {
            const std::size_t at = source_start + static_cast<std::size_t>(column - shift_x);
            const double coverage = static_cast<double>(first[at]) / 255.0;
            if (coverage <= 0.0) continue;
            const std::size_t offset = start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel;
            const Unit drawn{static_cast<double>(destination.pixels[offset + 2U]) / 255.0,
                             static_cast<double>(destination.pixels[offset + 1U]) / 255.0,
                             static_cast<double>(destination.pixels[offset]) / 255.0,
                             static_cast<double>(destination.pixels[offset + 3U]) / 255.0};
            // The raster over its shadow.
            Unit shadowed = premultiply(filters.shadow_colour, coverage);
            blend_over(shadowed, drawn);
            store(destination, offset, shadowed);
        }
    }
}

// Blurs the destination's pixels, every channel.
void blur_destination(const PartFilters& filters, const PartRasterView& destination, BoxBlurWork& work) noexcept {
    if (!make_room(destination, filters.blur_half, box_blur_lanes, work)) return;
    const std::size_t width = destination.extent.width;
    const std::size_t height = destination.extent.height;
    const std::size_t margin = static_cast<std::size_t>(filters.blur_half) * 3U;
    const std::size_t columns = width + margin * 2U;
    const std::size_t rows = height + margin * 2U;
    load_grown(destination, margin, box_blur_lanes, work.first);
    blur_three_boxes(work, columns, rows, box_blur_lanes, filters.blur_half);
    const std::span<const std::uint32_t> first = work.first;
    const std::size_t stride = columns * box_blur_lanes;
    for (std::size_t row = 0U; row < height; ++row) {
        const std::size_t start = row * destination.row_bytes;
        for (std::size_t column = 0U; column < width; ++column) {
            const std::size_t from = row * stride + column * box_blur_lanes;
            const std::size_t to = start + column * part_raster_bytes_per_pixel;
            for (std::size_t lane = 0U; lane < box_blur_lanes; ++lane) {
                destination.pixels[to + lane] = static_cast<std::uint8_t>(first[from + lane]);
            }
        }
    }
}

// The drop shadow, then the blur, over a finished bake. Work images that
// cannot be allocated leave that step undone: the bake is noexcept and its
// pixels stay as drawn.
void apply_part_filters(const SurfaceMaterial& material, const PrimitiveTarget& target,
                        const PartRasterView& destination) noexcept {
    const PartFilters filters = part_filters(material, target.scale.value);
    if (!filters.shadow && filters.blur_half == 0U) return;
    BoxBlurWork work{};
    if (filters.shadow) add_drop_shadow(filters, destination, work);
    if (filters.blur_half > 0U) blur_destination(filters, destination, work);
}

// -- the reflection (stage T5, C7) ----------------------------------------------------

std::int64_t nearest_pixel(double value) noexcept {
    const std::int64_t pixel = static_cast<std::int64_t>(std::floor(value + 0.5));
    return pixel;
}

Unit load_unit(const PartRasterView& destination, std::size_t offset) noexcept {
    const Unit unit{static_cast<double>(destination.pixels[offset + 2U]) / 255.0,
                    static_cast<double>(destination.pixels[offset + 1U]) / 255.0,
                    static_cast<double>(destination.pixels[offset]) / 255.0,
                    static_cast<double>(destination.pixels[offset + 3U]) / 255.0};
    return unit;
}

// Mirrors the finished face within the box about the reflection's edge into
// the destination past it (MaterialReflection), in device pixels: the box's
// edges rounded to the nearest pixel boundary, the gap and the reach
// rounded to whole pixels. Line k past the gap (a row for the top and
// bottom edges, a column for the others) shows line k inside the box from
// that edge, along the box's extent on the edge, at the opacity the
// reflection's linear ramp has at the line's centre, beneath what is
// already drawn there. Lines past the destination or past the box's far
// side are not drawn. It reads pixels inside the box only and writes pixels
// outside it only, so it never reads its own writes.
void apply_reflection(const MaterialReflection& reflection, const Rect& box, double scale,
                      const PartRasterView& destination) noexcept {
    const std::int64_t width = static_cast<std::int64_t>(destination.extent.width);
    const std::int64_t height = static_cast<std::int64_t>(destination.extent.height);
    const std::int64_t left = std::clamp<std::int64_t>(nearest_pixel(box.x), 0, width);
    const std::int64_t top = std::clamp<std::int64_t>(nearest_pixel(box.y), 0, height);
    const std::int64_t right = std::clamp<std::int64_t>(nearest_pixel(box.right()), 0, width);
    const std::int64_t bottom = std::clamp<std::int64_t>(nearest_pixel(box.bottom()), 0, height);
    const std::int64_t gap = nearest_pixel(reflection.gap * scale);
    const std::int64_t reach = nearest_pixel(reflection.reach * scale);
    if (reach <= 0 || left >= right || top >= bottom) return;
    const bool rows = reflection.edge == MaterialEdge::top || reflection.edge == MaterialEdge::bottom;
    const std::int64_t along_first = rows ? left : top;
    const std::int64_t along_end = rows ? right : bottom;
    const std::int64_t inside = rows ? bottom - top : right - left;
    const std::int64_t limit = rows ? height : width;
    const std::int64_t lines = std::min(reach, inside);
    const double step = (reflection.end_opacity - reflection.start_opacity) / static_cast<double>(reach);
    for (std::int64_t line = 0; line < lines; ++line) {
        std::int64_t source = 0;
        std::int64_t target = 0;
        switch (reflection.edge) {
        case MaterialEdge::top:
            source = top + line;
            target = top - 1 - gap - line;
            break;
        case MaterialEdge::bottom:
            source = bottom - 1 - line;
            target = bottom + gap + line;
            break;
        case MaterialEdge::left:
            source = left + line;
            target = left - 1 - gap - line;
            break;
        case MaterialEdge::right:
        default:
            source = right - 1 - line;
            target = right + gap + line;
            break;
        }
        // Each line lies further out than the last: once past the
        // destination, every later one is too.
        if (target < 0 || target >= limit) break;
        const double opacity = reflection.start_opacity + step * (static_cast<double>(line) + 0.5);
        if (opacity <= 0.0) continue;
        for (std::int64_t along = along_first; along < along_end; ++along) {
            const std::int64_t source_x = rows ? along : source;
            const std::int64_t source_y = rows ? source : along;
            const std::int64_t target_x = rows ? along : target;
            const std::int64_t target_y = rows ? target : along;
            const std::size_t read = static_cast<std::size_t>(source_y) * destination.row_bytes +
                                     static_cast<std::size_t>(source_x) * part_raster_bytes_per_pixel;
            const std::size_t write = static_cast<std::size_t>(target_y) * destination.row_bytes +
                                      static_cast<std::size_t>(target_x) * part_raster_bytes_per_pixel;
            const Unit face = load_unit(destination, read);
            if (face.alpha <= 0.0) continue;
            Unit reflected{face.red * opacity, face.green * opacity, face.blue * opacity, face.alpha * opacity};
            blend_over(reflected, load_unit(destination, write));
            store(destination, write, reflected);
        }
    }
}

// The mask's alpha at pixel (x, y) (E9.6): its centre moved into the box,
// so that outside the box the mask extends at its edge value (an image
// mask's maps are extended so, extend_at_box).
double mask_alpha(const MaterialLook& look, const MaterialImages& images, std::int64_t x, std::int64_t y) noexcept {
    const Rect& box = look.box;
    const double centre_x = std::max(box.x, std::min(static_cast<double>(x) + 0.5, box.right()));
    const double centre_y = std::max(box.y, std::min(static_cast<double>(y) + 0.5, box.bottom()));
    const Unit sampled = fill_colour(look.mask, images, static_cast<std::size_t>(x), static_cast<std::size_t>(y),
                                     centre_x, centre_y, 1.0);
    return sampled.alpha;
}

// Multiplies every pixel of the finished raster by the mask's alpha (E9.6,
// owner 2026-10-23): after the part filters, so that the shadows, the drop
// shadow and the blur fade with the face beside them.
// An image mask whose variant was not lent leaves the raster unmasked, as
// the built-in path draws it.
void apply_mask(const MaterialLook& look, const MaterialImages& images, const PartRasterView& destination) noexcept {
    if (!look.masked) return;
    const bool image = look.mask.kind == MaterialFillKind::image;
    if (image && images.layers[mask_layer].variant == nullptr) return;
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            const std::size_t offset = start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel;
            // The mask's alpha over 255: a byte times it is the masked unit.
            const double weight = mask_alpha(look, images, static_cast<std::int64_t>(column),
                                           static_cast<std::int64_t>(row)) / 255.0;
            const Unit masked{static_cast<double>(destination.pixels[offset + 2U]) * weight,
                              static_cast<double>(destination.pixels[offset + 1U]) * weight,
                              static_cast<double>(destination.pixels[offset]) * weight,
                              static_cast<double>(destination.pixels[offset + 3U]) * weight};
            store(destination, offset, masked);
        }
    }
}

// -- the bake effort ---------------------------------------------------------------

// Whether a fill blends other than source-over (MaterialBlendMode).
bool blends_apart(const MaterialFillLayer& fill) noexcept {
    const bool apart = fill.blend != MaterialBlendMode::normal;
    return apart;
}

DeviceExtent destination_extent(const PrimitiveTarget& target, double outset) noexcept {
    const double scale = target.scale.value;
    const double width = std::ceil((target.logical_size.width + outset * 2.0) * scale);
    const double height = std::ceil((target.logical_size.height + outset * 2.0) * scale);
    const DeviceExtent extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    return extent;
}

std::uint64_t extra_stops(const MaterialFillLayer& layer) noexcept {
    const std::uint64_t stops = layer.stops.size();
    const std::uint64_t extra = stops > 2U ? stops - 2U : 0U;
    return extra;
}

// One fill layer's effort weight per pixel, in effort units.
std::uint64_t fill_weight(const MaterialFillLayer& layer, const MaterialEffortWeights& weights) noexcept {
    switch (layer.kind) {
    case MaterialFillKind::solid: {
        // A solid fill of alpha 0 is not drawn (resolve_look).
        const std::uint64_t solid = layer.color.alpha != 0U ? weights.solid : 0U;
        return solid;
    }
    case MaterialFillKind::linear_gradient: {
        const std::uint64_t linear = weights.linear + extra_stops(layer) * weights.gradient_stop;
        return linear;
    }
    case MaterialFillKind::radial_gradient: {
        const std::uint64_t radial = weights.radial + extra_stops(layer) * weights.gradient_stop;
        return radial;
    }
    case MaterialFillKind::conic_gradient: {
        const std::uint64_t conic = weights.conic + extra_stops(layer) * weights.gradient_stop;
        return conic;
    }
    case MaterialFillKind::image:
        // Compositing the prepared variant only: its resampling is not
        // the bake's (owner, 2026-10-22).
        return weights.image;
    case MaterialFillKind::procedural:
        // Compositing the generated pattern, as an image's variant is
        // composited; its generation is an idle frame's, bounded apart
        // (procedural_generation_effort).
        return weights.image;
    }
    return 0U;
}

} // namespace

double gaussian_integral(double u) noexcept {
    // Φ(u) = 1/2 + v · P(v²), v = u / 3.5, P fitted by least squares to the
    // exact integral (maximum error 3.8e-4). Beyond ±3.5 it is exactly 0 or
    // 1, so that a covered interior is opaque; the step there, under 4e-4,
    // is less than a fifth of an 8-bit step.
    constexpr double c0 = 1.3941422783609632;
    constexpr double c1 = -2.782308136192413;
    constexpr double c2 = 4.603387247455512;
    constexpr double c3 = -4.9815632521829425;
    constexpr double c4 = 3.0698264846092123;
    constexpr double c5 = -0.8038758147952942;
    if (u >= 3.5) return 1.0;
    if (u <= -3.5) return 0.0;
    const double v = u / 3.5;
    const double w = v * v;
    const double p = c0 + w * (c1 + w * (c2 + w * (c3 + w * (c4 + w * c5))));
    const double value = std::clamp(0.5 + v * p, 0.0, 1.0);
    return value;
}

std::span<const std::string_view> material_recipe_names() noexcept {
    const std::span<const std::string_view> names(recipe_names.data(), recipe_names.size());
    return names;
}

namespace {

// How many of `extent` device pixels, along one axis, lie beyond the
// straight part of a rounded outer shadow's edge and within its reach,
// where its blur is not exactly zero: at most its reach plus its radius,
// and a pixel for the partial one, on each side.
double corner_span(double start, double end, const ResolvedShadow& shadow, std::uint32_t extent) noexcept {
    const double reach = 3.5 / shadow.inverse_sigma;
    const double length = static_cast<double>(extent);
    const double before = std::clamp(start + shadow.radius, 0.0, length) - std::clamp(start - reach, 0.0, length);
    const double after = std::clamp(end + reach, 0.0, length) - std::clamp(end - shadow.radius, 0.0, length);
    const double span = std::min(length, before + after + 2.0);
    return span;
}

// The pixels at which the rounded outer shadows evaluate their signed
// distance (outer_coverage), summed over the shadows: each shadow's
// columns times its rows, rounded up to a whole pixel.
std::uint64_t corner_pixels(const MaterialLook& look, DeviceExtent extent) noexcept {
    std::uint64_t pixels = 0U;
    for (std::size_t index = 0U; index < look.outer_count; ++index) {
        const ResolvedShadow& shadow = look.outer[index];
        if (shadow.radius <= 0.0) continue;
        const Rect& shape = shadow.shape;
        const double columns = corner_span(shape.x, shape.right(), shadow, extent.width);
        const double rows = corner_span(shape.y, shape.bottom(), shadow, extent.height);
        const std::uint64_t covered = static_cast<std::uint64_t>(std::ceil(columns * rows));
        pixels = pixels + covered;
    }
    return pixels;
}

} // namespace

BakeEffort material_bake_effort(const SurfaceMaterial& authored, const PrimitiveTarget& target, double outset,
                                PartShapeKind shape) noexcept {
    std::optional<SurfaceMaterial> finish_storage{};
    const SurfaceMaterial& material = baked_material(authored, finish_storage);
    const MaterialEffortWeights& weights = material_effort_weights;
    const DeviceExtent extent = destination_extent(target, outset);
    // A material's layers are few (SurfaceMaterial's maxima), so the weight
    // per pixel is some thousands of units, and with sides of at most
    // 2^32 pixels the products stay far within 64 bits for any part a
    // raster can hold (maximum_part_raster_side).
    std::uint64_t per_pixel = weights.base;
    for (const MaterialFillLayer& layer : material.fills) per_pixel = per_pixel + fill_weight(layer, weights);
    // A mask (E9.6) is a fill's evaluation and its pass.
    if (material.mask) per_pixel = per_pixel + fill_weight(*material.mask, weights) + weights.mask;
    for (const MaterialShadow& shadow : material.shadows) {
        per_pixel = per_pixel + (shadow.inset ? weights.inset_shadow : weights.outer_shadow);
    }
    // A glow is an outer shadow's kernel with an addition for its blend.
    per_pixel = per_pixel + material.glows.size() * std::uint64_t{weights.outer_shadow};
    if (material.border) per_pixel = per_pixel + weights.border;
    if (material.border && (*material.border).style != MaterialBorderStyle::solid) {
        per_pixel = per_pixel + weights.border_pattern;
    }
    const MaterialBorderEdges& edges = material.border_edges;
    const std::uint64_t edge_count = std::uint64_t{edges.top.has_value()} + std::uint64_t{edges.right.has_value()} +
                                     std::uint64_t{edges.bottom.has_value()} + std::uint64_t{edges.left.has_value()};
    per_pixel = per_pixel + edge_count * weights.border_edge;
    per_pixel = per_pixel + material.keylines.size() * std::uint64_t{weights.keyline};
    if (material.colour_filter) per_pixel = per_pixel + weights.colour_filter;
    if (material.drop_shadow) per_pixel = per_pixel + weights.drop_shadow;
    if (material.filter_blur > 0.0) per_pixel = per_pixel + weights.filter_blur;
    if (material.reflection) per_pixel = per_pixel + weights.reflection;
    if (std::any_of(material.fills.begin(), material.fills.end(), &blends_apart)) {
        per_pixel = per_pixel + weights.fill_blend;
    }
    const std::uint64_t pixels = std::uint64_t{extent.width} * extent.height;
    const std::uint64_t corners = corner_pixels(resolve_look(material, target, outset, shape), extent);
    const BakeEffort effort{pixels * per_pixel + corners * weights.outer_corner};
    return effort;
}

// -- slicing (E7r) -------------------------------------------------------------------

namespace {

// How a material's pixels vary along one axis of its box, in device
// pixels: how far in from the box's start and from its end they change
// with position along the axis, and the shortest box at which every layer
// is drawn as in a longer one (a keyline, an inset shadow's hole or the
// border's inner edge that would vanish in a shorter box, or a radius that
// would be clamped). `varies` when a layer changes all along the axis.
struct AxisReach final {
    double start{};
    double end{};
    double minimum_length{};
    bool varies{};
};

// Where a blurred shadow edge changes: its shape's corner radius plus the
// reach of Φ, which is exactly 0 or 1 from 3.5 σ' on.
double shadow_reach(double blur, double scale, double corner) noexcept {
    const double sigma = 1.0 / inverse_sigma_of(blur, scale);
    const double reach = corner + 3.5 * sigma;
    return reach;
}

void reach_at_least(AxisReach& reach, double start, double end, double minimum_length) noexcept {
    reach.start = std::max(reach.start, start);
    reach.end = std::max(reach.end, end);
    reach.minimum_length = std::max(reach.minimum_length, minimum_length);
}

// A keyline's or a single edge's reach along the axis. A band on an edge
// across the axis (top or bottom, for the across axis) spans the box less
// its inset and changes only at its two ends; one on an edge along it (left
// or right) lies within `inset` + `width` of that end.
void band_reach(AxisReach& reach, MaterialEdge edge, double inset, double width, bool across) noexcept {
    const bool at_start = across ? edge == MaterialEdge::left : edge == MaterialEdge::top;
    const bool at_end = across ? edge == MaterialEdge::right : edge == MaterialEdge::bottom;
    if (at_start) {
        reach_at_least(reach, inset + width + 1.0, 0.0, inset * 2.0 + width + 1.0);
    } else if (at_end) {
        reach_at_least(reach, 0.0, inset + width + 1.0, inset * 2.0 + width + 1.0);
    } else {
        reach_at_least(reach, inset + 1.0, inset + 1.0, inset * 2.0 + 1.0);
    }
}

// Whether a fill layer's colour changes along the axis: a solid's never, a
// linear gradient's when it runs along it, every other kind's always.
bool fill_varies(const MaterialFillLayer& layer, Rect box, double scale, bool across) noexcept {
    if (layer.kind == MaterialFillKind::solid) return false;
    if (layer.kind != MaterialFillKind::linear_gradient) return true;
    const ResolvedFill fill = resolve_fill(layer, box, scale);
    const double step = across ? fill.step_x : fill.step_y;
    const bool varies = step != 0.0;
    return varies;
}

AxisReach axis_reach(const SurfaceMaterial& material, const PrimitiveTarget& target, bool across,
                     double outset, PartShapeKind kind) noexcept {
    const double scale = target.scale.value;
    // The largest corner stands for all four: the reach is at least it. A
    // corner is drawn at most half the box's shorter side, so a larger
    // uniform radius reaches no further (and no pixel differs).
    const MaterialOutline outline = material_outline(material, target, kind);
    const Rect box = material_box(target, outset);
    const double largest = grown(outline, box, 0.0).radius;
    const double radius = std::min(largest, std::min(box.width, box.height) * 0.5);
    AxisReach reach{};
    // A dashed or dotted border changes all the way round.
    if (material.border && (*material.border).style != MaterialBorderStyle::solid) reach.varies = true;
    // The box's own edge, and its corners' arcs.
    reach_at_least(reach, radius + 1.0, radius + 1.0, radius * 2.0 + 1.0);
    for (const MaterialFillLayer& layer : material.fills) {
        if (fill_varies(layer, box, scale, across)) reach.varies = true;
    }
    // A mask (E9.6) varies as a fill does.
    if (material.mask && fill_varies(*material.mask, box, scale, across)) reach.varies = true;
    for (const MaterialShadow& shadow : material.shadows) {
        const double spread = shadow.spread * scale;
        const double offset = (across ? shadow.offset.x : shadow.offset.y) * scale;
        if (!shadow.inset) {
            const double corner = std::max(0.0, radius + spread);
            const double blurred = shadow_reach(shadow.blur_radius, scale, corner);
            reach_at_least(reach, offset - spread + blurred + 1.0, -offset - spread + blurred + 1.0,
                           1.0 - spread * 2.0);
            continue;
        }
        const double corner = std::max(0.0, radius - spread);
        const double blurred = shadow_reach(shadow.blur_radius, scale, corner);
        reach_at_least(reach, offset + spread + blurred + 1.0, -offset + spread + blurred + 1.0,
                       spread * 2.0 + 1.0);
    }
    // A glow changes as an outer shadow at no offset does.
    for (const MaterialGlow& glow : material.glows) {
        const double spread = glow.spread * scale;
        const double blurred = shadow_reach(glow.blur_radius, scale, radius + spread);
        reach_at_least(reach, blurred - spread + 1.0, blurred - spread + 1.0, 1.0 - spread * 2.0);
    }
    if (material.border) {
        const double width = (*material.border).width * scale;
        const double inner = std::max(radius, width) + 1.0;
        reach_at_least(reach, inner, inner, width * 2.0 + 1.0);
    }
    const MaterialBorderEdges& edges = material.border_edges;
    if (edges.top) band_reach(reach, MaterialEdge::top, 0.0, (*edges.top).width * scale, across);
    if (edges.right) band_reach(reach, MaterialEdge::right, 0.0, (*edges.right).width * scale, across);
    if (edges.bottom) band_reach(reach, MaterialEdge::bottom, 0.0, (*edges.bottom).width * scale, across);
    if (edges.left) band_reach(reach, MaterialEdge::left, 0.0, (*edges.left).width * scale, across);
    for (const MaterialKeyline& keyline : material.keylines) {
        band_reach(reach, keyline.edge, keyline.inset * scale, keyline.width * scale, across);
    }
    // The drop shadow and the blur carry every change further in.
    const double carried = filter_reach_pixels(part_filters(material, scale));
    reach.start = reach.start + carried;
    reach.end = reach.end + carried;
    reach.minimum_length = reach.minimum_length + carried * 2.0;
    // A reflection (T5 C7) about the top or bottom edge copies the rows
    // within its reach of that edge, so those rows stay whole along the down
    // axis; one about the left or right edge likewise the columns.
    if (material.reflection) {
        const MaterialReflection& reflection = *material.reflection;
        const bool rows = reflection.edge == MaterialEdge::top || reflection.edge == MaterialEdge::bottom;
        if (rows != across) {
            const double lines = (reflection.gap + reflection.reach) * scale + 1.0;
            reach_at_least(reach, lines, lines, lines * 2.0);
        }
    }
    return reach;
}

// One axis's plan, and the reference's logical length and device extent
// along it.
struct AxisPlan final {
    SliceAxis axis{};
    double reference_length{};
    std::uint32_t reference_extent{};
};

bool whole_number(double value) noexcept {
    const bool whole = std::isfinite(value) && std::floor(value) == value;
    return whole;
}

// The most reference lengths tried past the shortest, looking for one whose
// logical length times the scale gives back exactly its device length.
constexpr std::uint32_t reference_length_tries = 8U;

AxisPlan plan_axis(const AxisReach& reach, double logical, double scale, std::uint32_t extent,
                   double logical_outset) noexcept {
    AxisPlan plan{};
    plan.reference_length = logical;
    plan.reference_extent = extent;
    if (reach.varies) return plan;
    // Slices sit on device-pixel boundaries: the box must start and end on
    // them, or the reference's far end would not have the part's phase.
    const double outset = logical_outset * scale;
    const double length = logical * scale;
    if (!whole_number(outset) || !whole_number(length) || static_cast<double>(extent) != outset + length + outset) {
        plan.axis.outcome = SliceAxisOutcome::fractional;
        return plan;
    }
    const double start = std::ceil(reach.start);
    const double end = std::ceil(reach.end);
    const double shortest = std::max(start + end + 3.0, std::ceil(reach.minimum_length));
    if (!(length > shortest)) {
        plan.axis.outcome = SliceAxisOutcome::too_small;
        return plan;
    }
    for (std::uint32_t attempt = 0U; attempt <= reference_length_tries; ++attempt) {
        const double device = shortest + static_cast<double>(attempt);
        if (!(device < length)) break;
        const double reference = device / scale;
        const double reference_extent = std::ceil((reference + logical_outset + logical_outset) * scale);
        if (reference * scale != device || reference_extent != outset + device + outset) continue;
        plan.axis.outcome = SliceAxisOutcome::sliced;
        plan.axis.start = static_cast<std::uint32_t>(outset + start);
        plan.axis.end = static_cast<std::uint32_t>(outset + end);
        plan.reference_length = reference;
        plan.reference_extent = static_cast<std::uint32_t>(reference_extent);
        return plan;
    }
    plan.axis.outcome = SliceAxisOutcome::fractional;
    return plan;
}

} // namespace

PrimitiveSlicePlan material_slice_plan(const SurfaceMaterial& authored, const PrimitiveTarget& target,
                                       DeviceExtent extent, double outset, PartShapeKind shape) noexcept {
    std::optional<SurfaceMaterial> finish_storage{};
    const SurfaceMaterial& material = baked_material(authored, finish_storage);
    const double scale = target.scale.value;
    const AxisReach across_reach = axis_reach(material, target, true, outset, shape);
    const AxisReach down_reach = axis_reach(material, target, false, outset, shape);
    const AxisPlan across = plan_axis(across_reach, target.logical_size.width, scale, extent.width, outset);
    const AxisPlan down = plan_axis(down_reach, target.logical_size.height, scale, extent.height, outset);
    PrimitiveSlicePlan plan{};
    plan.slices = true;
    plan.across = across.axis;
    plan.down = down.axis;
    plan.reference_size = Size{across.reference_length, down.reference_length};
    plan.reference_extent = DeviceExtent{across.reference_extent, down.reference_extent};
    return plan;
}

// -- MaterialPrimitive -------------------------------------------------------------

namespace {

PartShapeKind shape_kind(const PrimitiveParameterBlock& parameters) noexcept {
    const double position = parameters.values[material_shape].number;
    const PartShapeKind kind = static_cast<PartShapeKind>(static_cast<std::uint8_t>(position));
    return kind;
}

} // namespace

const PrimitiveDescriptor& MaterialPrimitive::descriptor() const noexcept {
    return material_descriptor;
}

std::optional<ControlVisualRole> MaterialPrimitive::theme_material(const PrimitiveParameterBlock& parameters,
                                                                   ControlVisualRole family) const noexcept {
    const double position = parameters.values[material_recipe].number;
    if (position == family_recipe) return family;
    const ControlVisualRole role = static_cast<ControlVisualRole>(static_cast<std::uint8_t>(position));
    return role;
}

PartShape MaterialPrimitive::shape(const PrimitiveParameterBlock& parameters) const noexcept {
    // A rounded rectangle's radii are the material's, which the parameters
    // do not hold: its hit region is the box, as before E9.6.
    PartShape outline{};
    outline.kind = shape_kind(parameters);
    return outline;
}

bool MaterialPrimitive::accepts(const PrimitiveParameterBlock& parameters,
                                const PrimitiveTarget& target) const noexcept {
    if (target.material == nullptr) return false;
    const SurfaceMaterial& material = *target.material;
    if (!valid_surface_material(material)) return false;
    const Rect box = material_box(target, material_outset);
    for (const MaterialFillLayer& layer : material.fills) {
        if (layer.kind == MaterialFillKind::procedural) {
            // A pattern whose generation would stall an idle frame, or which
            // would crowd the shared cache, is refused (E10): the cost-bound
            // fallback.
            const DeviceExtent pattern = procedural_extent(layer.procedural, box, target.scale.value);
            if (!procedural_pattern_bounded(layer.procedural, pattern)) return false;
            continue;
        }
        if (layer.kind != MaterialFillKind::image) continue;
        if (target.images == nullptr || !material_image_available(*target.images, layer)) return false;
    }
    if (material.mask && (*material.mask).kind == MaterialFillKind::image) {
        if (target.images == nullptr || !material_image_available(*target.images, *material.mask)) return false;
    }
    // A part assembled from slices bakes only its reference.
    const PartShapeKind kind = shape_kind(parameters);
    const PrimitiveSlicePlan plan =
        material_slice_plan(material, target, destination_extent(target, material_outset), material_outset, kind);
    const bool sliced =
        plan.across.outcome == SliceAxisOutcome::sliced || plan.down.outcome == SliceAxisOutcome::sliced;
    PrimitiveTarget baked = target;
    if (sliced) baked.logical_size = plan.reference_size;
    const BakeEffort effort = material_bake_effort(material, baked, material_outset, kind);
    const bool within = effort <= maximum_material_bake_effort;
    return within;
}

PrimitiveSlicePlan MaterialPrimitive::slice_plan(const PrimitiveParameterBlock& parameters,
                                                 const PrimitiveTarget& target, DeviceExtent extent) const noexcept {
    if (target.material == nullptr) return PrimitiveSlicePlan{};
    const PrimitiveSlicePlan plan =
        material_slice_plan(*target.material, target, extent, material_outset, shape_kind(parameters));
    return plan;
}

std::size_t MaterialPrimitive::image_variants(const PrimitiveParameterBlock&, const PrimitiveTarget& target,
                                              std::span<ImageVariantRequest> requests) const noexcept {
    if (target.material == nullptr) return 0U;
    const SurfaceMaterial& material = *target.material;
    const Rect box = material_box(target, material_outset);
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < material.fills.size() && count < requests.size(); ++index) {
        const MaterialFillLayer& layer = material.fills[index];
        if (layer.kind == MaterialFillKind::procedural) {
            ImageVariantRequest pattern{};
            pattern.layer = index;
            pattern.scale = target.scale;
            pattern.extent = procedural_extent(layer.procedural, box, target.scale.value);
            pattern.procedural = layer.procedural;
            requests[count] = pattern;
            count = count + 1U;
            continue;
        }
        if (layer.kind != MaterialFillKind::image) continue;
        requests[count] = image_variant_request(layer, index, box, target.scale.value);
        count = count + 1U;
    }
    // An image mask's variant (E9.6), in its own slot.
    const bool image_mask = material.mask && (*material.mask).kind == MaterialFillKind::image;
    if (image_mask && count < requests.size()) {
        requests[count] = image_variant_request(*material.mask, mask_layer, box, target.scale.value);
        count = count + 1U;
    }
    return count;
}

void MaterialPrimitive::derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept {}

void MaterialPrimitive::fill(const PrimitiveParameterBlock& parameters, const PrimitiveTarget& target,
                             std::span<const double>, const PartRasterView& destination) const noexcept {
    // accepts() has passed: the target lends a valid material.
    bake_material(*target.material, target, destination, material_outset, shape_kind(parameters));
}

namespace {

// The look's pixels into every pixel of `destination`, with `images`
// prepared for it (prepare_images).
void bake_look(const MaterialLook& look, const MaterialImages& images, const PartRasterView& destination) noexcept {
    const std::size_t width = destination.extent.width;
    OuterIntegrals integrals{};
    fill_columns(look, width, integrals);
    for (std::uint32_t row = 0U; row < destination.extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * destination.row_bytes;
        fill_row(look, row, integrals);
        for (std::uint32_t column = 0U; column < destination.extent.width; ++column) {
            const Unit pixel = material_pixel(look, images, integrals, static_cast<std::int64_t>(column),
                                              static_cast<std::int64_t>(row));
            store(destination, start + static_cast<std::size_t>(column) * part_raster_bytes_per_pixel, pixel);
        }
    }
}

} // namespace

void bake_material(const SurfaceMaterial& authored, const PrimitiveTarget& target,
                   const PartRasterView& destination, double outset, PartShapeKind shape) noexcept {
    std::optional<SurfaceMaterial> finish_storage{};
    const SurfaceMaterial& material = baked_material(authored, finish_storage);
    const MaterialLook look = resolve_look(material, target, outset, shape);
    MaterialImages images{};
    prepare_images(material, target, look, destination.extent, images);
    bake_look(look, images, destination);
    apply_part_filters(material, target, destination);
    apply_mask(look, images, destination);
    if (material.reflection) apply_reflection(*material.reflection, look.box, target.scale.value, destination);
}

void bake_material_region(const SurfaceMaterial& authored, const PrimitiveTarget& target, Point device_origin,
                          const PartRasterView& destination) noexcept {
    std::optional<SurfaceMaterial> finish_storage{};
    const SurfaceMaterial& material = baked_material(authored, finish_storage);
    const MaterialLook look =
        resolve_look_at(material, target, Point{-device_origin.x, -device_origin.y}, PartShapeKind::rounded_rectangle);
    MaterialImages images{};
    prepare_images(material, target, look, destination.extent, images);
    bake_look(look, images, destination);
    apply_mask(look, images, destination);
}

} // namespace gui_forms::engine_primitives
