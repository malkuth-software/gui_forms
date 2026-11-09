#pragma once

#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>

namespace gui_forms {

// The built-in procedural patterns (T2 plan, E10; design language §4.3 and
// §4.5). A theme chooses a pattern and sets its parameters; it never
// supplies generator code.
enum class ProceduralPattern : std::uint8_t {
    // Fine pile: four octaves of value noise, cells of 8, 4, 2 and 1
    // logical units.
    felt,
    // Rings across the tile's height, warped by low-frequency noise, with
    // a fine fibre streak along them.
    wood_grain,
    // Faint formation noise with short seeded fibres in every direction.
    paper_fibre,
    // Horizontal lines, one every `tile` logical units, each the keyline
    // width max(1, round(min(scale, 2))) device pixels; no noise, so the
    // seed is not read.
    pinstripe,
    // Long horizontal streaks over a finer horizontal grain.
    brushed_metal,
    // The box's own extent, not a tile: a three-stop vertical gradient
    // (ground, figure, accent) and three broad seeded blooms of `bloom`.
    fresco,
    // A checkerboard: the tile's top-left and bottom-right quarters the
    // ground, the other two the figure (mixed by strength); no noise. A
    // 2-unit tile is a 50% dither of logical pixels, as a Windows scroll
    // track's pattern brush.
    checker,
};

// One procedural fill's parameters. The pattern is generated per display
// scale from these alone, in integer arithmetic, so that its bytes are the
// same on every host; each scale is generated, never resampled from
// another.
//
// - `seed` chooses the noise; equal parameters give equal bytes.
// - `tile` is the pattern's repeat in logical units: the side of its
//   square tile, round(tile × scale) device pixels, within [2, 256]; a
//   pinstripe's period. The fresco covers the box and does not read it.
// - `strength` in [0, 1] scales how far the figure departs from the
//   ground; for the fresco, its blooms' opacity.
// - The ground is the pattern's base colour, and what the fill draws, as
//   a solid, until its pattern is ready; the figure is its contrasting
//   colour (fibres, grain, stripes, streaks); the fresco also reads
//   `accent`, its gradient's last stop, and `bloom`, whose alpha is its
//   blooms' peak opacity.
struct ProceduralFill final {
    ProceduralPattern pattern{ProceduralPattern::felt};
    std::uint32_t seed{};
    double tile{64.0};
    double strength{1.0};
    Color ground{Color::rgba(128, 128, 128)};
    Color figure{Color::rgba(96, 96, 96)};
    Color accent{Color::rgba(128, 128, 128)};
    Color bloom{Color::rgba(255, 255, 255, 0)};
    friend constexpr bool operator==(const ProceduralFill& left, const ProceduralFill& right) noexcept {
        return left.pattern == right.pattern && left.seed == right.seed && left.tile == right.tile &&
               left.strength == right.strength && left.ground == right.ground && left.figure == right.figure &&
               left.accent == right.accent && left.bloom == right.bloom;
    }
};

// The smallest and largest tile, in logical units.
inline constexpr double minimum_procedural_tile = 2.0;
inline constexpr double maximum_procedural_tile = 256.0;

} // namespace gui_forms
