#pragma once

#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gui_forms {

// A border's line (E9.6): CSS `solid`, `dashed` or `dotted`. A dash is
// three widths long with a gap of two; a dot is a round dot one width
// across, one every two widths. Each runs along the border's centre line
// from the top-left corner's end, clockwise, and its period is stretched
// so that a whole number of dashes or dots goes round. The engine's
// material kernel draws them on a uniform border, for a part's face and for
// paint_surface_material where it bakes; a minimal painter's layers draw
// every border solid, and the kernel draws a single edge
// (MaterialBorderEdges) solid.
enum class MaterialBorderStyle : std::uint8_t {
    solid,
    dashed,
    dotted,
};

struct MaterialBorder final {
    Color color{Color::rgba(76, 94, 111)};
    double width{1.0};
    MaterialBorderStyle style{MaterialBorderStyle::solid};
    friend constexpr bool operator==(const MaterialBorder& left,
                                     const MaterialBorder& right) noexcept {
        return left.color == right.color && left.width == right.width &&
               left.style == right.style;
    }
};

// A drop shadow of the part's own drawn alpha (E9.6, CSS `filter:
// drop-shadow()`): the baked raster's alpha, blurred by `blur_radius`
// (σ = blur / 2, as a shadow's), moved by `offset`, in `color`, drawn
// beneath the raster. Unlike a MaterialShadow it follows whatever the
// material draws, such as a glyph or a round shape.
struct MaterialDropShadow final {
    Point offset{0.0, 1.0};
    double blur_radius{2.0};
    Color color{Color::rgba(0, 0, 0, 96)};
    friend constexpr bool operator==(const MaterialDropShadow& left,
                                     const MaterialDropShadow& right) noexcept {
        return left.offset == right.offset &&
               left.blur_radius == right.blur_radius && left.color == right.color;
    }
};

// A material's four corner radii (E9.6, CSS `border-*-radius`), in logical
// units: top-left, top-right, bottom-right and bottom-left. Each is drawn
// at most half the box's shorter side.
struct MaterialCornerRadii final {
    double top_left{};
    double top_right{};
    double bottom_right{};
    double bottom_left{};
    friend constexpr bool operator==(const MaterialCornerRadii& left,
                                     const MaterialCornerRadii& right) noexcept {
        return left.top_left == right.top_left &&
               left.top_right == right.top_right &&
               left.bottom_right == right.bottom_right &&
               left.bottom_left == right.bottom_left;
    }
};
struct MaterialShadow final {
    Point offset{0.0, 2.0};
    double blur_radius{4.0};
    double spread{};
    Color color{Color::rgba(20, 30, 42, 72)};
    bool inset{};
    friend constexpr bool operator==(const MaterialShadow& left,
                                     const MaterialShadow& right) noexcept {
        return left.offset == right.offset &&
               left.blur_radius == right.blur_radius &&
               left.spread == right.spread && left.color == right.color &&
               left.inset == right.inset;
    }
};
// Where a glow lies in its material (owner, E9.4 follow-up):
// - `under`: a halo beneath the face, which the fills cover; it marks the
//   current selection, such as a selected tab or the item under arrow-key
//   focus;
// - `over`: light added over the face, clipped to the face's shape; it
//   marks other emphasis, such as the default button (never in place of
//   the default action's perimeter, M105).
enum class MaterialGlowPlacement : std::uint8_t { under, over };

// A soft coloured emission of the material's box (E9-C): the box grown by
// `spread` on every side and blurred by `blur_radius` (σ = blur_radius / 2,
// as a shadow's), centred on the box. The engine's material bake adds it
// as light, each premultiplied channel at most 1:
// - `under`, to the outer shadows beneath it, with the fills drawn over
//   it.
// - `over`, to the finished face (after its keylines), within the box's
//   coverage.
// paint_surface_material adds them so too where it bakes through the
// kernel. A minimal painter's layers composite each as an outer shadow at
// no offset (an over glow clipped to the face, after the keylines): that is
// source-over, not added, so its under glow differs where it lies over a
// shadow and its over glow is the dimmer.
// Its reach, spread + 2 × blur_radius (where its blur is below one 8-bit
// step), is at most SurfaceMaterial::maximum_glow_reach in both
// placements, so that an under glow stays within the part's granted
// overflow; an over glow lies within the box and needs no overflow.
struct MaterialGlow final {
    Color color{Color::rgba(255, 255, 255, 96)};
    double blur_radius{2.0};
    double spread{};
    MaterialGlowPlacement placement{MaterialGlowPlacement::under};
    friend constexpr bool operator==(const MaterialGlow& left,
                                     const MaterialGlow& right) noexcept {
        return left.color == right.color &&
               left.blur_radius == right.blur_radius &&
               left.spread == right.spread && left.placement == right.placement;
    }
};
enum class MaterialEdge : std::uint8_t { top, right, bottom, left };

// One ordered physical keyline. An inset of zero places the stroke immediately
// inside the owned edge; positive insets allow a separate highlight or lowlight
// to sit behind/inside an outer boundary without requiring a nested control.
struct MaterialKeyline final {
    MaterialEdge edge{MaterialEdge::top};
    Color color{Color::rgba(255, 255, 255, 128)};
    double width{1.0};
    double inset{};
    friend constexpr bool operator==(const MaterialKeyline& left,
                                     const MaterialKeyline& right) noexcept {
        return left.edge == right.edge && left.color == right.color &&
               left.width == right.width && left.inset == right.inset;
    }
};
// What a reflection copies (stage T5, PLAN.md §4.4).
enum class MaterialReflectionSource : std::uint8_t {
    // The part's finished face within its box (fills, borders, keylines,
    // glows, filters and mask, as the engine baked them; text is not
    // reflected, owner T5-Q14).
    face,
    // The window's backplane under the box, as glass samples it (the
    // backplane plane alone, never other controls). A part in the backplane
    // plane, or a window that lends no backplane, draws no reflection.
    backplane,
};
// A reflection (stage T5, C7; owner T5-Q14, T5-Q15): `source` within the
// part's box, mirrored about the box's `edge`, `gap` logical units beyond
// it, for `reach` units, faded by a linear opacity from `start_opacity`
// next to the edge to `end_opacity` at the far end. gap + reach is at most
// SurfaceMaterial::maximum_reflection_reach, within the part's granted
// overflow; reach is above 0 and both opacities lie in [0, 1]. The
// reflection is composited beneath what the face drew past that edge (its
// outer shadows and glows). The engine's material primitive draws it, and
// omits it under high contrast and reduced transparency (T5-Q16); the
// built-in path, paint_surface_material, draws none.
struct MaterialReflection final {
    MaterialEdge edge{MaterialEdge::bottom};
    double gap{};
    double reach{8.0};
    double start_opacity{0.5};
    double end_opacity{};
    MaterialReflectionSource source{MaterialReflectionSource::face};
    friend constexpr bool operator==(const MaterialReflection& left,
                                     const MaterialReflection& right) noexcept {
        return left.edge == right.edge && left.gap == right.gap &&
               left.reach == right.reach &&
               left.start_opacity == right.start_opacity &&
               left.end_opacity == right.end_opacity &&
               left.source == right.source;
    }
};
struct MaterialBorderEdges final {
    std::optional<MaterialBorder> top;
    std::optional<MaterialBorder> right;
    std::optional<MaterialBorder> bottom;
    std::optional<MaterialBorder> left;
    [[nodiscard]] static MaterialBorderEdges from_parts(
        const MaterialBorder* top, const MaterialBorder* right,
        const MaterialBorder* bottom, const MaterialBorder* left);
    [[nodiscard]] bool empty() const noexcept {
        return !top && !right && !bottom && !left;
    }
    friend bool operator==(const MaterialBorderEdges& left_value,
                           const MaterialBorderEdges& right_value) noexcept {
        return left_value.top == right_value.top &&
               left_value.right == right_value.right &&
               left_value.bottom == right_value.bottom &&
               left_value.left == right_value.left;
    }
};
struct SurfaceMaterial final {
    static constexpr std::size_t maximum_fill_layers = 8U;
    static constexpr std::size_t maximum_shadows = 4U;
    static constexpr std::size_t maximum_keylines = 8U;
    static constexpr std::size_t maximum_glows = 2U;
    // The farthest a glow reaches outside the box, in logical units: the
    // engine material's granted overflow on every side.
    static constexpr double maximum_glow_reach = 8.0;
    // The farthest the part filters reach outside the box, in logical
    // units: the drop shadow's larger offset component plus 1.5 × its blur,
    // plus 1.5 × the filter blur. The engine material's outsets hold it.
    static constexpr double maximum_filter_reach = 8.0;
    // The farthest a reflection reaches outside the box, gap and reach
    // together, in logical units (T5-Q15: the blur and glow cap).
    static constexpr double maximum_reflection_reach = 8.0;
    std::vector<MaterialFillLayer> fills{
        MaterialFillLayer::solid(Color::rgba(255, 255, 255))};
    std::vector<MaterialShadow> shadows;
    std::vector<MaterialGlow> glows;
    std::optional<MaterialBorder> border;
    MaterialBorderEdges border_edges;
    std::vector<MaterialKeyline> keylines;
    double corner_radius{};
    // Corners that differ (E9.6): when present they replace corner_radius
    // in the engine's material primitive. The built-in path,
    // paint_surface_material, rounds every corner by corner_radius, so a
    // theme sets that to the radius the fallback should draw.
    std::optional<MaterialCornerRadii> corner_radii;
    // The part filters (E9.6, CSS `filter`), applied to the baked raster in
    // this order: the colour filter to each pixel, then the drop shadow
    // beneath, then the blur over the whole. A theme's filter list lowers
    // to that order. The engine's material primitive draws them; the
    // built-in path, paint_surface_material, draws none of them, and the
    // window's backplane image draws only the colour filter.
    MaterialColourFilter colour_filter;
    std::optional<MaterialDropShadow> drop_shadow;
    // CSS `filter: blur(σ)` as filter_blur = 2σ logical units, the blur
    // radius convention of shadows; 0 for none.
    double filter_blur{};
    // The surface finish (E9.6, CSS coverage row 9), from 0, the material
    // as authored, to 1, a full gloss derived from its first fill
    // (finished_surface_material, which both paths draw). A finish above 0
    // needs a solid or linear first fill and room for one more keyline.
    double finish{};
    // An alpha mask (E9.6, CSS `mask-image`): a solid, gradient or image
    // fill whose alpha, times its opacity for an image, multiplies the
    // whole finished part, its shadows and glows included; outside the box
    // it extends at its edge value, so that a shadow fades with the face
    // beside it. The engine's material primitive draws it; the built-in
    // path, paint_surface_material, draws the material unmasked.
    std::optional<MaterialFillLayer> mask;
    // A reflection of the finished face (MaterialReflection), drawn after
    // the mask; none by default.
    std::optional<MaterialReflection> reflection;
    [[nodiscard]] static SurfaceMaterial from_parts(
        const MaterialFillLayer* fills, std::size_t fill_count,
        const MaterialShadow* shadows, std::size_t shadow_count,
        const MaterialBorder* border, double corner_radius);
    [[nodiscard]] static SurfaceMaterial from_parts(
        const MaterialFillLayer* fills, std::size_t fill_count,
        const MaterialShadow* shadows, std::size_t shadow_count,
        const MaterialBorder* border, const MaterialBorderEdges* border_edges,
        double corner_radius);
    [[nodiscard]] static SurfaceMaterial from_parts(
        const MaterialFillLayer* fills, std::size_t fill_count,
        const MaterialShadow* shadows, std::size_t shadow_count,
        const MaterialBorder* border, const MaterialBorderEdges* border_edges,
        const MaterialKeyline* keylines, std::size_t keyline_count,
        double corner_radius);
    friend bool operator==(const SurfaceMaterial& left,
                           const SurfaceMaterial& right) noexcept(noexcept(
        left.fills == right.fills && left.shadows == right.shadows &&
        left.glows == right.glows &&
        left.border == right.border && left.border_edges == right.border_edges &&
        left.keylines == right.keylines &&
        left.corner_radius == right.corner_radius &&
        left.corner_radii == right.corner_radii &&
        left.colour_filter == right.colour_filter &&
        left.drop_shadow == right.drop_shadow &&
        left.filter_blur == right.filter_blur && left.finish == right.finish &&
        left.mask == right.mask && left.reflection == right.reflection)) {
        return left.fills == right.fills && left.shadows == right.shadows &&
               left.glows == right.glows &&
               left.border == right.border &&
               left.border_edges == right.border_edges &&
               left.keylines == right.keylines &&
               left.corner_radius == right.corner_radius &&
               left.corner_radii == right.corner_radii &&
               left.colour_filter == right.colour_filter &&
               left.drop_shadow == right.drop_shadow &&
               left.filter_blur == right.filter_blur &&
               left.finish == right.finish && left.mask == right.mask &&
               left.reflection == right.reflection;
    }
};

} // namespace gui_forms
