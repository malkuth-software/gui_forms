#pragma once

#include <cstdint>

// The material primitive's bake policy: the one place where the effort
// budget and the effort weights it is checked with are set (owner
// direction, 2026-10-22; in effort, not time, owner 2026-10-23).
//
// A part's raster is baked once, when it is rebuilt after an invalidation
// (a change of state, size, scale or theme), and then cached; compositing
// it afterwards costs a blit, plus a group blend where one applies, however
// complex the material. So a material's complexity costs only at bake time,
// and the bake is bounded so that no invalidation can stall: a material
// whose bake effort exceeds maximum_material_bake_effort is refused, the
// part is drawn by the built-in recipe instead, and the refusal is counted
// (Metrics::record_engine_primitive_refusal).
//
// Effort, not time (owner, 2026-10-23: "we should cure effort, not time"):
// the bake effort is the destination's device pixels (the part's box grown
// by the primitive's outsets) times the sum of the material's layers'
// effort weights, plus the rounded outer shadows' corner pixels times
// theirs. Weights and budget are dimensionless integers fixed in code, so
// a material is accepted or refused identically on every platform and
// every machine, however fast. Measurements choose only the weights'
// relative sizes (gui_forms_engine_material_tests --measure prints each
// layer kind's work relative to a solid layer's); no measurement, clock or
// duration decides acceptance.

namespace gui_forms::engine_primitives {

// An amount of bake work in effort units: dimensionless, the sum of the
// effort weights of the work done, per device pixel. One unit is the least
// weight charged, a gradient stop's past the first two; a solid layer is 25.
struct BakeEffort final {
    std::uint64_t units{};

    friend constexpr bool operator==(BakeEffort left, BakeEffort right) noexcept {
        return left.units == right.units;
    }
    friend constexpr bool operator<(BakeEffort left, BakeEffort right) noexcept {
        return left.units < right.units;
    }
    friend constexpr bool operator<=(BakeEffort left, BakeEffort right) noexcept {
        return left.units <= right.units;
    }
    friend constexpr bool operator>(BakeEffort left, BakeEffort right) noexcept {
        return left.units > right.units;
    }
};

// The most effort one bake may take (the bake budget). It replaces the
// 4 ms bound of the reference machine (accepted by the owner, 2026-10-22)
// at the weights below, which are those measurements scaled to whole
// units, so that every material accepted or refused under that bound
// keeps its outcome (notes:planning/stages/T2-engine-core/README.md, "Bake
// effort"). Change it here.
inline constexpr BakeEffort maximum_material_bake_effort{20'000'000U};

// Effort weights: the relative work of each layer kind, in effort units.
struct MaterialEffortWeights final {
    // Per device pixel of the destination: the pixel loop, the box's
    // coverage and the store, with no layer drawn.
    std::uint32_t base{};
    // Per device pixel, for each layer of its kind.
    std::uint32_t solid{};
    std::uint32_t linear{};
    std::uint32_t radial{};
    std::uint32_t conic{};
    // Per device pixel, for each gradient stop past the first two.
    std::uint32_t gradient_stop{};
    // Per device pixel, for each outer shadow: its separable integrals.
    std::uint32_t outer_shadow{};
    // Per device pixel within a rounded outer shadow's corners and its
    // reach, for each such shadow: its signed distance (E9).
    std::uint32_t outer_corner{};
    std::uint32_t inset_shadow{};
    std::uint32_t border{};
    // Per device pixel, for a dashed or dotted border, past a solid one's.
    std::uint32_t border_pattern{};
    std::uint32_t border_edge{};
    std::uint32_t keyline{};
    // Per device pixel, for each image fill: compositing its prepared
    // variant's colour. The variant's decode and resampling are not the
    // bake's (image_variant.hpp).
    std::uint32_t image{};
    // Per device pixel of the destination, for each part filter (E9.6): the
    // colour matrix, the drop shadow (its alpha blurred and composited) and
    // the blur (every channel blurred). A box blur's work per pixel does
    // not depend on its radius, but it blurs an image grown by 3h on every
    // side, which is not charged: on a small part with a wide blur the
    // charge is low by that ratio, on a part far within the budget.
    std::uint32_t colour_filter{};
    std::uint32_t drop_shadow{};
    std::uint32_t filter_blur{};
    // Per device pixel, once for a material whose fills blend other than
    // source-over (E9.6): gathering them into one face.
    std::uint32_t fill_blend{};
    // Per device pixel, for a mask (E9.6): its pass over the finished
    // raster, past evaluating the mask, which is charged as a fill of its
    // kind.
    std::uint32_t mask{};
    // Per device pixel of the destination, for a reflection (stage T5, C7):
    // its copy of at most its reach of lines, charged over the whole
    // destination, as the mask's pass is.
    std::uint32_t reflection{};
};

// The weights' relative sizes come from the quiet-machine measurements of
// 2026-10-23 (gui_forms_engine_material_tests --measure: for each layer
// kind, the median of nine bakes of a 256 × 256 destination at scale 1,
// less the base material's, per pixel; seven runs; recorded in
// notes:planning/stages/T2-engine-core/README.md, "Bake costs on a quiet
// machine"), each median rounded up and scaled so that the least, a
// gradient stop, is one unit. border_pattern is per pixel of a dashed ring
// whose every pixel is on an arc, its dearest, and is charged per
// destination pixel. mask covers a solid mask's pass and a linear one's.
inline constexpr MaterialEffortWeights material_effort_weights{
    .base = 90U,
    .solid = 25U,
    .linear = 65U,
    .radial = 75U,
    .conic = 230U,
    .gradient_stop = 1U,
    .outer_shadow = 30U,
    .outer_corner = 140U,
    .inset_shadow = 245U,
    .border = 100U,
    .border_pattern = 195U,
    .border_edge = 35U,
    .keyline = 35U,
    .image = 45U,
    .colour_filter = 80U,
    .drop_shadow = 135U,
    .filter_blur = 240U,
    .fill_blend = 140U,
    .mask = 55U,
    .reflection = 55U,
};

} // namespace gui_forms::engine_primitives
