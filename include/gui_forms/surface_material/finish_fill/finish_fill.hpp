#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>

namespace gui_forms {

// The surface finishes (skeudoindustrial surfaces, phase 1;
// notes:planning/SKEUDOINDUSTRIAL_SURFACES.md): painted and blasted metal
// and woven cloth, each a height field, an albedo and a sheen lit by one
// directional light. They are ports of the industrial-surfaces study's
// finish kernel (notes:experiments/industrial-surfaces/finish_kernel.js,
// cpp/surface_kernels.cpp), whose JavaScript stays the byte reference
// (tools/industrial_surfaces/finish_kernel.js).
//
// A finish needs no bitmap: every device pixel is a function of its
// position on the surface, the display scale and these parameters alone,
// so any rectangle of a surface repaints to exactly the bytes a whole
// repaint gives.
enum class FinishKind : std::uint8_t {
    // Flat enamel under settled dust: soft mottling and pale specks.
    dusty_matte,
    // Satin powder coat that flowed and cured with a soft peel.
    orange_peel,
    // Hammered-metal enamel: shallow bowls, raised rims, aluminium flake.
    hammertone,
    // Skinned-over enamel that shrank into fine ridges as it cured.
    wrinkle,
    // Bare steel peened with glass beads; draws its own steel colour
    // (#8b9194), not `paint`.
    bead_blast,
    // Casting skin under paint: coarse grain with scattered pits.
    sand_cast,
    // The paint colour as a plain solid (the study's flat baseline).
    flat,
    // A plain-woven cloth: warp and weft over and under, threads 3
    // logical units apart, with slubbed yarn. `paint` is the warp's colour
    // and `secondary` the weft's.
    cloth_weave,
    // Blasted metal seen as glints (skeudoindustrial surfaces, phase 2):
    // a field of tiny facets, each bright where it mirrors the light into
    // the eye, drawn from their statistics (`glint`, a fitted preset). An
    // optional effect: drawn only where a theme asks for it. It is still,
    // as every finish is: its light and eye are fixed above the surface's
    // centre, so it never animates and has nothing for reduced motion to
    // hold. Costlier than the other finishes (finish_effort_weights): the
    // bake budget refuses it over large surfaces.
    glint,
};

// The glint's presets, fitted by the industrial-surfaces study against
// photographs of blasted discs (glint_kernel.js, PRESETS) at display scale
// 2 with the light 900 and the eye 700 logical units above the surface.
enum class GlintPreset : std::uint8_t {
    // Many facets a pixel: a satin with faint, even grain (fitted; metal
    // #a5a5a5).
    bead_blast,
    // The same fine grain with twice the glint energy (fitted; #a3a3a3).
    sand_blast,
    // Larger specks, strong glints, colour from a thin oxide film (fitted;
    // #a6a5a7).
    iridescent,
    // Facets wider than a pixel: distinct, flat glinting chips (not fitted;
    // #9f9f9f).
    coarse_grit,
    // The sand blast under a thin satin coat of `paint` (not fitted;
    // #a3a3a3).
    painted_sand,
};

// One finish's parameters. The defaults are the study's: its paint (#2f6e6c,
// a teal), its dust (#bdb6a8), a light from 125 degrees (counter-clockwise
// from the right) at 38 degrees above the surface, and ambient 0.32.
//
// - `seed` chooses the noise; equal parameters give equal bytes.
// - `grain` multiplies every feature's size, and the relief with it, so
//   that a coarser finish keeps its slopes; within [0.5, 4].
// - `paint` is the finish's colour; `secondary` the dust's (dusty matte,
//   sand cast) or the weft's (cloth). Both are drawn opaque: their alpha is
//   not read; the fill layer's opacity composites the finish.
// - For the glint, `secondary` is the metal's colour (the presets' own are
//   listed with them), `paint` the coat's (painted sand alone draws it),
//   `gloss` scales the glints' energy and `grain` the facets and specks;
//   the light, ambient and relief are the preset's fitted geometry.
// - The light's azimuth is a whole number of degrees in [0, 359] and its
//   elevation one in [0, 90]: the kernel reads each degree's cosine and
//   sine from a table, never libm.
// - `ambient` in [0, 1]; `relief` and `gloss` scale the finish's own relief
//   and sheen, within [0, 4].
struct GUI_FORMS_API FinishFill final {
    FinishKind kind{FinishKind::orange_peel};
    std::int32_t seed{7};
    double grain{1.0};
    Color paint{Color::rgba(0x2f, 0x6e, 0x6c)};
    Color secondary{Color::rgba(0xbd, 0xb6, 0xa8)};
    std::int32_t light_azimuth_degrees{125};
    std::int32_t light_elevation_degrees{38};
    double ambient{0.32};
    double relief{1.0};
    double gloss{1.0};
    GlintPreset glint{GlintPreset::sand_blast};
    friend constexpr bool operator==(const FinishFill& left, const FinishFill& right) noexcept {
        return left.kind == right.kind && left.seed == right.seed && left.grain == right.grain &&
               left.paint == right.paint && left.secondary == right.secondary &&
               left.light_azimuth_degrees == right.light_azimuth_degrees &&
               left.light_elevation_degrees == right.light_elevation_degrees && left.ambient == right.ambient &&
               left.relief == right.relief && left.gloss == right.gloss && left.glint == right.glint;
    }
};

// The bounds valid_surface_material holds a finish to.
inline constexpr double minimum_finish_grain = 0.5;
inline constexpr double maximum_finish_grain = 4.0;
inline constexpr double maximum_finish_scale = 4.0;
inline constexpr std::int32_t maximum_finish_elevation_degrees = 90;

} // namespace gui_forms
