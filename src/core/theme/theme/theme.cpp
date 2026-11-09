#include "gui_forms/theme.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/surface_material/presets/material_presets.hpp"
#include "gui_forms/window/window.hpp"

#include "ancestor_facts.hpp"
#include "floor_sheet.hpp"
#include "forced_high_contrast.hpp"
#include "material_layers.hpp"
#include "theme_access.hpp"
#include "../theme_compiler/theme_source_record.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

bool finite(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool finite(Size size) noexcept {
    return std::isfinite(size.width) && std::isfinite(size.height);
}

bool finite(Insets insets) noexcept {
    return std::isfinite(insets.left) && std::isfinite(insets.top) &&
           std::isfinite(insets.right) && std::isfinite(insets.bottom);
}

bool bounded_coordinate(Point point, MaterialCoordinateSpace space) noexcept {
    if (!finite(point)) return false;
    const double bound = space == MaterialCoordinateSpace::normalized
        ? 16.0 : 1'000'000.0;
    return std::abs(point.x) <= bound && std::abs(point.y) <= bound;
}

// An image fill's position (E9.6): its anchor within [0, 1] on each axis
// and its offset within ±4096 logical units.
bool valid_image_position(const MaterialFillLayer& fill) noexcept {
    const Point anchor = fill.image_anchor;
    const Point offset = fill.image_offset;
    const bool anchored = finite(anchor) && anchor.x >= 0.0 && anchor.x <= 1.0 && anchor.y >= 0.0 && anchor.y <= 1.0;
    const bool offset_bounded = finite(offset) && std::abs(offset.x) <= 4096.0 && std::abs(offset.y) <= 4096.0;
    return anchored && offset_bounded;
}

bool valid_material_border(const MaterialBorder& border) noexcept {
    return std::isfinite(border.width) && border.width > 0.0 &&
           border.width <= 64.0 &&
           (border.style == MaterialBorderStyle::solid ||
            border.style == MaterialBorderStyle::dashed ||
            border.style == MaterialBorderStyle::dotted);
}

// The part filters' reach outside the box (SurfaceMaterial::
// maximum_filter_reach), or a negative value when one is malformed.
double filter_reach(const SurfaceMaterial& material) noexcept {
    if (!std::isfinite(material.filter_blur) || material.filter_blur < 0.0) return -1.0;
    double reach = material.filter_blur * 1.5;
    if (material.drop_shadow) {
        const MaterialDropShadow& shadow = *material.drop_shadow;
        if (!finite(shadow.offset) || !std::isfinite(shadow.blur_radius) || shadow.blur_radius < 0.0) return -1.0;
        const double moved = std::max(std::abs(shadow.offset.x), std::abs(shadow.offset.y));
        reach = reach + moved + shadow.blur_radius * 1.5;
    }
    return reach;
}

bool unit_interval(double value) noexcept {
    const bool inside = std::isfinite(value) && value >= 0.0 && value <= 1.0;
    return inside;
}

// A reflection (T5 C7): a known edge, a gap of zero or more and a reach
// above zero that together stay within SurfaceMaterial::
// maximum_reflection_reach, and opacities in [0, 1].
bool valid_material_reflection(const MaterialReflection& reflection) noexcept {
    const bool edge_known =
        static_cast<std::uint8_t>(reflection.edge) <= static_cast<std::uint8_t>(MaterialEdge::left);
    const bool lengths = std::isfinite(reflection.gap) && std::isfinite(reflection.reach) && reflection.gap >= 0.0 &&
                         reflection.reach > 0.0 &&
                         reflection.gap + reflection.reach <= SurfaceMaterial::maximum_reflection_reach;
    const bool opacities = unit_interval(reflection.start_opacity) && unit_interval(reflection.end_opacity);
    const bool source_known = static_cast<std::uint8_t>(reflection.source) <=
                              static_cast<std::uint8_t>(MaterialReflectionSource::backplane);
    return edge_known && lengths && opacities && source_known;
}

bool valid_corner(double radius) noexcept {
    return std::isfinite(radius) && radius >= 0.0 && radius <= 4096.0;
}

bool valid_corner_radii(const MaterialCornerRadii& radii) noexcept {
    return valid_corner(radii.top_left) && valid_corner(radii.top_right) &&
           valid_corner(radii.bottom_right) && valid_corner(radii.bottom_left);
}

bool valid_material_shadow(const MaterialShadow& shadow) noexcept {
    return finite(shadow.offset) && std::isfinite(shadow.blur_radius) &&
           std::isfinite(shadow.spread) && shadow.blur_radius >= 0.0 &&
           shadow.blur_radius <= 512.0 && std::abs(shadow.spread) <= 4096.0 &&
           std::abs(shadow.offset.x) <= 4096.0 &&
           std::abs(shadow.offset.y) <= 4096.0;
}

// How far a glow reaches outside its box: its spread plus two blur radii,
// four of its σ, past which its blur is below one 8-bit step.
double glow_reach(const MaterialGlow& glow) noexcept {
    const double reach = glow.spread + glow.blur_radius * 2.0;
    return reach;
}

// A glow within the granted overflow (E9-C): a blur and a spread of zero
// or more whose reach is at most SurfaceMaterial::maximum_glow_reach, under
// or over the face.
bool valid_material_glow(const MaterialGlow& glow) noexcept {
    const bool placed =
        glow.placement == MaterialGlowPlacement::under || glow.placement == MaterialGlowPlacement::over;
    const bool valid = placed && std::isfinite(glow.blur_radius) && std::isfinite(glow.spread) &&
                       glow.blur_radius >= 0.0 && glow.spread >= 0.0 &&
                       glow_reach(glow) <= SurfaceMaterial::maximum_glow_reach;
    return valid;
}

// A procedural fill's parameters (E10): a built-in pattern, a finite tile
// within [minimum_procedural_tile, maximum_procedural_tile], and a finite
// strength in [0, 1].
bool valid_procedural_fill(const ProceduralFill& fill) noexcept {
    switch (fill.pattern) {
    case ProceduralPattern::felt:
    case ProceduralPattern::wood_grain:
    case ProceduralPattern::paper_fibre:
    case ProceduralPattern::pinstripe:
    case ProceduralPattern::brushed_metal:
    case ProceduralPattern::fresco:
    case ProceduralPattern::checker: {
        const bool valid = std::isfinite(fill.tile) && fill.tile >= minimum_procedural_tile &&
                           fill.tile <= maximum_procedural_tile && std::isfinite(fill.strength) &&
                           fill.strength >= 0.0 && fill.strength <= 1.0;
        return valid;
    }
    }
    // A pattern outside the enumeration is not built in.
    return false;
}

// An overlay shadow: at most SurfaceMaterial::maximum_shadows valid layers,
// none of them inset (an overlay shadow falls outside its surface).
bool valid_overlay_shadow(const std::vector<MaterialShadow>& layers) noexcept {
    if (layers.size() > SurfaceMaterial::maximum_shadows) return false;
    for (const MaterialShadow& shadow : layers) {
        if (shadow.inset || !valid_material_shadow(shadow)) return false;
    }
    return true;
}

bool valid_material_keyline(const MaterialKeyline& keyline) noexcept {
    return (keyline.edge == MaterialEdge::top ||
            keyline.edge == MaterialEdge::right ||
            keyline.edge == MaterialEdge::bottom ||
            keyline.edge == MaterialEdge::left) &&
           std::isfinite(keyline.width) && keyline.width > 0.0 &&
           keyline.width <= 16.0 && std::isfinite(keyline.inset) &&
           keyline.inset >= 0.0 && keyline.inset <= 4096.0;
}

void paint_material_keyline(Painter& painter, Rect bounds,
                            const MaterialKeyline& keyline) {
    const double offset = keyline.inset + keyline.width * 0.5;
    switch (keyline.edge) {
    case MaterialEdge::top:
        if (offset * 2.0 > bounds.height) return;
        painter.draw_line({bounds.x + keyline.inset, bounds.y + offset},
                          {bounds.right() - keyline.inset, bounds.y + offset},
                          keyline.color, keyline.width);
        return;
    case MaterialEdge::right:
        if (offset * 2.0 > bounds.width) return;
        painter.draw_line({bounds.right() - offset,
                           bounds.y + keyline.inset},
                          {bounds.right() - offset,
                           bounds.bottom() - keyline.inset},
                          keyline.color, keyline.width);
        return;
    case MaterialEdge::bottom:
        if (offset * 2.0 > bounds.height) return;
        painter.draw_line({bounds.x + keyline.inset,
                           bounds.bottom() - offset},
                          {bounds.right() - keyline.inset,
                           bounds.bottom() - offset},
                          keyline.color, keyline.width);
        return;
    case MaterialEdge::left:
        if (offset * 2.0 > bounds.width) return;
        painter.draw_line({bounds.x + offset, bounds.y + keyline.inset},
                          {bounds.x + offset,
                           bounds.bottom() - keyline.inset},
                          keyline.color, keyline.width);
        return;
    }
    // An edge outside the enumeration draws no keyline.
    return;
}

Point resolve_point(Point point, MaterialCoordinateSpace space,
                    Rect bounds) noexcept {
    if (space == MaterialCoordinateSpace::normalized) {
        return {bounds.x + point.x * bounds.width,
                bounds.y + point.y * bounds.height};
    }
    return {bounds.x + point.x, bounds.y + point.y};
}

Size resolve_radii(Size radii, MaterialCoordinateSpace space,
                   Rect bounds) noexcept {
    if (space == MaterialCoordinateSpace::normalized) {
        return {radii.width * bounds.width, radii.height * bounds.height};
    }
    return radii;
}

std::pair<Point, Point> resolve_css_linear_gradient(
    double angle_degrees, Rect bounds) noexcept {
    double angle = std::fmod(angle_degrees, 360.0);
    if (angle < 0.0) angle += 360.0;
    Point direction;
    constexpr double epsilon = 1.0e-12;
    if (std::abs(angle) < epsilon || std::abs(angle - 360.0) < epsilon) {
        direction = {0.0, -1.0};
    } else if (std::abs(angle - 90.0) < epsilon) {
        direction = {1.0, 0.0};
    } else if (std::abs(angle - 180.0) < epsilon) {
        direction = {0.0, 1.0};
    } else if (std::abs(angle - 270.0) < epsilon) {
        direction = {-1.0, 0.0};
    } else {
        constexpr double degrees_to_radians =
            3.14159265358979323846264338327950288 / 180.0;
        const double radians = angle * degrees_to_radians;
        direction = {std::sin(radians), -std::cos(radians)};
    }
    const Point center{bounds.x + bounds.width * 0.5,
                       bounds.y + bounds.height * 0.5};
    const double half_length =
        (std::abs(direction.x) * bounds.width +
         std::abs(direction.y) * bounds.height) * 0.5;
    return {
        {center.x - direction.x * half_length,
         center.y - direction.y * half_length},
        {center.x + direction.x * half_length,
         center.y + direction.y * half_length},
    };
}

void paint_stretched_image(Painter& painter, Rect bounds,
                           const MaterialFillLayer& fill) {
    painter.draw_image_region(
        fill.image, {0.0, 0.0, fill.image_pixel_size.width,
                     fill.image_pixel_size.height},
        bounds, fill.opacity);
}

void paint_tiled_image(Painter& painter, Rect bounds,
                       const MaterialFillLayer& fill) {
    const double tile_width = fill.image_pixel_size.width / fill.image_scale;
    const double tile_height = fill.image_pixel_size.height / fill.image_scale;
    const bool placed = fill.image_anchor != Point{} || fill.image_offset != Point{};
    if (!placed) {
        painter.fill_image_pattern(
            fill.image, fill.image_pixel_size, bounds,
            {tile_width, tile_height}, ImagePatternWrap::tile, fill.opacity);
        return;
    }
    // A positioned tiling (E9.6): the pattern starts at the tile's corner
    // moved back by whole tiles to cover the box, clipped to the box.
    const double origin_x = bounds.x + (bounds.width - tile_width) * fill.image_anchor.x + fill.image_offset.x;
    const double origin_y = bounds.y + (bounds.height - tile_height) * fill.image_anchor.y + fill.image_offset.y;
    const double back_x = std::ceil((origin_x - bounds.x) / tile_width) * tile_width;
    const double back_y = std::ceil((origin_y - bounds.y) / tile_height) * tile_height;
    const Rect covered{origin_x - back_x, origin_y - back_y, bounds.right() - (origin_x - back_x),
                       bounds.bottom() - (origin_y - back_y)};
    painter.save();
    painter.clip_rect(bounds);
    painter.fill_image_pattern(
        fill.image, fill.image_pixel_size, covered,
        {tile_width, tile_height}, ImagePatternWrap::tile, fill.opacity);
    painter.restore();
}

// One tile placed as a tile is (E9.6, CSS `no-repeat`), clipped to the box.
void paint_single_image(Painter& painter, Rect bounds, const MaterialFillLayer& fill) {
    const double tile_width = fill.image_pixel_size.width / fill.image_scale;
    const double tile_height = fill.image_pixel_size.height / fill.image_scale;
    const double origin_x = bounds.x + (bounds.width - tile_width) * fill.image_anchor.x + fill.image_offset.x;
    const double origin_y = bounds.y + (bounds.height - tile_height) * fill.image_anchor.y + fill.image_offset.y;
    painter.save();
    painter.clip_rect(bounds);
    painter.draw_image_region(fill.image, {0.0, 0.0, fill.image_pixel_size.width, fill.image_pixel_size.height},
                              {origin_x, origin_y, tile_width, tile_height}, fill.opacity);
    painter.restore();
}

void paint_nine_patch(Painter& painter, Rect bounds,
                      const MaterialFillLayer& fill) {
    const Insets source = fill.image_slice;
    double destination_left = source.left / fill.image_scale;
    double destination_right = source.right / fill.image_scale;
    double destination_top = source.top / fill.image_scale;
    double destination_bottom = source.bottom / fill.image_scale;
    const double horizontal = destination_left + destination_right;
    if (horizontal > bounds.width && horizontal > 0.0) {
        const double factor = bounds.width / horizontal;
        destination_left *= factor;
        destination_right *= factor;
    }
    const double vertical = destination_top + destination_bottom;
    if (vertical > bounds.height && vertical > 0.0) {
        const double factor = bounds.height / vertical;
        destination_top *= factor;
        destination_bottom *= factor;
    }

    const std::array<double, 4> source_x{
        0.0, source.left,
        fill.image_pixel_size.width - source.right,
        fill.image_pixel_size.width};
    const std::array<double, 4> source_y{
        0.0, source.top,
        fill.image_pixel_size.height - source.bottom,
        fill.image_pixel_size.height};
    const std::array<double, 4> destination_x{
        bounds.x, bounds.x + destination_left,
        bounds.right() - destination_right, bounds.right()};
    const std::array<double, 4> destination_y{
        bounds.y, bounds.y + destination_top,
        bounds.bottom() - destination_bottom, bounds.bottom()};
    for (std::size_t row = 0; row < 3U; ++row) {
        for (std::size_t column = 0; column < 3U; ++column) {
            const Rect source_rect{
                source_x[column], source_y[row],
                source_x[column + 1U] - source_x[column],
                source_y[row + 1U] - source_y[row]};
            const Rect destination_rect{
                destination_x[column], destination_y[row],
                destination_x[column + 1U] - destination_x[column],
                destination_y[row + 1U] - destination_y[row]};
            if (!source_rect.empty() && !destination_rect.empty()) {
                painter.draw_image_region(fill.image, source_rect,
                                          destination_rect, fill.opacity);
            }
        }
    }
}

bool valid_spacing_value(double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 4096.0;
}

bool valid_geometry_value(double value) noexcept {
    return std::isfinite(value) && value > 0.0 && value <= 16384.0;
}

bool valid_motion_duration(std::chrono::milliseconds duration) noexcept {
    return duration.count() > 0 && duration.count() <= 60'000;
}

bool valid_structure(const ThemeStructureTokens& structure) noexcept {
    const ThemeSpacingTokens& spacing = structure.spacing;
    const double spacing_values[] = {
        spacing.micro, spacing.xsmall, spacing.small, spacing.medium,
        spacing.large, spacing.xlarge, spacing.section};
    if (!std::all_of(std::begin(spacing_values), std::end(spacing_values),
                     &valid_spacing_value) ||
        !(spacing.micro <= spacing.xsmall &&
          spacing.xsmall <= spacing.small &&
          spacing.small <= spacing.medium &&
          spacing.medium <= spacing.large &&
          spacing.large <= spacing.xlarge &&
          spacing.xlarge <= spacing.section)) {
        return false;
    }

    const ThemeGeometryTokens& geometry = structure.geometry;
    const double geometry_values[] = {
        geometry.compact_control_height, geometry.control_height,
        geometry.large_control_height, geometry.minimum_touch_target,
        geometry.splitter_width, geometry.splitter_hit_width,
        geometry.navigation_extent, geometry.navigation_minimum,
        geometry.content_minimum, geometry.compact_breakpoint};
    if (!std::all_of(std::begin(geometry_values), std::end(geometry_values),
                     &valid_geometry_value) ||
        !(geometry.compact_control_height <= geometry.control_height &&
          geometry.control_height <= geometry.large_control_height &&
          geometry.splitter_width <= geometry.splitter_hit_width &&
          geometry.navigation_minimum <= geometry.navigation_extent)) {
        return false;
    }

    const ThemeTypographyTokens& typography = structure.typography;
    if (!valid_font_spec(typography.control) ||
        !valid_font_spec(typography.field) ||
        !valid_font_spec(typography.caption) ||
        !valid_font_spec(typography.heading) ||
        !valid_font_spec(typography.title) ||
        !valid_font_spec(typography.monospace)) {
        return false;
    }

    const ThemeMotionTokens& motion = structure.motion;
    return valid_motion_duration(motion.quick) &&
           valid_motion_duration(motion.standard) &&
           valid_motion_duration(motion.emphasized) &&
           valid_motion_duration(motion.busy_cycle) &&
           motion.quick <= motion.standard &&
           motion.standard <= motion.emphasized;
}

SurfaceMaterial surface(Color top, Color bottom, Color border,
                        double radius = 3.0) {
    SurfaceMaterial result;
    if (top == bottom) {
        result.fills = {MaterialFillLayer::solid(top)};
    } else {
        result.fills = {MaterialFillLayer::linear(
            {0.0, 0.0}, {0.0, 1.0},
            {{0.0, top}, {1.0, bottom}})};
    }
    result.border = MaterialBorder{border, 1.0};
    result.corner_radius = radius;
    return result;
}

ControlVisualRecipe recipe(Color top, Color bottom, Color border, Color text,
                           double radius = 3.0) {
    ControlVisualRecipe result;
    result.material = surface(top, bottom, border, radius);
    result.text = text;
    result.muted_text = Color::rgba(118, 128, 138);
    result.glyph = text;
    result.focus_ring = Color::rgba(31, 104, 185);
    result.default_ring = Color::rgba(20, 75, 145);
    return result;
}

// Fills `result` with one professional role's recipes. It fills the
// caller's storage rather than returning a value: a ControlRoleRecipes is
// some 9 KB, and windows_professional_theme_definition makes one per
// family, so returned temporaries cost its frame over 100 KB where stack
// slots are not reused (sanitizer builds), close to a Windows thread's 1 MB
// stack.
void fill_professional_role(ControlRoleRecipes& result, Color top, Color bottom,
                            Color border, Color text, double radius = 3.0);

// A badge (T4, W-4): an accent chip with white text in every state but
// disabled, which greys it; a badge takes no input, so hot, pressed and the
// validity states look as normal. Its corners are rounded to a pill by the
// control (PartPaint::corner_radius).
void fill_badge_role(ControlRoleRecipes& result) {
    fill_professional_role(result, Color::rgba(38, 114, 185), Color::rgba(38, 114, 185),
                           Color::rgba(38, 114, 185), Color::rgba(255, 255, 255), 0.0);
    const ControlVisualRecipe resting = result.ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)];
    const ControlVisualRecipe greyed = result.ordinary[static_cast<std::size_t>(ControlSurfaceState::disabled)];
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const bool disabled = index == static_cast<std::size_t>(ControlSurfaceState::disabled);
        result.ordinary[index] = disabled ? greyed : resting;
        result.selected[index] = result.ordinary[index];
    }
}

void fill_professional_role(ControlRoleRecipes& result, Color top, Color bottom,
                            Color border, Color text, double radius) {
    // Every record starts from its default; the high-contrast records stay
    // so (they are derived when the theme is made).
    result = ControlRoleRecipes{};
    std::array<ControlVisualRecipe, control_surface_state_count>& values = result.ordinary;
    values[static_cast<std::size_t>(ControlSurfaceState::normal)] =
        recipe(top, bottom, border, text, radius);
    values[static_cast<std::size_t>(ControlSurfaceState::hot)] =
        recipe(Color::rgba(255, 255, 255), Color::rgba(213, 233, 250),
               Color::rgba(72, 139, 196), text, radius);
    values[static_cast<std::size_t>(ControlSurfaceState::pressed)] =
        recipe(Color::rgba(188, 218, 242), Color::rgba(225, 240, 251),
               Color::rgba(46, 109, 166), text, radius);
    values[static_cast<std::size_t>(ControlSurfaceState::pending)] =
        recipe(Color::rgba(255, 249, 218), Color::rgba(246, 229, 157),
               Color::rgba(173, 132, 31), text, radius);
    values[static_cast<std::size_t>(ControlSurfaceState::invalid)] =
        recipe(Color::rgba(255, 239, 239), Color::rgba(247, 207, 207),
               Color::rgba(178, 54, 54), text, radius);
    values[static_cast<std::size_t>(ControlSurfaceState::disabled)] =
        recipe(Color::rgba(240, 242, 244), Color::rgba(226, 229, 232),
               Color::rgba(184, 190, 196), Color::rgba(132, 143, 153), radius);
    values[static_cast<std::size_t>(ControlSurfaceState::deactivated)] =
        recipe(Color::rgba(244, 246, 248), Color::rgba(224, 228, 232),
               Color::rgba(161, 169, 176), Color::rgba(86, 96, 106), radius);

    result.selected = values;
    const std::array<Color, control_surface_state_count> selected_top{
        Color::rgba(225, 239, 255), Color::rgba(236, 247, 255),
        Color::rgba(166, 205, 242), Color::rgba(255, 242, 190),
        Color::rgba(255, 221, 221), Color::rgba(222, 226, 230),
        Color::rgba(218, 225, 232)};
    const std::array<Color, control_surface_state_count> selected_bottom{
        Color::rgba(188, 216, 247), Color::rgba(199, 226, 250),
        Color::rgba(205, 228, 247), Color::rgba(239, 214, 127),
        Color::rgba(244, 188, 188), Color::rgba(205, 211, 216),
        Color::rgba(198, 207, 216)};
    for (std::size_t index = 0; index < result.selected.size(); ++index) {
        ControlVisualRecipe& selected = result.selected[index];
        selected.material.fills = {MaterialFillLayer::linear(
            {0.0, 0.0}, {0.0, 1.0},
            {{0.0, selected_top[index]},
             {1.0, selected_bottom[index]}})};
        selected.material.border =
            MaterialBorder{Color::rgba(57, 117, 178), 1.0};
    }

    // The high-contrast records are derived from the ordinary ones when the
    // theme is made (derive_high_contrast_recipes, the forced high-contrast
    // pass of T2 E4), not authored here.
}

// Replaces the accent family's plates where fill_professional_role's light
// state plates sat under the accent's white text (1.0 to 1.3:1, and 3.3:1
// at the resting gradient's top; T4 phase 1's contrast report): resting,
// hot and pressed are blues and pending and invalid dark amber and red,
// each passing 4.5:1 for white text at every gradient position, selected
// or not. Disabled and deactivated keep their grey plates and dark text.
void fill_accent_states(ControlRoleRecipes& result) {
    const Color white = Color::rgba(255, 255, 255);
    std::array<ControlVisualRecipe, control_surface_state_count>& values = result.ordinary;
    values[static_cast<std::size_t>(ControlSurfaceState::normal)] =
        recipe(Color::rgba(46, 112, 190), Color::rgba(31, 100, 177), Color::rgba(20, 75, 145), white);
    values[static_cast<std::size_t>(ControlSurfaceState::hot)] =
        recipe(Color::rgba(50, 118, 198), Color::rgba(36, 106, 186), Color::rgba(14, 62, 128), white);
    values[static_cast<std::size_t>(ControlSurfaceState::pressed)] =
        recipe(Color::rgba(24, 84, 160), Color::rgba(34, 100, 178), Color::rgba(14, 62, 128), white);
    values[static_cast<std::size_t>(ControlSurfaceState::pending)] =
        recipe(Color::rgba(150, 104, 20), Color::rgba(128, 88, 12), Color::rgba(110, 74, 10), white);
    values[static_cast<std::size_t>(ControlSurfaceState::invalid)] =
        recipe(Color::rgba(178, 54, 54), Color::rgba(150, 40, 40), Color::rgba(120, 30, 30), white);
    // A selected (checked) accent button shows the pressed plate in its
    // active states, with the selection's blue edge.
    const std::array<ControlSurfaceState, 3> pressed_when_selected{
        ControlSurfaceState::normal, ControlSurfaceState::hot, ControlSurfaceState::pressed};
    for (const ControlSurfaceState state : pressed_when_selected) {
        ControlVisualRecipe& selected = result.selected[static_cast<std::size_t>(state)];
        selected = values[static_cast<std::size_t>(ControlSurfaceState::pressed)];
        selected.material.border = MaterialBorder{Color::rgba(57, 117, 178), 1.0};
    }
    result.selected[static_cast<std::size_t>(ControlSurfaceState::pending)] =
        values[static_cast<std::size_t>(ControlSurfaceState::pending)];
    result.selected[static_cast<std::size_t>(ControlSurfaceState::invalid)] =
        values[static_cast<std::size_t>(ControlSurfaceState::invalid)];
}

// Fills `result` with a link family's recipes (owner decision W-Q5): a
// professional role whose plates are all clear, so that a link draws only
// its text and underline, with `text` in every enabled state (hover and
// press keep the link's colour, and window deactivation does not dim it)
// and `disabled_text` when disabled. The built-in theme passes its
// compatibility style's link colours, which LinkLabel drew before the
// family existed.
void fill_link_role(ControlRoleRecipes& result, Color text, Color disabled_text) {
    const Color clear = Color::rgba(255, 255, 255, 0);
    fill_professional_role(result, clear, clear, clear, text, 0.0);
    for (std::size_t index = 0; index < control_surface_state_count; ++index) {
        const bool disabled = index == static_cast<std::size_t>(ControlSurfaceState::disabled);
        const Color state_text = disabled ? disabled_text : text;
        ControlVisualRecipe& ordinary = result.ordinary[index];
        ordinary.material = surface(clear, clear, clear, 0.0);
        ordinary.text = state_text;
        ordinary.glyph = state_text;
        result.selected[index] = ordinary;
    }
}

// GUI.Forms' own high-contrast mode (T2 plan, E4; design language 5.8),
// from the role model (T4 phase 5d): each colour role's high-contrast pair
// in one table, and every surface state's palette read from those roles.
// Under the host's own mode the host palette is meant to win; no host
// reports one to the core yet.
struct HighContrastPair final {
    Color background{};
    Color foreground{};
};

constexpr Color high_contrast_black = Color::rgba(0, 0, 0);
constexpr Color high_contrast_white = Color::rgba(255, 255, 255);
constexpr Color high_contrast_cyan = Color::rgba(0, 255, 255);
constexpr Color high_contrast_invalid = Color::rgba(255, 96, 96);
constexpr Color high_contrast_disabled_ink = Color::rgba(160, 160, 160);
// Two state colours no colour role names: the pressed edge and the
// disabled edge.
constexpr Color high_contrast_pressed_edge = Color::rgba(255, 255, 0);
constexpr Color high_contrast_disabled_edge = Color::rgba(128, 128, 128);

constexpr HighContrastPair high_contrast_ink{high_contrast_black, high_contrast_white};
constexpr HighContrastPair high_contrast_inverse{high_contrast_white, high_contrast_black};
constexpr HighContrastPair high_contrast_plate{high_contrast_black, high_contrast_black};
constexpr HighContrastPair high_contrast_line{high_contrast_black, high_contrast_white};
constexpr HighContrastPair high_contrast_cue{high_contrast_black, high_contrast_cyan};

// By ColourRole, in its order. A single role's colour is its foreground;
// its background is the plate it lies on.
constexpr std::array<HighContrastPair, colour_role_count> high_contrast_roles{{
    high_contrast_ink,                                          // ink_on_paper
    high_contrast_ink,                                          // muted_ink_on_paper
    high_contrast_ink,                                          // ink_on_soft_paper
    high_contrast_ink,                                          // ink_on_chrome
    high_contrast_ink,                                          // ink_on_chrome_low
    high_contrast_ink,                                          // ink_on_near_white_stock
    high_contrast_inverse,                                      // selection_ink_on_selection
    high_contrast_ink,                                          // ink_on_gold
    high_contrast_inverse,                                      // on_accent_on_accent
    high_contrast_ink,                                          // signal_on_paper
    high_contrast_ink,                                          // success_on_paper
    high_contrast_ink,                                          // pending_ink_on_tint
    {high_contrast_black, high_contrast_invalid},               // invalid_ink_on_tint
    {high_contrast_black, high_contrast_disabled_ink},          // disabled_ink_on_chrome
    {high_contrast_black, high_contrast_disabled_ink},          // disabled_ink_on_paper
    high_contrast_ink,                                          // phosphor_on_instrument_black
    high_contrast_ink,                                          // command_link_ink_on_paper
    high_contrast_plate,                                        // chrome_highlight
    high_contrast_plate,                                        // chrome
    high_contrast_plate,                                        // chrome_middle
    high_contrast_plate,                                        // chrome_low
    high_contrast_line,                                         // ordinary_line
    high_contrast_line,                                         // dark_line
    high_contrast_plate,                                        // shadow
    high_contrast_cue,                                          // selection_line
    high_contrast_cue,                                          // focus
}};
static_assert(static_cast<std::size_t>(ColourRole::focus) + 1U == colour_role_count,
              "high_contrast_roles lists every colour role");

HighContrastPair high_contrast_role(ColourRole role) noexcept {
    const std::size_t index = static_cast<std::size_t>(role);
    if (index >= colour_role_count) return high_contrast_ink;
    return high_contrast_roles[index];
}

// A surface state's palette: its plate, edge, text and focus ring.
struct HighContrastColours final {
    Color plate{};
    Color edge{};
    Color text{};
    Color focus{};
};

// Read from the roles: the chrome plate, the ordinary line, the ink on
// chrome and the focus cue, unless the state says otherwise.
HighContrastColours high_contrast_colours(ControlSurfaceState state) noexcept {
    HighContrastColours colours{high_contrast_role(ColourRole::chrome).foreground,
                                high_contrast_role(ColourRole::ordinary_line).foreground,
                                high_contrast_role(ColourRole::ink_on_chrome).foreground,
                                high_contrast_role(ColourRole::focus).foreground};
    switch (state) {
    case ControlSurfaceState::hot:
    case ControlSurfaceState::pending:
        colours.edge = high_contrast_role(ColourRole::focus).foreground;
        return colours;
    case ControlSurfaceState::pressed: {
        // Pressed is drawn inverse, as a selection, with its ink as the
        // focus ring (the cue colour does not show on the light plate).
        const HighContrastPair selection = high_contrast_role(ColourRole::selection_ink_on_selection);
        colours = HighContrastColours{selection.background, high_contrast_pressed_edge, selection.foreground,
                                      selection.foreground};
        return colours;
    }
    case ControlSurfaceState::invalid:
        colours.edge = high_contrast_role(ColourRole::invalid_ink_on_tint).foreground;
        return colours;
    case ControlSurfaceState::disabled:
        colours.edge = high_contrast_disabled_edge;
        colours.text = high_contrast_role(ColourRole::disabled_ink_on_chrome).foreground;
        return colours;
    case ControlSurfaceState::normal:
    case ControlSurfaceState::deactivated:
    case ControlSurfaceState::count:
        return colours;
    }
    // A state outside the enumeration takes the plain colours.
    return colours;
}

// One state's derived record: the ordinary record's geometry and cues (its
// corner radii, edge width, ring widths and offsets) kept, its material
// rebuilt as one solid plate inside one solid edge, so that every gradient,
// image, keyline, per-edge border, shadow and glow is dropped. Its text,
// glyph and muted text are `text` (high contrast has no quieter tone: muted
// text must meet 7:1 too), and its focus ring is `focus` (phase 5d: the
// ordinary ring colours failed the high-contrast gate).
ControlVisualRecipe derived_high_contrast(const ControlVisualRecipe& ordinary, Color plate, Color edge,
                                          Color text, Color focus) {
    ControlVisualRecipe result = ordinary;
    const double edge_width = ordinary.material.border ? (*ordinary.material.border).width : 1.0;
    SurfaceMaterial material{};
    material.fills = {MaterialFillLayer::solid(plate)};
    material.border = MaterialBorder{edge, edge_width};
    material.corner_radius = ordinary.material.corner_radius;
    material.corner_radii = ordinary.material.corner_radii;
    result.material = std::move(material);
    result.text = text;
    result.glyph = text;
    result.muted_text = text;
    result.focus_ring = focus;
    return result;
}

// A text shadow (E9.6): its offset within ±maximum_text_shadow_offset and
// its blur within [0, maximum_text_shadow_blur].
bool valid_text_shadow(const std::optional<ControlTextShadow>& shadow) noexcept {
    if (!shadow) return true;
    const Point offset = (*shadow).offset;
    const double blur = (*shadow).blur_radius;
    const bool placed = finite(offset) && std::abs(offset.x) <= maximum_text_shadow_offset &&
                        std::abs(offset.y) <= maximum_text_shadow_offset;
    const bool blurred = std::isfinite(blur) && blur >= 0.0 && blur <= maximum_text_shadow_blur;
    return placed && blurred;
}

// A valid request, and at most one base per backdrop, none for `opaque`
// (the window recipe is its base), each a valid material.
bool valid_window_backdrop(const ThemeWindowBackdrop& backdrop) noexcept {
    if (!window_backdrop_request_valid(backdrop.request)) return false;
    std::array<bool, window_backdrop_count> seen{};
    for (const ThemeBackdropBase& base : backdrop.bases) {
        const std::size_t index = static_cast<std::size_t>(base.backdrop);
        if (index >= window_backdrop_count || base.backdrop == WindowBackdrop::opaque || seen[index]) return false;
        seen[index] = true;
        if (!valid_surface_material(base.material)) return false;
    }
    return true;
}

bool valid_recipe(const ControlVisualRecipe& value) noexcept {
    return valid_surface_material(value.material) && valid_text_shadow(value.text_shadow) &&
           std::isfinite(value.focus_width) && value.focus_width >= 0.0 &&
           value.focus_width <= maximum_recipe_outline_width && std::isfinite(value.focus_offset) &&
           value.focus_offset >= -maximum_recipe_outline_offset &&
           value.focus_offset <= maximum_recipe_outline_offset &&
           (value.authored_focus_outline || value.focus_offset == 0.0) &&
           std::isfinite(value.default_width) &&
           value.default_width >= 0.0 && value.default_width <= 16.0 &&
           finite(value.visual_offset) &&
           std::abs(value.visual_offset.x) <= 32.0 &&
           std::abs(value.visual_offset.y) <= 32.0 &&
           finite(value.pressed_content_offset) &&
           std::abs(value.pressed_content_offset.x) <= 32.0 &&
           std::abs(value.pressed_content_offset.y) <= 32.0;
}

// An image fill layer in its mode.
void paint_image_fill(Painter& painter, Rect bounds, const MaterialFillLayer& fill) {
    switch (fill.image_mode) {
    case MaterialImageMode::stretch:
        paint_stretched_image(painter, bounds, fill);
        return;
    case MaterialImageMode::tile:
        paint_tiled_image(painter, bounds, fill);
        return;
    case MaterialImageMode::nine_patch:
        paint_nine_patch(painter, bounds, fill);
        return;
    case MaterialImageMode::single:
        paint_single_image(painter, bounds, fill);
        return;
    }
    // A mode outside the enumeration draws no image.
    return;
}

// One fill layer of a material over `bounds`; a solid fill follows the
// material's rounded corners when the layers are `clipped` to them.
void paint_fill_layer(Painter& painter, Rect bounds, const MaterialFillLayer& fill,
                      bool clipped, double corner_radius) {
    switch (fill.kind) {
    case MaterialFillKind::solid:
        if (clipped) {
            painter.fill_rounded_rect(bounds, corner_radius, fill.color);
        } else {
            painter.fill_rect(bounds, fill.color);
        }
        return;
    case MaterialFillKind::linear_gradient:
        if (fill.linear_geometry == MaterialLinearGeometry::css_angle) {
            const std::pair<Point, Point> endpoints =
                resolve_css_linear_gradient(fill.angle_degrees, bounds);
            painter.fill_linear_gradient_spread(
                bounds, endpoints.first, endpoints.second, fill.stops,
                fill.spread);
            return;
        }
        painter.fill_linear_gradient_spread(
            bounds, resolve_point(fill.start, fill.coordinate_space, bounds),
            resolve_point(fill.end, fill.coordinate_space, bounds), fill.stops,
            fill.spread);
        return;
    case MaterialFillKind::radial_gradient:
        // The Painter has no spread for a radial gradient, so a repeating
        // radial gradient draws padded here (documented on MaterialFillKind).
        painter.fill_radial_gradient(
            bounds, resolve_point(fill.center, fill.coordinate_space, bounds),
            resolve_radii(fill.radii, fill.coordinate_space, bounds), fill.stops);
        return;
    case MaterialFillKind::image:
        paint_image_fill(painter, bounds, fill);
        return;
    case MaterialFillKind::conic_gradient:
        // The Painter has no conic gradient; its first stop stands in
        // (documented on MaterialFillKind). Validation guarantees a stop.
        if (clipped) {
            painter.fill_rounded_rect(bounds, corner_radius, fill.stops.front().color);
        } else {
            painter.fill_rect(bounds, fill.stops.front().color);
        }
        return;
    case MaterialFillKind::procedural: {
        // The painter path never generates a pattern (E10): it draws the
        // fill's ground, as the engine does until the pattern is ready.
        Color ground = fill.procedural.ground;
        const double alpha = static_cast<double>(ground.alpha) * std::clamp(fill.opacity, 0.0, 1.0);
        ground.alpha = static_cast<std::uint8_t>(std::floor(alpha + 0.5));
        if (clipped) {
            painter.fill_rounded_rect(bounds, corner_radius, ground);
        } else {
            painter.fill_rect(bounds, ground);
        }
        return;
    }
    }
    // A kind outside the enumeration paints nothing.
    return;
}

} // namespace

bool valid_control_visual_recipe(
    const ControlVisualRecipe& value) noexcept {
    return valid_recipe(value);
}

bool valid_control_state_recipes(
    const ControlStateRecipes& value) noexcept {
    return std::all_of(value.values.begin(), value.values.end(),
                       &valid_recipe);
}

ControlStateRecipes ControlStateRecipes::from_parts(
    const ControlVisualRecipe* recipe_values, std::size_t value_count) {
    if (recipe_values == nullptr ||
        value_count != control_surface_state_count) {
        throw std::invalid_argument(
            "control state recipes require one nonnull recipe per retained state");
    }
    ControlStateRecipes result;
    std::copy_n(recipe_values, value_count, result.values.begin());
    if (!valid_control_state_recipes(result)) {
        throw std::invalid_argument("control state recipes contain an invalid recipe");
    }
    return result;
}

namespace {

// One fill layer, of the material's fills or its mask, in its kind.
bool valid_fill_layer(const MaterialFillLayer& fill) noexcept {
    if (fill.coordinate_space != MaterialCoordinateSpace::normalized &&
        fill.coordinate_space != MaterialCoordinateSpace::logical) {
        return false;
    }
    if (fill.blend != MaterialBlendMode::normal && fill.blend != MaterialBlendMode::multiply &&
        fill.blend != MaterialBlendMode::screen) {
        return false;
    }
    switch (fill.kind) {
    case MaterialFillKind::solid:
        if (fill.linear_geometry != MaterialLinearGeometry::endpoints ||
            fill.spread != GradientSpreadMode::pad) return false;
        break;
    case MaterialFillKind::linear_gradient:
        if (fill.spread != GradientSpreadMode::pad &&
            fill.spread != GradientSpreadMode::repeat &&
            fill.spread != GradientSpreadMode::reflect) {
            return false;
        }
        if (fill.linear_geometry != MaterialLinearGeometry::endpoints &&
            fill.linear_geometry != MaterialLinearGeometry::css_angle) {
            return false;
        }
        if (fill.linear_geometry == MaterialLinearGeometry::css_angle) {
            if (fill.coordinate_space != MaterialCoordinateSpace::normalized ||
                !std::isfinite(fill.angle_degrees) ||
                std::abs(fill.angle_degrees) > 360'000.0 ||
                !valid_gradient_stops(fill.stops)) return false;
        } else if (!bounded_coordinate(fill.start, fill.coordinate_space) ||
                   !bounded_coordinate(fill.end, fill.coordinate_space) ||
                   fill.start == fill.end ||
                   !valid_gradient_stops(fill.stops)) {
            return false;
        }
        break;
    case MaterialFillKind::radial_gradient: {
        if (fill.linear_geometry != MaterialLinearGeometry::endpoints ||
            (fill.spread != GradientSpreadMode::pad &&
             fill.spread != GradientSpreadMode::repeat &&
             fill.spread != GradientSpreadMode::reflect)) return false;
        if (!bounded_coordinate(fill.center, fill.coordinate_space) ||
            !finite(fill.radii) || fill.radii.width <= 0.0 ||
            fill.radii.height <= 0.0 ||
            !valid_gradient_stops(fill.stops)) {
            return false;
        }
        const double bound = fill.coordinate_space ==
                MaterialCoordinateSpace::normalized
            ? 16.0 : 1'000'000.0;
        if (fill.radii.width > bound || fill.radii.height > bound) {
            return false;
        }
        break;
    }
    case MaterialFillKind::image:
        if (fill.linear_geometry != MaterialLinearGeometry::endpoints ||
            fill.image.value == 0U || !finite(fill.image_pixel_size) ||
            fill.image_pixel_size.width <= 0.0 ||
            fill.image_pixel_size.height <= 0.0 ||
            std::floor(fill.image_pixel_size.width) !=
                fill.image_pixel_size.width ||
            std::floor(fill.image_pixel_size.height) !=
                fill.image_pixel_size.height ||
            fill.image_pixel_size.width > 32768.0 ||
            fill.image_pixel_size.height > 32768.0 ||
            !finite(fill.image_slice) || fill.image_slice.left < 0.0 ||
            fill.image_slice.top < 0.0 || fill.image_slice.right < 0.0 ||
            fill.image_slice.bottom < 0.0 ||
            fill.image_slice.left + fill.image_slice.right >
                fill.image_pixel_size.width ||
            fill.image_slice.top + fill.image_slice.bottom >
                fill.image_pixel_size.height ||
            !std::isfinite(fill.image_scale) || fill.image_scale < 0.125 ||
            fill.image_scale > 8.0 || !std::isfinite(fill.opacity) ||
            fill.opacity < 0.0 || fill.opacity > 1.0 ||
            !valid_image_position(fill)) {
            return false;
        }
        if (fill.image_mode != MaterialImageMode::stretch &&
            fill.image_mode != MaterialImageMode::tile &&
            fill.image_mode != MaterialImageMode::nine_patch &&
            fill.image_mode != MaterialImageMode::single) {
            return false;
        }
        if (fill.image_mode != MaterialImageMode::nine_patch &&
            fill.image_slice != Insets{}) {
            return false;
        }
        break;
    case MaterialFillKind::conic_gradient:
        // The from-angle is bounded like a CSS linear angle; the period
        // is a positive part of one turn.
        if (fill.linear_geometry != MaterialLinearGeometry::endpoints ||
            (fill.spread != GradientSpreadMode::pad &&
             fill.spread != GradientSpreadMode::repeat &&
             fill.spread != GradientSpreadMode::reflect) ||
            !bounded_coordinate(fill.center, fill.coordinate_space) ||
            !std::isfinite(fill.angle_degrees) ||
            std::abs(fill.angle_degrees) > 360'000.0 ||
            !std::isfinite(fill.period_degrees) ||
            fill.period_degrees <= 0.0 || fill.period_degrees > 360.0 ||
            !valid_gradient_stops(fill.stops)) {
            return false;
        }
        break;
    case MaterialFillKind::procedural:
        if (fill.linear_geometry != MaterialLinearGeometry::endpoints ||
            fill.spread != GradientSpreadMode::pad ||
            !valid_procedural_fill(fill.procedural) ||
            !std::isfinite(fill.opacity) || fill.opacity < 0.0 ||
            fill.opacity > 1.0) {
            return false;
        }
        break;
    default:
        return false;
    }
    return true;
}

} // namespace

namespace {

// Whether every present single edge of `edges` has the same width.
bool equal_edge_widths(const MaterialBorderEdges& edges) noexcept {
    const std::array<const std::optional<MaterialBorder>*, 4> sides{&edges.top, &edges.right, &edges.bottom,
                                                                     &edges.left};
    std::optional<double> width{};
    for (const std::optional<MaterialBorder>* side : sides) {
        if (!*side) continue;
        if (width && *width != (**side).width) return false;
        width = (**side).width;
    }
    return true;
}

} // namespace

bool valid_surface_material(const SurfaceMaterial& material) noexcept {
    if (material.fills.empty() ||
        material.fills.size() > SurfaceMaterial::maximum_fill_layers ||
        material.shadows.size() > SurfaceMaterial::maximum_shadows ||
        material.glows.size() > SurfaceMaterial::maximum_glows ||
        material.keylines.size() > SurfaceMaterial::maximum_keylines ||
        !std::isfinite(material.corner_radius) ||
        material.corner_radius < 0.0 || material.corner_radius > 4096.0) {
        return false;
    }
    if (material.border && !valid_material_border(*material.border)) {
        return false;
    }
    if (material.border && !material.border_edges.empty()) return false;
    // Rounded single edges draw as one ring split at the corners, so their
    // widths are equal.
    if (!material.border_edges.empty() && material.corner_radius != 0.0 &&
        !equal_edge_widths(material.border_edges)) {
        return false;
    }
    // Per-corner radii take no single edges.
    if (material.corner_radii &&
        (!valid_corner_radii(*material.corner_radii) ||
         !material.border_edges.empty())) {
        return false;
    }
    const std::optional<MaterialBorder>* edge_borders[]{
        &material.border_edges.top,
        &material.border_edges.right,
        &material.border_edges.bottom,
        &material.border_edges.left,
    };
    for (const std::optional<MaterialBorder>* edge : edge_borders) {
        if (*edge && !valid_material_border(**edge)) return false;
    }
    for (const MaterialShadow& shadow : material.shadows) {
        if (!valid_material_shadow(shadow)) return false;
    }
    for (const MaterialGlow& glow : material.glows) {
        if (!valid_material_glow(glow)) return false;
    }
    for (const MaterialKeyline& keyline : material.keylines) {
        if (!valid_material_keyline(keyline)) return false;
    }
    if (material.colour_filter && !valid_colour_matrix(*material.colour_filter)) return false;
    const double reach = filter_reach(material);
    if (reach < 0.0 || reach > SurfaceMaterial::maximum_filter_reach) return false;
    for (const MaterialFillLayer& fill : material.fills) {
        if (!valid_fill_layer(fill)) return false;
    }
    // A finish (E9.6) rebuilds a solid or linear first fill and adds one
    // keyline.
    if (!std::isfinite(material.finish) || material.finish < 0.0 || material.finish > 1.0) return false;
    if (material.finish > 0.0) {
        const MaterialFillKind first = material.fills.front().kind;
        const bool derivable = first == MaterialFillKind::solid || first == MaterialFillKind::linear_gradient;
        if (!derivable || material.keylines.size() >= SurfaceMaterial::maximum_keylines) return false;
    }
    // A mask (E9.6) is a solid, gradient or image layer; a procedural
    // pattern is not a mask.
    if (material.mask) {
        const MaterialFillLayer& mask = *material.mask;
        if (mask.kind == MaterialFillKind::procedural || !valid_fill_layer(mask)) return false;
    }
    if (material.reflection && !valid_material_reflection(*material.reflection)) return false;
    return true;
}

void paint_surface_material(Painter& painter, Rect bounds,
                            const SurfaceMaterial& material) {
    if (bounds.empty() || !valid_surface_material(material)) return;
    // A finished material (E9.6) draws as the material it describes.
    if (material.finish > 0.0) {
        const SurfaceMaterial finished = finished_surface_material(material);
        paint_surface_material(painter, bounds, finished);
        return;
    }
    // The painter chooses how: a raster from the material kernel where it
    // composites part rasters, else the layers below.
    painter.draw_surface_material(bounds, material);
}

namespace detail {

// The layers in the material kernel's order (material_primitive.hpp): outer
// shadows and under glows, the fills clipped to the rounded box, inset
// shadows, the border ring, its single edges, keylines and over glows.
void paint_material_layers(Painter& painter, Rect bounds, const SurfaceMaterial& material) {
    for (const MaterialShadow& shadow : material.shadows) {
        if (!shadow.inset) {
            painter.draw_box_shadow(
                bounds, material.corner_radius, shadow.offset,
                shadow.blur_radius, shadow.spread, shadow.color);
        }
    }
    // Under glows over the outer shadows, under the fills; composited as a
    // shadow at no offset (the engine's bake adds them, MaterialGlow).
    for (const MaterialGlow& glow : material.glows) {
        if (glow.placement != MaterialGlowPlacement::under) continue;
        painter.draw_box_shadow(bounds, material.corner_radius, Point{},
                                glow.blur_radius, glow.spread, glow.color);
    }
    const bool clipped = material.corner_radius > 0.0;
    if (clipped) {
        painter.save();
        painter.clip_rounded_rect(bounds, material.corner_radius);
    }
    for (const MaterialFillLayer& fill : material.fills) {
        paint_fill_layer(painter, bounds, fill, clipped, material.corner_radius);
    }
    if (clipped) painter.restore();
    for (const MaterialShadow& shadow : material.shadows) {
        if (shadow.inset) {
            painter.draw_inset_box_shadow(
                bounds, material.corner_radius, shadow.offset,
                shadow.blur_radius, shadow.spread, shadow.color);
        }
    }
    if (material.border) {
        const double inset = (*material.border).width * 0.5;
        painter.stroke_rounded_rect(
            {bounds.x + inset, bounds.y + inset,
             std::max(0.0, bounds.width - inset * 2.0),
             std::max(0.0, bounds.height - inset * 2.0)},
            std::max(0.0, material.corner_radius - inset),
            (*material.border).color, (*material.border).width);
    }
    if (material.border_edges.top) {
        const MaterialBorder& border = *material.border_edges.top;
        const double y = bounds.y + border.width * 0.5;
        painter.draw_line({bounds.x, y}, {bounds.x + bounds.width, y},
                          border.color, border.width);
    }
    if (material.border_edges.right) {
        const MaterialBorder& border = *material.border_edges.right;
        const double x = bounds.x + bounds.width - border.width * 0.5;
        painter.draw_line({x, bounds.y}, {x, bounds.y + bounds.height},
                          border.color, border.width);
    }
    if (material.border_edges.bottom) {
        const MaterialBorder& border = *material.border_edges.bottom;
        const double y = bounds.y + bounds.height - border.width * 0.5;
        painter.draw_line({bounds.x, y}, {bounds.x + bounds.width, y},
                          border.color, border.width);
    }
    if (material.border_edges.left) {
        const MaterialBorder& border = *material.border_edges.left;
        const double x = bounds.x + border.width * 0.5;
        painter.draw_line({x, bounds.y}, {x, bounds.y + bounds.height},
                          border.color, border.width);
    }
    if (!material.keylines.empty() && material.corner_radius > 0.0) {
        painter.save();
        painter.clip_rounded_rect(bounds, material.corner_radius);
    }
    for (const MaterialKeyline& keyline : material.keylines) {
        paint_material_keyline(painter, bounds, keyline);
    }
    if (!material.keylines.empty() && material.corner_radius > 0.0) {
        painter.restore();
    }
    // Over glows last, clipped to the face.
    for (const MaterialGlow& glow : material.glows) {
        if (glow.placement != MaterialGlowPlacement::over) continue;
        painter.save();
        painter.clip_rounded_rect(bounds, material.corner_radius);
        painter.draw_box_shadow(bounds, material.corner_radius, Point{},
                                glow.blur_radius, glow.spread, glow.color);
        painter.restore();
    }
}

} // namespace detail

Insets surface_material_visual_outsets(const SurfaceMaterial& material) noexcept {
    Insets result = box_shadow_visual_outsets(material.shadows);
    // A reflection (T5 C7) reaches past its one edge.
    if (material.reflection) {
        const MaterialReflection& reflection = *material.reflection;
        const double reach = reflection.gap + reflection.reach;
        if (reflection.edge == MaterialEdge::top) result.top = std::max(result.top, reach);
        if (reflection.edge == MaterialEdge::right) result.right = std::max(result.right, reach);
        if (reflection.edge == MaterialEdge::bottom) result.bottom = std::max(result.bottom, reach);
        if (reflection.edge == MaterialEdge::left) result.left = std::max(result.left, reach);
    }
    for (const MaterialGlow& glow : material.glows) {
        // An over glow lies within the box.
        if (glow.placement != MaterialGlowPlacement::under) continue;
        const double reach = glow_reach(glow);
        result.left = std::max(result.left, reach);
        result.top = std::max(result.top, reach);
        result.right = std::max(result.right, reach);
        result.bottom = std::max(result.bottom, reach);
    }
    return result;
}

bool surface_material_opaque(const SurfaceMaterial& material) noexcept {
    if (!valid_surface_material(material) || material.finish != 0.0) return false;
    if (material.corner_radius != 0.0 || material.corner_radii) return false;
    if (!(surface_material_visual_outsets(material) == Insets{})) return false;
    if (material.colour_filter || material.filter_blur != 0.0 || material.drop_shadow || material.mask ||
        material.reflection) {
        return false;
    }
    for (const MaterialFillLayer& fill : material.fills) {
        const bool opaque = fill.kind == MaterialFillKind::solid && fill.color.alpha == 255U &&
            fill.opacity == 1.0 && fill.blend == MaterialBlendMode::normal;
        if (opaque) return true;
    }
    return false;
}

std::optional<double> surface_material_resize_band(const SurfaceMaterial& material) noexcept {
    if (!valid_surface_material(material) || material.finish != 0.0) return std::nullopt;
    if (material.corner_radii) return std::nullopt;
    if (!material.shadows.empty() || !material.glows.empty() || !material.keylines.empty()) return std::nullopt;
    if (!material.border_edges.empty()) return std::nullopt;
    if (!(surface_material_visual_outsets(material) == Insets{})) return std::nullopt;
    if (material.colour_filter || material.filter_blur != 0.0 || material.drop_shadow || material.mask ||
        material.reflection) {
        return std::nullopt;
    }
    for (const MaterialFillLayer& fill : material.fills) {
        if (fill.kind != MaterialFillKind::solid) return std::nullopt;
    }
    double band = std::max(0.0, material.corner_radius);
    if (material.border) {
        // A dashed or dotted border's pattern runs along each edge's length.
        if ((*material.border).style != MaterialBorderStyle::solid) return std::nullopt;
        band = std::max(band, (*material.border).width);
    }
    return band;
}

bool surface_material_size_invariant(const SurfaceMaterial& material) noexcept {
    const std::optional<double> band = surface_material_resize_band(material);
    const bool invariant = band && *band == 0.0;
    return invariant;
}

Insets box_shadow_visual_outsets(std::span<const MaterialShadow> shadows) noexcept {
    Insets result{};
    for (const MaterialShadow& shadow : shadows) {
        if (shadow.inset) continue;
        const double extent = std::max(0.0, shadow.spread) +
                              shadow.blur_radius * 3.0;
        result.left = std::max(result.left,
                               std::max(0.0, extent - shadow.offset.x));
        result.top = std::max(result.top,
                              std::max(0.0, extent - shadow.offset.y));
        result.right = std::max(result.right,
                                std::max(0.0, extent + shadow.offset.x));
        result.bottom = std::max(result.bottom,
                                 std::max(0.0, extent + shadow.offset.y));
    }
    return result;
}

namespace {

Color by_position(const HighContrastColours& colours, ColourPosition position) noexcept {
    switch (position) {
    case ColourPosition::edge: return colours.edge;
    case ColourPosition::text: return colours.text;
    case ColourPosition::fill:
    case ColourPosition::none:
    default: return colours.plate;
    }
}

// A role foreground's colour from `colours`: its text, or its edge where
// it draws an edge.
Color foreground_of(const HighContrastColours& colours, ColourPosition position) noexcept {
    const Color colour = position == ColourPosition::edge ? colours.edge : colours.text;
    return colour;
}

} // namespace

Color high_contrast_colour(ColourOrigin origin, ColourPosition position, ControlSurfaceState state,
                           bool selected) noexcept {
    HighContrastColours colours = high_contrast_colours(state);
    if (selected) {
        const HighContrastPair selection = high_contrast_role(ColourRole::selection_ink_on_selection);
        colours.plate = selection.background;
        colours.text = selection.foreground;
        colours.focus = selection.foreground;
    }
    if (origin.source != ColourSource::role) return by_position(colours, position);
    const bool foreground = origin.side == ColourRoleSide::foreground;
    // The roles with a colour of their own take it from the role table; the
    // ink, chrome and line roles take the state's palette, so that a pressed
    // or selected part inverts.
    const HighContrastPair pair = high_contrast_role(origin.role);
    switch (origin.role) {
    case ColourRole::disabled_ink_on_chrome:
    case ColourRole::disabled_ink_on_paper:
        if (!foreground) return colours.plate;
        return foreground_of(high_contrast_colours(ControlSurfaceState::disabled), position);
    case ColourRole::invalid_ink_on_tint:
        if (!foreground) return colours.plate;
        return pair.foreground;
    case ColourRole::focus: return colours.focus;
    case ColourRole::selection_line: return pair.foreground;
    case ColourRole::selection_ink_on_selection:
    case ColourRole::on_accent_on_accent: return foreground ? pair.foreground : pair.background;
    case ColourRole::chrome_highlight:
    case ColourRole::chrome:
    case ColourRole::chrome_middle:
    case ColourRole::chrome_low:
    case ColourRole::shadow: return colours.plate;
    case ColourRole::ordinary_line:
    case ColourRole::dark_line: return colours.edge;
    case ColourRole::ink_on_paper:
    case ColourRole::muted_ink_on_paper:
    case ColourRole::ink_on_soft_paper:
    case ColourRole::ink_on_chrome:
    case ColourRole::ink_on_chrome_low:
    case ColourRole::ink_on_near_white_stock:
    case ColourRole::ink_on_gold:
    case ColourRole::signal_on_paper:
    case ColourRole::success_on_paper:
    case ColourRole::pending_ink_on_tint:
    case ColourRole::phosphor_on_instrument_black:
    case ColourRole::command_link_ink_on_paper:
    case ColourRole::count:
    default: return foreground ? foreground_of(colours, position) : colours.plate;
    }
}

namespace detail {

void forced_material_fields(const std::array<const MaterialDeclaration*, material_field_count>& resolved,
                            ControlSurfaceState state, bool selected, ForcedMaterialFields& result) {
    result.fields = {};
    result.remapped.clear();
    // Every remapped copy fits, so `fields` never points into storage the
    // vector moved.
    result.remapped.reserve(material_field_count);
    result.any = false;
    for (std::size_t index = 0; index < material_field_count; ++index) {
        const MaterialDeclaration* const declaration = resolved[index];
        if (declaration == nullptr) continue;
        const MaterialField field = static_cast<MaterialField>(index);
        bool kept = false;
        bool remapped = false;
        switch (field) {
        case MaterialField::border_top_style:
        case MaterialField::border_top_width:
        case MaterialField::border_right_style:
        case MaterialField::border_right_width:
        case MaterialField::border_bottom_style:
        case MaterialField::border_bottom_width:
        case MaterialField::border_left_style:
        case MaterialField::border_left_width:
        case MaterialField::corner_top_left:
        case MaterialField::corner_top_right:
        case MaterialField::corner_bottom_right:
        case MaterialField::corner_bottom_left: kept = true; break;
        case MaterialField::background_colour:
        case MaterialField::border_top_colour:
        case MaterialField::border_right_colour:
        case MaterialField::border_bottom_colour:
        case MaterialField::border_left_colour:
        case MaterialField::keylines: remapped = true; break;
        case MaterialField::fills:
        case MaterialField::fill_blend_modes:
        case MaterialField::fill_positions:
        case MaterialField::fill_sizes:
        case MaterialField::fill_repeats:
        case MaterialField::shadows:
        case MaterialField::glows:
        case MaterialField::glow_placements:
        case MaterialField::border_image:
        case MaterialField::colour_filter:
        case MaterialField::drop_shadow:
        case MaterialField::filter_blur:
        case MaterialField::finish:
        case MaterialField::mask:
        case MaterialField::reflection:
        case MaterialField::count:
        default: break; // Not drawn under the pass.
        }
        if (kept) {
            result.fields[index] = declaration;
            result.any = true;
            continue;
        }
        if (!remapped) continue;
        const Color colour =
            high_contrast_colour((*declaration).origin, material_field_colour_position(field), state, selected);
        result.remapped.push_back(*declaration);
        MaterialDeclaration& copy = result.remapped.back();
        copy.colour = colour;
        for (MaterialKeyline& line : copy.keylines) line.color = colour;
        // The colours are the pass's, not the part's text colour.
        copy.current_colours.clear();
        result.fields[index] = &copy;
        result.any = true;
    }
}

} // namespace detail

ControlRoleRecipes derive_high_contrast_recipes(ControlRoleRecipes recipes, ControlVisualRole family) {
    // Families whose text does not lie on a plate of their own never invert
    // (T4 phase 5e): a container's children paint over its plate in their
    // own states (a pressed or toggled card's white plate hid its
    // children's white text), a choice's label lies on its parent's ground
    // (a checked check box's black ink lay on black), and an editor's text
    // lies on its well, which does not invert (a pressed combo box's).
    // Pressed keeps the plain plate and ink inside the pressed edge, and
    // selected the state's palette inside the selection line.
    const bool container = family == ControlVisualRole::window || family == ControlVisualRole::panel ||
                           family == ControlVisualRole::card || family == ControlVisualRole::choice ||
                           family == ControlVisualRole::editor;
    const HighContrastPair selection = high_contrast_role(ColourRole::selection_ink_on_selection);
    for (std::size_t index = 0; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        HighContrastColours colours = high_contrast_colours(state);
        if (container && state == ControlSurfaceState::pressed) {
            const HighContrastColours plain = high_contrast_colours(ControlSurfaceState::normal);
            colours.plate = plain.plate;
            colours.text = plain.text;
            colours.focus = plain.focus;
        }
        const ControlVisualRecipe& ordinary = recipes.ordinary[index];
        recipes.high_contrast[index] = derived_high_contrast(ordinary, colours.plate, colours.edge, colours.text,
                                                             colours.focus);
        if (container) {
            const Color line = high_contrast_role(ColourRole::selection_line).foreground;
            recipes.high_contrast_selected[index] =
                derived_high_contrast(ordinary, colours.plate, line, colours.text, colours.focus);
            continue;
        }
        // Selected: the selection's plate inside the state's edge, with its
        // ink.
        recipes.high_contrast_selected[index] =
            derived_high_contrast(ordinary, selection.background, colours.edge, selection.foreground,
                                  selection.foreground);
    }
    return recipes;
}

std::shared_ptr<const Theme> Theme::create(const ThemeDefinition& definition) {
    std::unique_ptr<Theme> theme = validated(definition);
    (*theme).source_ = detail::empty_theme_source();
    std::shared_ptr<const Theme> result(std::move(theme));
    return result;
}

std::shared_ptr<const detail::ThemeSourceRecord> detail::empty_theme_source() {
    static const std::shared_ptr<const ThemeSourceRecord> empty = std::make_shared<const ThemeSourceRecord>();
    return empty;
}

std::shared_ptr<const Theme> detail::ThemeAccess::create_holding(
    const ThemeDefinition& definition, std::vector<std::shared_ptr<const ThemeImageLease>> images,
    std::shared_ptr<const ThemeSourceRecord> source) {
    std::unique_ptr<Theme> theme = Theme::validated(definition);
    (*theme).images_ = std::move(images);
    (*theme).source_ = source != nullptr ? std::move(source) : empty_theme_source();
    std::shared_ptr<const Theme> result(std::move(theme));
    return result;
}

std::optional<ImageId> Theme::icon_override(std::string_view identifier) const noexcept {
    for (const detail::ThemeSourceImage& icon : (*source_).icons) {
        if (icon.resource == identifier) return icon.image;
    }
    return std::nullopt;
}

const detail::ThemeSourceRecord& detail::ThemeAccess::source(const Theme& theme) noexcept {
    return *theme.source_;
}

std::unique_ptr<Theme> Theme::validated(const ThemeDefinition& definition) {
    if (definition.id.empty() || definition.id.size() > 128U) {
        throw std::invalid_argument("theme id must contain 1 to 128 bytes");
    }
    for (const ControlRoleRecipes& role : definition.roles) {
        if (!std::all_of(role.ordinary.begin(), role.ordinary.end(),
                         &valid_recipe) ||
            !std::all_of(role.selected.begin(), role.selected.end(),
                         &valid_recipe)) {
            throw std::invalid_argument("theme contains an invalid visual recipe");
        }
    }
    if (definition.accent_ramp.empty() || definition.accent_ramp.size() > 256U) {
        throw std::invalid_argument("theme accent ramp must hold 1 to 256 colours");
    }
    if (!valid_overlay_shadow(definition.overlay_shadow) ||
        !valid_overlay_shadow(definition.high_contrast_overlay_shadow)) {
        throw std::invalid_argument(
            "theme overlay shadow must hold at most four valid outer layers");
    }
    if (!valid_structure(definition.structure)) {
        throw std::invalid_argument(
            "theme structural tokens must be finite, ordered, and bounded");
    }
    if (!valid_window_backdrop(definition.window_backdrop)) {
        throw std::invalid_argument(
            "theme window backdrop needs a valid request, no opaque base and valid base materials");
    }
    const ColourSchemeSet schemes =
        definition.rules.colour_schemes ? *definition.rules.colour_schemes : definition.colour_schemes;
    if (!schemes.light && !schemes.dark) {
        throw std::invalid_argument("theme must have a light or a dark variant");
    }
    std::unique_ptr<Theme> theme(new Theme(definition));
    // A sheet's `color-scheme` is the theme's (T4 theme variants).
    (*theme).definition_.colour_schemes = schemes;
    // The forced high-contrast pass: every family's high-contrast records
    // are derived from its ordinary ones, replacing whatever was authored.
    // It reads only the roles, so the engine compiled from the rules stands.
    for (std::size_t index = 0; index < (*theme).definition_.roles.size(); ++index) {
        ControlRoleRecipes& role = (*theme).definition_.roles[index];
        role = derive_high_contrast_recipes(std::move(role), static_cast<ControlVisualRole>(index));
    }
    // The dark pass (T4 phase 5): derived when the definition asks for it,
    // kept when it authored them, otherwise the ordinary records, so a
    // dark window keeps its colours.
    const bool derive_dark = definition.dark_variant == ThemeDarkVariant::derived;
    const bool authored_dark = definition.dark_variant == ThemeDarkVariant::authored;
    for (std::size_t index = 0; index < (*theme).definition_.roles.size(); ++index) {
        ControlRoleRecipes& role = (*theme).definition_.roles[index];
        if (authored_dark) continue;
        if (derive_dark) {
            role = derive_dark_recipes(std::move(role), static_cast<ControlVisualRole>(index));
        } else {
            role.dark = role.ordinary;
            role.dark_selected = role.selected;
        }
    }
    return theme;
}

namespace detail {

void fill_ancestor_facts(const Control& control, std::vector<AncestorFacts>& chain, AncestorBloom& bloom) {
    chain.clear();
    bloom = AncestorBloom{};
    Control::Ptr parent = control.parent();
    while (parent != nullptr && chain.size() < maximum_ancestor_depth) {
        const Control& above = *parent;
        AncestorFacts facts{};
        facts.control_type = above.anatomy().control_id;
        facts.stable_id = above.stable_id().value();
        facts.classes = above.classes();
        chain.push_back(facts);
        bloom.add({}, facts.control_type);
        if (!facts.stable_id.empty()) bloom.add("#", facts.stable_id);
        for (const std::string& name : facts.classes) bloom.add(".", name);
        parent = above.parent();
    }
}

void attach_ancestor_facts(const Control& control, const CompiledThemeRules& rules,
                           std::vector<AncestorFacts>& chain, CompiledThemeRules::PartFacts& facts) {
    const bool user = facts.user_rules != nullptr && (*facts.user_rules).uses_ancestors();
    const bool owned = facts.inline_rules != nullptr && (*facts.inline_rules).uses_ancestors();
    if (!rules.uses_ancestors() && !user && !owned) return;
    fill_ancestor_facts(control, chain, facts.ancestor_bloom);
    facts.ancestors = chain;
}

} // namespace detail

std::string_view control_visual_role_name(ControlVisualRole role) noexcept {
    switch (role) {
    case ControlVisualRole::window: return "window";
    case ControlVisualRole::panel: return "panel";
    case ControlVisualRole::card: return "card";
    case ControlVisualRole::button: return "button";
    case ControlVisualRole::accent_button: return "accent_button";
    case ControlVisualRole::command_button: return "command_button";
    case ControlVisualRole::choice: return "choice";
    case ControlVisualRole::editor: return "editor";
    case ControlVisualRole::menu_item: return "menu_item";
    case ControlVisualRole::selection: return "selection";
    case ControlVisualRole::progress: return "progress";
    case ControlVisualRole::tool_tip: return "tool_tip";
    case ControlVisualRole::link: return "link";
    case ControlVisualRole::visited_link: return "visited_link";
    case ControlVisualRole::notice: return "notice";
    case ControlVisualRole::badge: return "badge";
    case ControlVisualRole::count: return {};
    }
    return {};
}

const ControlVisualRecipe& Theme::resolve(
    ControlVisualRole role, ControlVisualContext context) const noexcept {
    std::size_t role_index = static_cast<std::size_t>(role);
    std::size_t state_index = static_cast<std::size_t>(context.surface);
    if (role_index >= definition_.roles.size()) role_index = 0U;
    if (state_index >= control_surface_state_count) state_index = 0U;
    const ControlRoleRecipes& recipes = definition_.roles[role_index];
    if (context.high_contrast) {
        return context.selected ? recipes.high_contrast_selected[state_index]
                                : recipes.high_contrast[state_index];
    }
    if (context.dark) {
        return context.selected ? recipes.dark_selected[state_index] : recipes.dark[state_index];
    }
    return context.selected ? recipes.selected[state_index]
                            : recipes.ordinary[state_index];
}

namespace {

// The anatomy states a visual context shows (Theme::resolve's first form).
AnatomyStateSet visual_context_states(ControlVisualContext context) noexcept {
    AnatomyStateSet states{};
    states.insert(AnatomyState::normal);
    switch (context.surface) {
    case ControlSurfaceState::hot: states.insert(AnatomyState::hot); break;
    case ControlSurfaceState::pressed: states.insert(AnatomyState::pressed); break;
    case ControlSurfaceState::pending: states.insert(AnatomyState::pending); break;
    case ControlSurfaceState::invalid: states.insert(AnatomyState::invalid); break;
    case ControlSurfaceState::deactivated: states.insert(AnatomyState::deactivated); break;
    case ControlSurfaceState::normal:
    case ControlSurfaceState::disabled:
    case ControlSurfaceState::count: break;
    // A value outside the enumeration adds no state.
    default: break;
    }
    if (context.surface == ControlSurfaceState::disabled) {
        states.insert(AnatomyState::disabled);
    } else {
        states.insert(AnatomyState::enabled);
    }
    if (context.selected) states.insert(AnatomyState::selected);
    if (context.focused) {
        states.insert(AnatomyState::focused);
        states.insert(AnatomyState::focus_visible);
    }
    if (context.defaulted) states.insert(AnatomyState::preselect);
    return states;
}

// The declaration of `field` that `fields` sets, or null where it sets
// none or declares the field `initial` (the floor's value).
const RecipeDeclaration* set_recipe_field(const CompiledThemeRules::RecipeResolution& fields,
                                          RecipeField field) noexcept {
    const RecipeDeclaration* const declaration = fields.fields[static_cast<std::size_t>(field)];
    if (declaration == nullptr || (*declaration).initial) return nullptr;
    return declaration;
}

// The text colour `control` inherits (T3 P8c, owner decision D7): its
// parent control's cascade result, followed up the tree while that
// inherits too; `fallback` where a control's cascade sets no text colour,
// or sets its initial, or at the root.
Color inherited_text(const Control& control, Color fallback) {
    Control::Ptr parent = control.parent();
    while (parent != nullptr) {
        const Control& above = *parent;
        CompiledThemeRules::PartFacts facts{};
        facts.control_type = above.anatomy().control_id;
        facts.stable_id = above.stable_id().value();
        facts.classes = above.classes();
        facts.inline_rules = above.inline_style_rules();
        facts.states = above.anatomy_states();
        const SurfaceRoot* const window = above.attached_surface_root();
        if (window != nullptr) facts.media = window_media_facts(*window);
        if (window != nullptr) facts.user_rules = (*window).user_theme_rules();
        std::vector<AncestorFacts> ancestors{};
        detail::attach_ancestor_facts(above, above.effective_theme().engine().rules(), ancestors, facts);
        const CompiledThemeRules::RecipeResolution fields =
            above.effective_theme().engine().rules().resolve_recipe_fields(facts);
        const RecipeDeclaration* const text = fields.fields[static_cast<std::size_t>(RecipeField::text)];
        if (text == nullptr || (*text).initial) return fallback;
        if (!(*text).inherit) return (*text).colour;
        parent = above.parent();
    }
    return fallback;
}

// The recipe fields `fields` sets, over `recipe` (the floor's copy), in
// place. The outline's style comes first: a ring has no offset, and no
// outline no width.
void apply_recipe_fields(const CompiledThemeRules::RecipeResolution& fields, ControlVisualRecipe& recipe) noexcept {
    const RecipeDeclaration* const text = set_recipe_field(fields, RecipeField::text);
    if (text != nullptr) recipe.text = (*text).colour;
    const RecipeDeclaration* const muted = set_recipe_field(fields, RecipeField::muted_text);
    if (muted != nullptr) recipe.muted_text = (*muted).colour;
    const RecipeDeclaration* const glyph = set_recipe_field(fields, RecipeField::glyph);
    if (glyph != nullptr) recipe.glyph = (*glyph).colour;
    const RecipeDeclaration* const default_width = set_recipe_field(fields, RecipeField::default_outline_width);
    if (default_width != nullptr) recipe.default_width = (*default_width).number;
    const RecipeDeclaration* const default_colour = set_recipe_field(fields, RecipeField::default_outline_colour);
    if (default_colour != nullptr) recipe.default_ring = (*default_colour).colour;
    const RecipeDeclaration* const style = set_recipe_field(fields, RecipeField::outline_style);
    const bool no_outline = style != nullptr && (*style).outline_style == RecipeOutlineStyle::none;
    if (style != nullptr) {
        switch ((*style).outline_style) {
        case RecipeOutlineStyle::ring:
            recipe.authored_focus_outline = false;
            recipe.dotted_focus = false;
            recipe.focus_offset = 0.0;
            break;
        case RecipeOutlineStyle::solid:
            recipe.authored_focus_outline = true;
            recipe.dotted_focus = false;
            break;
        case RecipeOutlineStyle::dotted:
            recipe.authored_focus_outline = true;
            recipe.dotted_focus = true;
            break;
        case RecipeOutlineStyle::none: recipe.focus_width = 0.0; break;
        // A value outside the enumeration, which the rules refuse
        // (recipe_field_invalid), keeps the floor's outline.
        default: break;
        }
    }
    const RecipeDeclaration* const colour = set_recipe_field(fields, RecipeField::outline_colour);
    if (colour != nullptr) recipe.focus_ring = (*colour).colour;
    const RecipeDeclaration* const width = set_recipe_field(fields, RecipeField::outline_width);
    if (width != nullptr && !no_outline) recipe.focus_width = (*width).number;
    const RecipeDeclaration* const offset = set_recipe_field(fields, RecipeField::outline_offset);
    if (offset != nullptr && recipe.authored_focus_outline) recipe.focus_offset = (*offset).number;
    const RecipeDeclaration* const shadow = set_recipe_field(fields, RecipeField::text_shadow);
    if (shadow == nullptr) return;
    if (!(*shadow).shadowed) {
        recipe.text_shadow.reset();
        return;
    }
    ControlTextShadow written{};
    written.offset = (*shadow).shadow_offset;
    written.blur_radius = (*shadow).shadow_blur;
    written.color = (*shadow).colour;
    recipe.text_shadow = written;
}

} // namespace

namespace detail {

void fill_family_recipes(const CompiledThemeRules& rules, ControlVisualRole role, const ControlVisualRecipe& base,
                         ControlRoleRecipes& recipes) {
    recipes = ControlRoleRecipes{};
    // A unit box: the sheet's geometry does not depend on it (header).
    const Size unit_box{1.0, 1.0};
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        for (std::size_t chosen = 0U; chosen < 2U; ++chosen) {
            ControlVisualContext context{};
            context.surface = static_cast<ControlSurfaceState>(index);
            context.selected = chosen == 1U;
            CompiledThemeRules::PartFacts facts{};
            facts.family = control_visual_role_name(role);
            facts.states = visual_context_states(context);
            const CompiledThemeRules::Resolution resolution = rules.resolve(facts);
            const CompiledThemeRules::RecipeResolution fields = rules.resolve_recipe_fields(facts);
            ControlVisualRecipe value = base;
            assemble_material(base.material, resolution.materials, unit_box, value.material);
            apply_recipe_fields(fields, value);
            ControlVisualRecipe& target = context.selected ? recipes.selected[index] : recipes.ordinary[index];
            target = std::move(value);
        }
    }
}

} // namespace detail

ResolvedControlRecipe Theme::resolve(const Control& control, ControlVisualRole role,
                                     ControlVisualContext context) const {
    const AnatomyStateSet states = visual_context_states(context);
    ResolvedControlRecipe result = resolve(control, role, context, states);
    return result;
}

void Theme::provenance(const Control& control, ControlVisualRole role, ControlVisualContext context,
                       CompiledThemeRules::PartProvenance& result) const {
    const bool forced = context.high_contrast;
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = control.anatomy().control_id;
    facts.stable_id = control.stable_id().value();
    facts.classes = control.classes();
    facts.inline_rules = control.inline_style_rules();
    facts.states = visual_context_states(context);
    facts.forced_high_contrast = forced;
    facts.family = control_visual_role_name(role);
    const SurfaceRoot* const window = control.attached_surface_root();
    if (window != nullptr) facts.media = window_media_facts(*window);
    if (window != nullptr) facts.user_rules = (*window).user_theme_rules();
    std::vector<AncestorFacts> ancestors{};
    detail::attach_ancestor_facts(control, engine_.rules(), ancestors, facts);
    engine_.rules().resolve_provenance(facts, result);
    result.parameters = {};
    result.parameter_count = 0U;
    if (!forced) return;
    // The pass draws every field no important inline declaration sets, and
    // its colours stand over every declaration's (resolve() above).
    CompiledThemeRules::FieldProvenance pass{};
    pass.level = CascadeLevel::forced_high_contrast;
    pass.layer = ThemeLayer::forced_high_contrast;
    const std::array<RecipeField, 6> pass_colours{RecipeField::text,        RecipeField::outline_colour,
                                                  RecipeField::text_shadow, RecipeField::muted_text,
                                                  RecipeField::glyph,       RecipeField::default_outline_colour};
    for (const RecipeField field : pass_colours) result.recipe_fields[static_cast<std::size_t>(field)] = pass;
    for (CompiledThemeRules::FieldProvenance& field : result.recipe_fields) {
        if (!field.authored) field = pass;
    }
    for (CompiledThemeRules::FieldProvenance& field : result.materials) {
        if (!field.authored) field = pass;
    }
}

ResolvedFont Theme::resolve_font(const Control& control, FontSpec own) const noexcept {
    ResolvedFont result{};
    result.font = own;
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = control.anatomy().control_id;
    facts.stable_id = control.stable_id().value();
    facts.classes = control.classes();
    facts.inline_rules = control.inline_style_rules();
    facts.states = visual_context_states(ControlVisualContext{});
    const Window* const window = control.attached_window();
    if (window != nullptr) facts.media = window_media_facts(*window);
    if (window != nullptr) facts.user_rules = (*window).user_theme_rules();
    std::vector<AncestorFacts> ancestors{};
    detail::attach_ancestor_facts(control, engine_.rules(), ancestors, facts);
    const CompiledThemeRules::RecipeResolution fields = engine_.rules().resolve_recipe_fields(facts);
    if (!fields.authored) return result;
    ResolvedFont themed = result;
    const std::size_t first = static_cast<std::size_t>(RecipeField::font_family);
    for (std::size_t index = 0U; index < font_field_count; ++index) {
        const RecipeField field = static_cast<RecipeField>(first + index);
        const RecipeDeclaration* const declaration = set_recipe_field(fields, field);
        if (declaration == nullptr) continue;
        themed.themed[index] = true;
        themed.levels[index] = fields.levels[first + index];
    }
    // The size before the spacing, whose em is the resolved size.
    const RecipeDeclaration* const family = set_recipe_field(fields, RecipeField::font_family);
    if (family != nullptr) themed.font.role = (*family).font_role;
    const RecipeDeclaration* const size = set_recipe_field(fields, RecipeField::font_size);
    if (size != nullptr) {
        double resolved = (*size).number;
        if (!(*size).em_lengths.empty()) resolved += (*size).em_lengths[0] * own.size;
        themed.font.size = resolved;
    }
    const RecipeDeclaration* const weight = set_recipe_field(fields, RecipeField::font_weight);
    if (weight != nullptr) themed.font.weight = static_cast<std::uint16_t>((*weight).number);
    const RecipeDeclaration* const style = set_recipe_field(fields, RecipeField::font_style);
    if (style != nullptr) themed.font.italic = (*style).italic;
    const RecipeDeclaration* const spacing = set_recipe_field(fields, RecipeField::letter_spacing);
    if (spacing != nullptr) {
        double resolved = (*spacing).number;
        if (!(*spacing).em_lengths.empty()) resolved += (*spacing).em_lengths[0] * themed.font.size;
        themed.font.letter_spacing = resolved;
    }
    const bool sized =
        themed.font.size >= minimum_rule_font_size && themed.font.size <= maximum_rule_font_size;
    if (!sized || !valid_font_spec(themed.font)) return result;
    return themed;
}

std::optional<BackdropChain> Theme::window_backdrop_chain(const Window& window) const noexcept {
    const Control::Ptr root = window.root();
    CompiledThemeRules::PartFacts facts{};
    facts.surface = SurfaceKind::window;
    facts.stable_id = (*root).stable_id().value();
    facts.classes = (*root).classes();
    facts.media = window_media_facts(window);
    facts.user_rules = window.user_theme_rules();
    const BackdropDeclaration* const declared = engine_.rules().resolve_backdrop(facts);
    if (declared == nullptr || (*declared).length == 0U) return std::nullopt;
    // The compiler and add_backdrop admitted the declaration, and the two
    // chains share their capacity and ranges, so every operation appends.
    static_assert(BackdropDeclaration::maximum_steps == BackdropChain::maximum_operations);
    BackdropChain chain{};
    for (std::size_t index = 0U; index < (*declared).length; ++index) {
        const BackdropStep& step = (*declared).steps[index];
        BackdropOperation operation = BackdropOperation::tint(step.colour);
        if (step.kind == BackdropStepKind::blur) operation = BackdropOperation::blur(step.amount);
        if (step.kind == BackdropStepKind::saturate) operation = BackdropOperation::saturate(step.amount);
        static_cast<void>(chain.append(operation));
    }
    if (!backdrop_chain_valid(chain)) return std::nullopt;
    return chain;
}

bool Theme::declares_clarity(const Control& control) const noexcept {
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = control.anatomy().control_id;
    facts.stable_id = control.stable_id().value();
    facts.classes = control.classes();
    facts.inline_rules = control.inline_style_rules();
    facts.states = visual_context_states(ControlVisualContext{});
    const Window* const window = control.attached_window();
    if (window != nullptr) facts.media = window_media_facts(*window);
    if (window != nullptr) facts.user_rules = (*window).user_theme_rules();
    std::vector<AncestorFacts> ancestors{};
    detail::attach_ancestor_facts(control, engine_.rules(), ancestors, facts);
    const CompiledThemeRules::Resolution resolution = engine_.rules().resolve(facts);
    if (!resolution.materials_authored) return false;
    const MaterialDeclaration* const colour =
        resolution.materials[static_cast<std::size_t>(MaterialField::background_colour)];
    const MaterialDeclaration* const layers = resolution.materials[static_cast<std::size_t>(MaterialField::fills)];
    if (colour == nullptr || layers == nullptr || (*colour).initial || (*layers).initial) return false;
    if ((*colour).colour.alpha != 0U) return false;
    // Every declared layer must show nothing: a solid layer of alpha 0.
    for (const MaterialFillLayer& layer : (*layers).fills) {
        const bool invisible = layer.kind == MaterialFillKind::solid && layer.color.alpha == 0U;
        if (!invisible) return false;
    }
    return true;
}

ResolvedControlRecipe Theme::resolve(const Control& control, ControlVisualRole role, ControlVisualContext context,
                                     AnatomyStateSet states) const {
    ResolvedControlRecipe result = resolve(control, role, context, states, std::string_view{});
    return result;
}

ResolvedControlRecipe Theme::resolve(const Control& control, ControlVisualRole role, ControlVisualContext context,
                                     AnatomyStateSet states, std::string_view part) const {
    const ControlVisualRecipe& floor = resolve(role, context);
    // Forced high contrast is the engine's own pass (E1): the built-in
    // recipe's high-contrast records draw the part, under the non-colour
    // fields of an important inline style alone.
    const bool forced = context.high_contrast;
    if (forced && control.inline_style_rules() == nullptr) return ResolvedControlRecipe(floor);
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = control.anatomy().control_id;
    facts.stable_id = control.stable_id().value();
    facts.classes = control.classes();
    facts.inline_rules = control.inline_style_rules();
    facts.part = part;
    facts.states = states;
    facts.forced_high_contrast = forced;
    facts.family = control_visual_role_name(role);
    const SurfaceRoot* const window = control.attached_surface_root();
    if (window != nullptr) facts.media = window_media_facts(*window);
    if (window != nullptr) facts.user_rules = (*window).user_theme_rules();
    std::vector<AncestorFacts> ancestors{};
    detail::attach_ancestor_facts(control, engine_.rules(), ancestors, facts);
    CompiledThemeRules::RecipeResolution fields = engine_.rules().resolve_recipe_fields(facts);
    if (forced) {
        // The pass's colours stand over every declaration's, and it draws
        // no text shadow.
        fields.fields[static_cast<std::size_t>(RecipeField::text)] = nullptr;
        fields.fields[static_cast<std::size_t>(RecipeField::outline_colour)] = nullptr;
        fields.fields[static_cast<std::size_t>(RecipeField::text_shadow)] = nullptr;
        fields.fields[static_cast<std::size_t>(RecipeField::muted_text)] = nullptr;
        fields.fields[static_cast<std::size_t>(RecipeField::glyph)] = nullptr;
        fields.fields[static_cast<std::size_t>(RecipeField::default_outline_colour)] = nullptr;
        bool any = false;
        for (const RecipeDeclaration* field : fields.fields) any = any || field != nullptr;
        fields.authored = any;
    }
    if (!fields.authored) return ResolvedControlRecipe(floor);
    // The text colour first (T3 P8c): an inherited one is the parent
    // control's, and currentcolor names the result.
    Color text = floor.text;
    const RecipeDeclaration* const written = set_recipe_field(fields, RecipeField::text);
    if (written != nullptr) text = (*written).inherit ? inherited_text(control, floor.text) : (*written).colour;
    // The fields relative to the control, copied: an inherited text colour,
    // currentcolor, and lengths in em at the control's em.
    std::vector<RecipeDeclaration> scaled{};
    scaled.reserve(recipe_field_count);
    bool reads_em = false;
    for (const RecipeDeclaration*& field : fields.fields) {
        if (field == nullptr || (*field).initial) continue;
        const bool relative = (*field).inherit || (*field).current_colour || !(*field).em_lengths.empty();
        if (!relative) continue;
        scaled.push_back(*field);
        RecipeDeclaration& copy = scaled.back();
        if (copy.inherit || copy.current_colour) copy.colour = text;
        copy.inherit = false;
        copy.current_colour = false;
        if (!copy.em_lengths.empty()) reads_em = true;
        field = &copy;
    }
    if (reads_em) {
        const FontDeclaration font = engine_.rules().resolve(facts).font;
        const double em = text_role_em(control, resolved_text_role(control, font));
        for (RecipeDeclaration& field : scaled) add_recipe_em(field, em);
    }
    ControlVisualRecipe authored = floor;
    apply_recipe_fields(fields, authored);
    if (!valid_recipe(authored)) return ResolvedControlRecipe(floor);
    const std::size_t text_position = static_cast<std::size_t>(RecipeField::text);
    const bool important_text = fields.fields[text_position] != nullptr &&
                                fields.levels[text_position] >= CascadeLevel::important_user_theme;
    return ResolvedControlRecipe(floor, std::move(authored), important_text);
}

ThemeDefinition windows_professional_theme_definition() {
    ThemeDefinition result;
    const Color ink = Color::rgba(27, 39, 51);
    for (ControlRoleRecipes& role : result.roles) {
        fill_professional_role(role, Color::rgba(250, 252, 254),
                                 Color::rgba(225, 231, 237),
                                 Color::rgba(132, 148, 163), ink);
    }
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::window)],
        Color::rgba(246, 248, 250),
                          Color::rgba(246, 248, 250),
                          Color::rgba(155, 166, 177), ink, 0.0);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::panel)],
        Color::rgba(240, 244, 248),
                          Color::rgba(232, 237, 242),
                          Color::rgba(148, 162, 175), ink, 0.0);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::card)],
        Color::rgba(255, 255, 255),
                          Color::rgba(244, 247, 250),
                          Color::rgba(151, 166, 179), ink, 6.0);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::accent_button)],
        Color::rgba(72, 145, 213),
                          Color::rgba(31, 100, 177),
                          Color::rgba(20, 75, 145), Color::rgba(255, 255, 255));
    fill_accent_states(result.roles[static_cast<std::size_t>(ControlVisualRole::accent_button)]);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::command_button)],
        Color::rgba(252, 253, 255),
                          Color::rgba(229, 236, 244),
                          Color::rgba(126, 147, 166), ink, 2.0);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::editor)],
        Color::rgba(255, 255, 255),
                          Color::rgba(255, 255, 255),
                          Color::rgba(118, 139, 158), ink, 2.0);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::menu_item)],
        Color::rgba(255, 255, 255, 0),
                          Color::rgba(255, 255, 255, 0),
                          Color::rgba(148, 162, 175, 0), ink, 2.0);
    // Menu and command-strip items are flat at rest (design language: strip
    // commands may be flat when dividers and strip shading carry the
    // structure): a disabled or deactivated item keeps the transparent
    // resting plate and only its text changes. Hot, pressed and selected
    // items still draw a plate.
    ControlRoleRecipes& menu_items =
        result.roles[static_cast<std::size_t>(ControlVisualRole::menu_item)];
    const SurfaceMaterial resting =
        menu_items.ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)].material;
    menu_items.ordinary[static_cast<std::size_t>(ControlSurfaceState::disabled)].material = resting;
    menu_items.ordinary[static_cast<std::size_t>(ControlSurfaceState::deactivated)].material = resting;
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::selection)],
        Color::rgba(255, 255, 255, 0),
                          Color::rgba(255, 255, 255, 0),
                          Color::rgba(148, 162, 175, 0), ink, 0.0);
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::progress)],
        Color::rgba(238, 242, 246),
                          Color::rgba(220, 227, 234),
                          Color::rgba(118, 139, 158), ink, 2.0);
    ControlRoleRecipes& progress =
        result.roles[static_cast<std::size_t>(ControlVisualRole::progress)];
    for (ControlVisualRecipe& selected : progress.selected) {
        selected.text = Color::rgba(255, 255, 255);
        selected.glyph = Color::rgba(255, 255, 255);
        selected.muted_text = Color::rgba(255, 255, 255, 130);
    }
    // Tooltips (owner decision RB-38): the face and edge ToolTip drew before
    // it had a family, a pale yellow square plate with a grey line and
    // near-black text. A tooltip takes no input, so its hot and pressed
    // recipes are never reached.
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::tool_tip)],
        Color::rgba(255, 255, 225),
                          Color::rgba(255, 255, 225),
                          Color::rgba(104, 111, 118),
                          Color::rgba(24, 31, 38), 0.0);
    fill_link_role(result.roles[static_cast<std::size_t>(ControlVisualRole::link)],
                   result.compatibility.link, result.compatibility.disabled_text);
    fill_link_role(result.roles[static_cast<std::size_t>(ControlVisualRole::visited_link)],
                   result.compatibility.visited_link, result.compatibility.disabled_text);
    // Notices (T4, W-4): a white plate with a quiet line; the severity
    // keyline is the control's (InfoBanner, Toast). A notice takes no input
    // of its own, so its hot and pressed recipes are never reached.
    fill_professional_role(result.roles[static_cast<std::size_t>(ControlVisualRole::notice)],
        Color::rgba(255, 255, 255),
                          Color::rgba(250, 251, 252),
                          Color::rgba(151, 166, 179), ink, 2.0);
    fill_badge_role(result.roles[static_cast<std::size_t>(ControlVisualRole::badge)]);
    result.overlay_shadow = provisional_overlay_shadow();
    result.high_contrast_overlay_shadow.clear();
    return result;
}

std::shared_ptr<const Theme> default_theme() {
    static const std::shared_ptr<const Theme> value = Theme::create(windows_professional_theme_definition());
    return value;
}

} // namespace gui_forms
