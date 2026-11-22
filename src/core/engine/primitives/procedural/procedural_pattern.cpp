#include "procedural_pattern.hpp"

#include "../kernel/pixel_kernel.hpp"
#include "../material/material_bake_policy.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <new>
#include <optional>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
// The pixel loops are integer; the few doubles are read before them.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

// 1.0 in 16.16 fixed point, and the largest 16-bit fraction.
constexpr std::uint32_t fixed_one = 65536U;
constexpr std::uint32_t fraction_mask = 0xFFFFU;
// Spreads consecutive indices over the hash (2^32 / φ).
constexpr std::uint32_t golden = 0x9E3779B9U;

// lowbias32 (C. Wellons): a 32-bit integer hash. Unsigned arithmetic wraps
// modulo 2^32 on every host.
std::uint32_t mix(std::uint32_t value) noexcept {
    std::uint32_t mixed = value;
    mixed = mixed ^ (mixed >> 16U);
    mixed = mixed * 0x7FEB352DU;
    mixed = mixed ^ (mixed >> 15U);
    mixed = mixed * 0x846CA68BU;
    mixed = mixed ^ (mixed >> 16U);
    return mixed;
}

// The noise key of `seed`'s octave `octave`.
std::uint32_t octave_key(std::uint32_t seed, std::uint32_t octave) noexcept {
    const std::uint32_t key = mix(seed + octave * golden);
    return key;
}

// The value at lattice point (across, down) under `key`, in [0, 65535].
std::uint32_t lattice(std::uint32_t key, std::uint32_t across, std::uint32_t down) noexcept {
    const std::uint32_t row = mix(down + golden);
    const std::uint32_t point = mix(across ^ row);
    const std::uint32_t value = mix(key ^ point) >> 16U;
    return value;
}

// How many cells of `cell` logical units fit across a tile of `tile`
// logical units, rounded, at least one.
std::uint32_t cells_across(double tile, double cell) noexcept {
    const double count = tile / cell + 0.5;
    const std::uint32_t cells = std::max(static_cast<std::uint32_t>(count), 1U);
    return cells;
}

// A 16-bit fraction smoothed by 3f² − 2f³, still in [0, 65535]. The
// product is below 2^50.
std::uint32_t smooth(std::uint32_t fraction) noexcept {
    const std::uint64_t f = fraction;
    const std::uint64_t smoothed = (f * f * (3U * std::uint64_t{fixed_one} - 2U * f)) >> 32U;
    return static_cast<std::uint32_t>(smoothed);
}

// The 16.16 position of pixel `index`'s centre along an axis of `length`
// pixels holding `cells` cells: ((2 index + 1) × cells × 2^16) / (2 length).
std::uint64_t centre_position(std::uint32_t index, std::uint32_t length, std::uint32_t cells) noexcept {
    const std::uint64_t doubled = 2U * std::uint64_t{index} + 1U;
    const std::uint64_t position = (doubled * cells * fixed_one) / (2U * std::uint64_t{length});
    return position;
}

// Fills `cells` and `weights` from `offset` with each pixel's lattice cell
// and smoothed fraction along an axis of `length` pixels holding `count`
// cells. The vectors hold offset + length entries.
void fill_axis(std::uint32_t length, std::uint32_t count, std::size_t offset, std::vector<std::uint32_t>& cells,
               std::vector<std::uint32_t>& weights) noexcept {
    for (std::uint32_t index = 0U; index < length; ++index) {
        const std::uint64_t position = centre_position(index, length, count);
        cells[offset + index] = static_cast<std::uint32_t>(position >> 16U);
        weights[offset + index] = smooth(static_cast<std::uint32_t>(position & fraction_mask));
    }
}

// One octave of periodic value noise: its cells across and down the tile,
// its key, and its weight in the sum.
struct Octave final {
    std::uint32_t across{};
    std::uint32_t down{};
    std::uint32_t key{};
    std::uint32_t weight{};
};

// An octave of cells `across_logical` × `down_logical` logical units.
Octave make_octave(const ProceduralFill& fill, std::uint32_t index, double across_logical, double down_logical,
                   std::uint32_t weight) noexcept {
    Octave octave{};
    octave.across = cells_across(fill.tile, across_logical);
    octave.down = cells_across(fill.tile, down_logical);
    octave.key = octave_key(fill.seed, index);
    octave.weight = weight;
    return octave;
}

// The most octaves a pattern sums.
constexpr std::size_t maximum_octaves = 4U;

// A pattern's octaves, `count` of them live, and the sum of their weights;
// where each octave's two lattice rows lie in the scratch, and which cell
// row they hold now (none before the first pixel row).
struct OctaveSet final {
    std::array<Octave, maximum_octaves> octaves{};
    std::size_t count{};
    std::uint32_t total_weight{};
    std::array<std::size_t, maximum_octaves> row_offsets{};
    std::array<std::optional<std::uint32_t>, maximum_octaves> rows_for{};
};

void add_octave(OctaveSet& set, const Octave& octave) noexcept {
    set.octaves[set.count] = octave;
    set.count = set.count + 1U;
    set.total_weight = set.total_weight + octave.weight;
}

// Fills the scratch's axes for `set` over a `width` × `height` tile,
// octave-major, and sizes its lattice rows: two rows of `across` values
// for each octave. Allocates; nothing is written to the destination.
void prepare_axes(OctaveSet& set, std::uint32_t width, std::uint32_t height, ProceduralScratch& scratch) {
    scratch.across_cells.resize(set.count * width);
    scratch.across_weights.resize(set.count * width);
    scratch.down_cells.resize(set.count * height);
    scratch.down_weights.resize(set.count * height);
    std::size_t lattice_values = 0U;
    for (std::size_t index = 0U; index < set.count; ++index) {
        const Octave& octave = set.octaves[index];
        fill_axis(width, octave.across, index * width, scratch.across_cells, scratch.across_weights);
        fill_axis(height, octave.down, index * height, scratch.down_cells, scratch.down_weights);
        set.row_offsets[index] = lattice_values;
        set.rows_for[index].reset();
        lattice_values = lattice_values + 2U * std::size_t{octave.across};
    }
    scratch.lattice_rows.resize(lattice_values);
}

// The next cell along an axis of `count` cells, wrapping.
std::uint32_t next_cell(std::uint32_t cell, std::uint32_t count) noexcept {
    const std::uint32_t next = cell + 1U == count ? 0U : cell + 1U;
    return next;
}

// Before pixel row `y` of a tile `height` pixels high: holds, for each
// octave, the lattice values along the cell rows above and below the row,
// at every cell across. An octave whose cell row is the one held already
// is left as it is, so each value is hashed once per cell row, not once
// per pixel.
void prepare_row(OctaveSet& set, ProceduralScratch& scratch, std::uint32_t height, std::uint32_t y) noexcept {
    for (std::size_t index = 0U; index < set.count; ++index) {
        const Octave& octave = set.octaves[index];
        const std::uint32_t top = scratch.down_cells[index * height + y];
        if (set.rows_for[index] && *set.rows_for[index] == top) continue;
        const std::uint32_t bottom = next_cell(top, octave.down);
        const std::size_t upper = set.row_offsets[index];
        const std::size_t lower = upper + octave.across;
        for (std::uint32_t cell = 0U; cell < octave.across; ++cell) {
            scratch.lattice_rows[upper + cell] = lattice(octave.key, cell, top);
            scratch.lattice_rows[lower + cell] = lattice(octave.key, cell, bottom);
        }
        set.rows_for[index] = top;
    }
}

// Octave `index`'s value at pixel (x, y) of a `width` × `height` tile,
// after prepare_row for row y: the four lattice values around the pixel
// blended by its smoothed fractions, in [0, 65535].
std::uint64_t octave_value(const OctaveSet& set, std::size_t index, const ProceduralScratch& scratch,
                           std::uint32_t width, std::uint32_t height, std::uint32_t x, std::uint32_t y) noexcept {
    const Octave& octave = set.octaves[index];
    const std::size_t column = index * width + x;
    const std::uint32_t left = scratch.across_cells[column];
    const std::uint32_t right = next_cell(left, octave.across);
    const std::uint64_t sx = scratch.across_weights[column];
    const std::uint64_t sy = scratch.down_weights[index * height + y];
    const std::size_t upper = set.row_offsets[index];
    const std::size_t lower = upper + octave.across;
    const std::uint64_t above = (std::uint64_t{scratch.lattice_rows[upper + left]} * (fixed_one - sx) +
                                 std::uint64_t{scratch.lattice_rows[upper + right]} * sx) >> 16U;
    const std::uint64_t below = (std::uint64_t{scratch.lattice_rows[lower + left]} * (fixed_one - sx) +
                                 std::uint64_t{scratch.lattice_rows[lower + right]} * sx) >> 16U;
    const std::uint64_t value = (above * (fixed_one - sy) + below * sy) >> 16U;
    return value;
}

// The octaves' weighted sum at pixel (x, y), in [0, 65535], after
// prepare_row for row y.
std::uint32_t noise_sum(const OctaveSet& set, const ProceduralScratch& scratch, std::uint32_t width,
                        std::uint32_t height, std::uint32_t x, std::uint32_t y) noexcept {
    std::uint64_t sum = 0U;
    for (std::size_t index = 0U; index < set.count; ++index) {
        const std::uint64_t value = octave_value(set, index, scratch, width, height, x, y);
        sum = sum + value * set.octaves[index].weight;
    }
    const std::uint32_t mean = static_cast<std::uint32_t>(sum / set.total_weight);
    return mean;
}

// A colour's straight channels as integers in [0, 255].
struct Channels final {
    std::int32_t red{};
    std::int32_t green{};
    std::int32_t blue{};
    std::int32_t alpha{};
};

Channels channels_of(Color colour) noexcept {
    const Channels channels{colour.red, colour.green, colour.blue, colour.alpha};
    return channels;
}

// ground + (figure − ground) × weight / 65536, rounded, within [0, 255];
// `weight` is in [−65536, 65536], so a negative weight moves away from the
// figure. Right shifts of negative values are arithmetic (C++20).
std::int32_t mix_channel(std::int32_t ground, std::int32_t figure, std::int32_t weight) noexcept {
    const std::int64_t delta = static_cast<std::int64_t>(figure - ground) * weight;
    const std::int64_t mixed = ground + ((delta + 32768) >> 16);
    const std::int64_t bounded = std::clamp<std::int64_t>(mixed, 0, 255);
    return static_cast<std::int32_t>(bounded);
}

Channels mix_channels(const Channels& ground, const Channels& figure, std::int32_t weight) noexcept {
    const Channels mixed{mix_channel(ground.red, figure.red, weight), mix_channel(ground.green, figure.green, weight),
                         mix_channel(ground.blue, figure.blue, weight),
                         mix_channel(ground.alpha, figure.alpha, weight)};
    return mixed;
}

// Writes straight `colour` premultiplied as BGRA8, each channel rounded once.
void store_channels(std::uint8_t* pixel, const Channels& colour) noexcept {
    const std::int32_t alpha = colour.alpha;
    pixel[0] = static_cast<std::uint8_t>((colour.blue * alpha + 127) / 255);
    pixel[1] = static_cast<std::uint8_t>((colour.green * alpha + 127) / 255);
    pixel[2] = static_cast<std::uint8_t>((colour.red * alpha + 127) / 255);
    pixel[3] = static_cast<std::uint8_t>(alpha);
}

// `strength` in [0, 1] as a weight in [0, 65536].
std::int32_t strength_weight(double strength) noexcept {
    const double scaled = std::clamp(strength, 0.0, 1.0) * static_cast<double>(fixed_one) + 0.5;
    const std::int32_t weight = static_cast<std::int32_t>(scaled);
    return weight;
}

// `weight` (in [−65536, 65536]) scaled by `strength` (in [0, 65536]).
std::int32_t scaled_weight(std::int64_t weight, std::int32_t strength) noexcept {
    const std::int64_t scaled = (weight * strength) >> 16;
    return static_cast<std::int32_t>(scaled);
}

// A sum in [0, 65535] about its middle: [−65535, 65535].
std::int64_t centred(std::uint32_t sum) noexcept {
    const std::int64_t signed_sum = 2 * static_cast<std::int64_t>(sum) - 65535;
    return signed_sum;
}

std::uint8_t* pixel_at(const PartRasterView& destination, std::uint32_t x, std::uint32_t y) noexcept {
    const std::size_t offset =
        static_cast<std::size_t>(y) * destination.row_bytes + static_cast<std::size_t>(x) * part_raster_bytes_per_pixel;
    std::uint8_t* const pixel = destination.pixels.data() + offset;
    return pixel;
}

// -- the patterns ------------------------------------------------------------------

// A pattern's fixed inputs, read once before its pixel loop.
struct PatternInputs final {
    Channels ground{};
    Channels figure{};
    std::int32_t strength{};
    std::uint32_t width{};
    std::uint32_t height{};
};

PatternInputs inputs_of(const ProceduralFill& fill, const PartRasterView& destination) noexcept {
    PatternInputs inputs{};
    inputs.ground = channels_of(fill.ground);
    inputs.figure = channels_of(fill.figure);
    inputs.strength = strength_weight(fill.strength);
    inputs.width = destination.extent.width;
    inputs.height = destination.extent.height;
    return inputs;
}

// A pattern whose weight is its octaves' sum about the middle, so that its
// mean is the ground: felt and brushed metal.
void generate_centred_noise(OctaveSet& set, const PatternInputs& inputs, const PartRasterView& destination,
                            ProceduralScratch& scratch) {
    prepare_axes(set, inputs.width, inputs.height, scratch);
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        prepare_row(set, scratch, inputs.height, y);
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            const std::uint32_t sum = noise_sum(set, scratch, inputs.width, inputs.height, x, y);
            const std::int32_t weight = scaled_weight(centred(sum), inputs.strength);
            store_channels(pixel_at(destination, x, y), mix_channels(inputs.ground, inputs.figure, weight));
        }
    }
}

// Felt: cells of 8, 4, 2 and 1 logical units, weighted 6, 4, 3 and 3.
void generate_felt(const ProceduralFill& fill, const PatternInputs& inputs, const PartRasterView& destination,
                   ProceduralScratch& scratch) {
    OctaveSet set{};
    add_octave(set, make_octave(fill, 0U, 8.0, 8.0, 6U));
    add_octave(set, make_octave(fill, 1U, 4.0, 4.0, 4U));
    add_octave(set, make_octave(fill, 2U, 2.0, 2.0, 3U));
    add_octave(set, make_octave(fill, 3U, 1.0, 1.0, 3U));
    generate_centred_noise(set, inputs, destination, scratch);
}

// Brushed metal: streaks 32 units long, grain 8 units long, both half a
// unit high, and a fine 2 × 1 grain; weighted 8, 5 and 3.
void generate_brushed_metal(const ProceduralFill& fill, const PatternInputs& inputs,
                            const PartRasterView& destination, ProceduralScratch& scratch) {
    OctaveSet set{};
    add_octave(set, make_octave(fill, 0U, 32.0, 0.5, 8U));
    add_octave(set, make_octave(fill, 1U, 8.0, 0.5, 5U));
    add_octave(set, make_octave(fill, 2U, 2.0, 1.0, 3U));
    generate_centred_noise(set, inputs, destination, scratch);
}

// Wood grain: rings 4 logical units apart across the tile's height, each a
// phase p in [0, 1) drawn as p⁴ (earlywood to latewood), displaced by up to
// one and a half rings by a warp of 32 × 16-unit cells; three parts ring to
// one part fibre streak (16 × 0.5-unit cells).
void generate_wood_grain(const ProceduralFill& fill, const PatternInputs& inputs, const PartRasterView& destination,
                         ProceduralScratch& scratch) {
    // The warp is the first octave and the streak the second; each is read
    // alone.
    OctaveSet set{};
    add_octave(set, make_octave(fill, 0U, 32.0, 16.0, 1U));
    add_octave(set, make_octave(fill, 1U, 16.0, 0.5, 1U));
    prepare_axes(set, inputs.width, inputs.height, scratch);
    const std::uint64_t rings = cells_across(fill.tile, 4.0);
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const std::uint64_t along = centre_position(y, inputs.height, 1U);
        prepare_row(set, scratch, inputs.height, y);
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            const std::uint64_t displaced = octave_value(set, 0U, scratch, inputs.width, inputs.height, x, y);
            const std::uint64_t phase = (along * rings + displaced * 3U / 2U) & fraction_mask;
            const std::uint64_t squared = (phase * phase) >> 16U;
            const std::uint64_t ring = (squared * squared) >> 16U;
            const std::uint64_t streak = octave_value(set, 1U, scratch, inputs.width, inputs.height, x, y);
            const std::int64_t grain = static_cast<std::int64_t>((ring * 3U + streak) / 4U);
            const std::int32_t weight = scaled_weight(grain, inputs.strength);
            store_channels(pixel_at(destination, x, y), mix_channels(inputs.ground, inputs.figure, weight));
        }
    }
}

// The 16 fibre directions over a half turn, k × π / 16, as Q14 unit
// vectors (cos, sin): fibres have no head, so a half turn covers them.
constexpr std::array<std::array<std::int32_t, 2>, 16> fibre_directions{{
    {16384, 0}, {16069, 3196}, {15137, 6270}, {13623, 9102}, {11585, 11585}, {9102, 13623},
    {6270, 15137}, {3196, 16069}, {0, 16384}, {-3196, 16069}, {-6270, 15137}, {-9102, 13623},
    {-11585, 11585}, {-13623, 9102}, {-15137, 6270}, {-16069, 3196},
}};

// The signed distance from `from` to `to` along a wrapping axis of 2^16,
// in [−32768, 32768).
std::int32_t wrapped_delta(std::uint32_t to, std::uint32_t from) noexcept {
    const std::uint32_t shifted = (to - from + 32768U) & fraction_mask;
    const std::int32_t delta = static_cast<std::int32_t>(shifted) - 32768;
    return delta;
}

// Paper fibre: formation noise (cells of 16, 4 and 1 logical units,
// weighted 2, 1 and 1) at half amplitude about the ground, and one fibre
// in each 8-unit cell: a segment 6 units long through a seeded point in a
// seeded direction, max(0.45 units, 0.75 device pixels) in half width,
// its weight falling linearly across its width; the strongest fibre over
// a pixel is drawn. Positions are fractions of the tile in 2^16, so the
// tile wraps.
void generate_paper_fibre(const ProceduralFill& fill, const PatternInputs& inputs, double scale,
                          const PartRasterView& destination, ProceduralScratch& scratch) {
    OctaveSet set{};
    add_octave(set, make_octave(fill, 0U, 16.0, 16.0, 2U));
    add_octave(set, make_octave(fill, 1U, 4.0, 4.0, 1U));
    add_octave(set, make_octave(fill, 2U, 1.0, 1.0, 1U));
    prepare_axes(set, inputs.width, inputs.height, scratch);
    const std::uint32_t grid = cells_across(fill.tile, 8.0);
    const std::uint32_t fibre_key = octave_key(fill.seed, 7U);
    // Each cell's fibre, row-major: its centre across and down (tile
    // fractions in 2^16), its direction (an index into fibre_directions)
    // and its tone in [1/2, 1] (in 2^16).
    scratch.fibres.resize(std::size_t{grid} * grid * 4U);
    for (std::uint32_t cell_y = 0U; cell_y < grid; ++cell_y) {
        for (std::uint32_t cell_x = 0U; cell_x < grid; ++cell_x) {
            const std::uint32_t placed = mix(fibre_key ^ mix(cell_x ^ mix(cell_y + golden)));
            const std::uint32_t turned = mix(placed);
            const std::size_t at = (std::size_t{cell_y} * grid + cell_x) * 4U;
            const std::uint64_t offset_x = placed & fraction_mask;
            const std::uint64_t offset_y = placed >> 16U;
            scratch.fibres[at] = static_cast<std::uint32_t>((std::uint64_t{cell_x} * fixed_one + offset_x) / grid);
            scratch.fibres[at + 1U] = static_cast<std::uint32_t>((std::uint64_t{cell_y} * fixed_one + offset_y) / grid);
            scratch.fibres[at + 2U] = turned & 15U;
            scratch.fibres[at + 3U] = 32768U + ((turned >> 17U) & 0x7FFFU);
        }
    }
    const double unit = static_cast<double>(fixed_one) / fill.tile;
    const std::int64_t half_length = static_cast<std::int64_t>(3.0 * unit + 0.5);
    const double device_unit = static_cast<double>(fixed_one) / (fill.tile * scale);
    const std::int64_t half_width =
        std::max<std::int64_t>(static_cast<std::int64_t>(std::max(0.45 * unit, 0.75 * device_unit) + 0.5), 1);
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const std::uint32_t py = static_cast<std::uint32_t>(centre_position(y, inputs.height, 1U));
        const std::uint32_t cell_y = static_cast<std::uint32_t>((std::uint64_t{py} * grid) >> 16U);
        prepare_row(set, scratch, inputs.height, y);
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            const std::uint32_t px = static_cast<std::uint32_t>(centre_position(x, inputs.width, 1U));
            const std::uint32_t cell_x = static_cast<std::uint32_t>((std::uint64_t{px} * grid) >> 16U);
            std::int64_t fibre = 0;
            for (std::uint32_t step_y = 0U; step_y < 3U; ++step_y) {
                const std::uint32_t near_y = (cell_y + grid + step_y - 1U) % grid;
                for (std::uint32_t step_x = 0U; step_x < 3U; ++step_x) {
                    const std::uint32_t near_x = (cell_x + grid + step_x - 1U) % grid;
                    const std::size_t at = (std::size_t{near_y} * grid + near_x) * 4U;
                    const std::array<std::int32_t, 2>& direction = fibre_directions[scratch.fibres[at + 2U]];
                    const std::int64_t dx = wrapped_delta(px, scratch.fibres[at]);
                    const std::int64_t dy = wrapped_delta(py, scratch.fibres[at + 1U]);
                    const std::int64_t along = (dx * direction[0] + dy * direction[1]) >> 14;
                    const std::int64_t across = (dy * direction[0] - dx * direction[1]) >> 14;
                    const std::int64_t distance = across < 0 ? -across : across;
                    const std::int64_t reach = along < 0 ? -along : along;
                    if (reach >= half_length || distance >= half_width) continue;
                    const std::int64_t tone = scratch.fibres[at + 3U];
                    const std::int64_t weight = ((half_width - distance) * 65536 / half_width) * tone >> 16;
                    fibre = std::max(fibre, weight);
                }
            }
            const std::uint32_t sum = noise_sum(set, scratch, inputs.width, inputs.height, x, y);
            const std::int64_t formation = centred(sum) / 2;
            const std::int64_t combined = std::clamp<std::int64_t>(formation + fibre, -65536, 65536);
            const std::int32_t weight = scaled_weight(combined, inputs.strength);
            store_channels(pixel_at(destination, x, y), mix_channels(inputs.ground, inputs.figure, weight));
        }
    }
}

// Pinstripe: the tile's first keyline_width(scale) rows are the figure, at
// most half the tile; the rest is the ground.
void generate_pinstripe(const PatternInputs& inputs, double scale, const PartRasterView& destination) {
    const std::uint32_t line = static_cast<std::uint32_t>(keyline_width(scale));
    const std::uint32_t rows = std::min(line, inputs.height / 2U);
    const Channels stripe = mix_channels(inputs.ground, inputs.figure, inputs.strength);
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const Channels& colour = y < rows ? stripe : inputs.ground;
        for (std::uint32_t x = 0U; x < inputs.width; ++x) store_channels(pixel_at(destination, x, y), colour);
    }
}

// Checker: the tile split at half its width and half its height, the
// top-left and bottom-right quarters the ground and the other two the
// figure. An odd side gives the first half the smaller share.
void generate_checker(const PatternInputs& inputs, const PartRasterView& destination) {
    const Channels square = mix_channels(inputs.ground, inputs.figure, inputs.strength);
    const std::uint32_t half_across = inputs.width / 2U;
    const std::uint32_t half_down = inputs.height / 2U;
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const bool lower = y >= half_down;
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            const bool right = x >= half_across;
            const Channels& colour = lower == right ? inputs.ground : square;
            store_channels(pixel_at(destination, x, y), colour);
        }
    }
}

// Hatch: square (column, row) of the 8 by 8 cell holds the pixels whose
// 8x / width and 8y / height floor to it; a marked square is the figure.
void generate_hatch(const ProceduralFill& fill, const PatternInputs& inputs, const PartRasterView& destination) {
    const Channels line = mix_channels(inputs.ground, inputs.figure, inputs.strength);
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const std::uint32_t row = static_cast<std::uint32_t>((std::uint64_t{y} * hatch_cell_squares) / inputs.height);
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            const std::uint32_t column =
                static_cast<std::uint32_t>((std::uint64_t{x} * hatch_cell_squares) / inputs.width);
            const bool marked = hatch_square_marked(fill.hatch_style, column, row);
            const Channels& colour = marked ? line : inputs.ground;
            store_channels(pixel_at(destination, x, y), colour);
        }
    }
}

// Carbon fibre: a 2/2 twill of tows 4 logical units wide. The tile holds
// `repeats` whole 16-unit repeats, so `tows` = 4 × repeats tows across it
// and down it, and every term below wraps with the tile. At crossing
// (tow column i, tow row j), p = (i + j) mod 4 says which tow floats over:
// p of 0 or 1, the tow down the tile (column i), in the first or second
// tow of its float; p of 2 or 3, the tow across it (row j), in the first
// or second tow of its float. So each tow floats over two and under two,
// and the floats step one tow a row: the twill's diagonals.
//
// A pixel's weight about the ground, in 2^16: its tow's light (the tow
// across catches carbon_across_level, the tow down carbon_down_level)
// times its rounded profile across the tow and along the float, plus its
// tow's fibre streaks and its crossing's tone. Second pass (owner,
// 2026-11-22: finer strands, more texture within each tow, sparse
// glints): each tow holds carbon_fibres_per_tow fine fibres and
// carbon_bundles_per_tow bundles of them, each with its own seeded streak,
// and about one crossing in ten (carbon_glint_threshold) where the tow
// across floats over lights one of its inner fibres as a glint, brightest
// at the middle of the float. The weight lies within [−0.13, 0.99] of the
// full contrast, so it never leaves [−65536, 65536].
constexpr double carbon_repeat_length = 16.0;
constexpr std::uint32_t carbon_tows_per_repeat = 4U;
constexpr std::uint32_t carbon_fibres_per_tow = 16U;
constexpr std::uint32_t carbon_bundles_per_tow = 3U;
constexpr std::int64_t carbon_across_level = 41943;      // 0.64
constexpr std::int64_t carbon_down_level = 18350;        // 0.28
constexpr std::int64_t carbon_edge_floor = 16384;        // 0.25 of the light at a tow's edge
constexpr std::int64_t carbon_end_floor = 26214;         // 0.40 of it where a float dives under
constexpr std::int64_t carbon_streak_amplitude = 3277;   // ± 0.05, each fine fibre
constexpr std::int64_t carbon_bundle_amplitude = 2621;   // ± 0.04, each bundle of fibres
constexpr std::int64_t carbon_crossing_amplitude = 2621; // ± 0.04
constexpr std::int64_t carbon_glint_level = 14418;       // 0.22 at the float's middle
// A crossing glints when its hash (16 bits) falls below 65536 / 10; the
// glinting fibre is one of the tow's inner half (never its edge fibres, so
// a tow stays rounded: darker at its edges than at its middle).
constexpr std::uint32_t carbon_glint_threshold = 6554U;
constexpr std::uint32_t carbon_glint_first_fibre = carbon_fibres_per_tow / 4U;
constexpr std::uint32_t carbon_glint_fibre_span = carbon_fibres_per_tow / 2U;
// No glint at a crossing.
constexpr std::int32_t carbon_no_glint = -1;

// A tow's rounded profile across its width at `fraction` (in [0, 65535]):
// the edge floor at its edges, rising as 1 − t² to 1 at its middle, where
// t runs from −1 to 1 across the tow. In [carbon_edge_floor, 65536].
std::int32_t carbon_across_profile(std::uint32_t fraction) noexcept {
    const std::int64_t t = 2 * static_cast<std::int64_t>(fraction) + 1 - 65536;
    const std::int64_t bulge = 65536 - ((t * t) >> 16);
    const std::int64_t profile = carbon_edge_floor + (((65536 - carbon_edge_floor) * bulge) >> 16);
    return static_cast<std::int32_t>(profile);
}

// A float's profile along its two tows at `position` (in [0, 131072), the
// first tow then the second): 1 at its middle, falling as d⁴ to the end
// floor where it dives under, d running from −1 to 1 along the float. In
// [carbon_end_floor, 65536].
std::int32_t carbon_along_profile(std::uint32_t position) noexcept {
    const std::int64_t d = static_cast<std::int64_t>(position) - 65536;
    const std::int64_t squared = (d * d) >> 16;
    const std::int64_t fourth = (squared * squared) >> 16;
    const std::int64_t profile = 65536 - (((65536 - carbon_end_floor) * fourth) >> 16);
    return static_cast<std::int32_t>(profile);
}

// Which of carbon_fibres_per_tow fibres lies at `fraction` across a tow.
std::uint32_t carbon_fibre_index(std::uint32_t fraction) noexcept {
    const std::uint32_t fibre = (fraction * carbon_fibres_per_tow) >> 16U;
    return fibre;
}

// The fibre streaks at `fraction` across tow `tow` under `key`: its fine
// fibre's, within ± carbon_streak_amplitude, plus its bundle's, within
// ± carbon_bundle_amplitude.
std::int32_t carbon_streak(std::uint32_t key, std::uint32_t tow, std::uint32_t fraction) noexcept {
    const std::uint32_t fibre = carbon_fibre_index(fraction);
    const std::uint32_t bundle = (fraction * carbon_bundles_per_tow) >> 16U;
    const std::uint32_t fine = mix(key ^ mix(tow * carbon_fibres_per_tow + fibre + golden)) >> 16U;
    const std::uint32_t coarse = mix((key + golden) ^ mix(tow * carbon_bundles_per_tow + bundle + golden)) >> 16U;
    const std::int64_t fine_streak = ((static_cast<std::int64_t>(fine) - 32768) * carbon_streak_amplitude) >> 15;
    const std::int64_t bundle_streak = ((static_cast<std::int64_t>(coarse) - 32768) * carbon_bundle_amplitude) >> 15;
    return static_cast<std::int32_t>(fine_streak + bundle_streak);
}

// Where the carbon fibre's terms lie in the scratch's weave_terms, for a
// `width` × `height` tile of `tows` tows each way. Per pixel column: the
// profile across a tow down the tile, the profiles along a float of a tow
// across it (its first tow, then `width` more for its second) and the
// streak of a tow down it; per pixel row, the same for the other
// direction, and the fibre index of a tow across it; then each tow
// column's first pixel column (tows + 1 entries, the last the width), each
// crossing's tone, row-major by tow row, and each crossing's glinting
// fibre (carbon_no_glint for none), in the same order.
struct WeaveLayout final {
    std::size_t column_across{};
    std::size_t column_along{};
    std::size_t column_streak{};
    std::size_t row_across{};
    std::size_t row_along{};
    std::size_t row_streak{};
    std::size_t row_fibre{};
    std::size_t column_runs{};
    std::size_t crossings{};
    std::size_t glints{};
    std::size_t total{};
};

WeaveLayout weave_layout(std::uint32_t width, std::uint32_t height, std::uint32_t tows) noexcept {
    WeaveLayout layout{};
    layout.column_across = 0U;
    layout.column_along = layout.column_across + width;
    layout.column_streak = layout.column_along + 2U * std::size_t{width};
    layout.row_across = layout.column_streak + width;
    layout.row_along = layout.row_across + height;
    layout.row_streak = layout.row_along + 2U * std::size_t{height};
    layout.row_fibre = layout.row_streak + height;
    layout.column_runs = layout.row_fibre + height;
    layout.crossings = layout.column_runs + tows + 1U;
    layout.glints = layout.crossings + std::size_t{tows} * tows;
    layout.total = layout.glints + std::size_t{tows} * tows;
    return layout;
}

// Fills one axis's terms for `length` pixels holding `tows` tows: at
// `across`, each pixel's profile across its tow; at `along`, its profile
// along a float in the float's first tow and, `length` entries on, in its
// second; at `streak`, its tow's streak under `key`.
void fill_weave_axis(std::uint32_t length, std::uint32_t tows, std::uint32_t key, std::size_t across,
                     std::size_t along, std::size_t streak, std::vector<std::int32_t>& terms) noexcept {
    for (std::uint32_t index = 0U; index < length; ++index) {
        const std::uint64_t position = centre_position(index, length, tows);
        const std::uint32_t tow = static_cast<std::uint32_t>(position >> 16U);
        const std::uint32_t fraction = static_cast<std::uint32_t>(position & fraction_mask);
        terms[across + index] = carbon_across_profile(fraction);
        terms[along + index] = carbon_along_profile(fraction);
        terms[along + length + index] = carbon_along_profile(fixed_one + fraction);
        terms[streak + index] = carbon_streak(key, tow, fraction);
    }
}

void generate_carbon_fibre(const ProceduralFill& fill, const PatternInputs& inputs, const PartRasterView& destination,
                           ProceduralScratch& scratch) {
    const std::uint32_t tows = cells_across(fill.tile, carbon_repeat_length) * carbon_tows_per_repeat;
    const std::uint32_t down_key = octave_key(fill.seed, 0U);
    const std::uint32_t across_key = octave_key(fill.seed, 1U);
    const std::uint32_t crossing_key = octave_key(fill.seed, 2U);
    const WeaveLayout layout = weave_layout(inputs.width, inputs.height, tows);
    std::vector<std::int32_t>& terms = scratch.weave_terms;
    terms.resize(layout.total);
    // The tows down the tile run along the pixel rows' axis: their profile
    // across and their streak are the column's; their profile along the
    // float is the row's. The tows across the tile, the other way round.
    fill_weave_axis(inputs.width, tows, down_key, layout.column_across, layout.column_along, layout.column_streak,
                    terms);
    fill_weave_axis(inputs.height, tows, across_key, layout.row_across, layout.row_along, layout.row_streak, terms);
    // Each tow column's pixel columns, so that the pixel loop walks one tow
    // at a time and decides which tow floats once per crossing. A tile
    // narrower than its tows leaves some tow columns without pixels.
    std::uint32_t next_tow = 0U;
    for (std::uint32_t x = 0U; x < inputs.width; ++x) {
        const std::uint32_t tow = static_cast<std::uint32_t>(centre_position(x, inputs.width, tows) >> 16U);
        for (; next_tow <= tow; ++next_tow) terms[layout.column_runs + next_tow] = static_cast<std::int32_t>(x);
    }
    for (; next_tow <= tows; ++next_tow) terms[layout.column_runs + next_tow] = static_cast<std::int32_t>(inputs.width);
    for (std::uint32_t row = 0U; row < tows; ++row) {
        for (std::uint32_t column = 0U; column < tows; ++column) {
            const std::uint32_t hashed = mix(crossing_key ^ mix(column ^ mix(row + golden))) >> 16U;
            const std::int64_t tone = ((static_cast<std::int64_t>(hashed) - 32768) * carbon_crossing_amplitude) >> 15;
            terms[layout.crossings + std::size_t{row} * tows + column] = static_cast<std::int32_t>(tone);
            // Only where the tow across floats over: the tow down stays near
            // the ground, as the sheen is anisotropic.
            const std::uint32_t chance = mix((crossing_key + golden) ^ mix(row ^ mix(column + golden))) >> 16U;
            const bool across_over = ((column + row) & 3U) >= 2U;
            std::int32_t glint = carbon_no_glint;
            if (across_over && chance < carbon_glint_threshold) {
                glint = static_cast<std::int32_t>(carbon_glint_first_fibre + chance % carbon_glint_fibre_span);
            }
            terms[layout.glints + std::size_t{row} * tows + column] = glint;
        }
    }
    // Each pixel row's fibre in its tow across, for the glints.
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const std::uint32_t fraction = static_cast<std::uint32_t>(centre_position(y, inputs.height, tows) & fraction_mask);
        terms[layout.row_fibre + y] = static_cast<std::int32_t>(carbon_fibre_index(fraction));
    }
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const std::uint32_t row_tow = static_cast<std::uint32_t>(centre_position(y, inputs.height, tows) >> 16U);
        const std::int64_t lit_row = (carbon_across_level * terms[layout.row_across + y]) >> 16;
        const std::int64_t row_streak = terms[layout.row_streak + y];
        for (std::uint32_t column_tow = 0U; column_tow < tows; ++column_tow) {
            const std::uint32_t phase = (column_tow + row_tow) & 3U;
            const std::uint32_t start = static_cast<std::uint32_t>(terms[layout.column_runs + column_tow]);
            const std::uint32_t end = static_cast<std::uint32_t>(terms[layout.column_runs + column_tow + 1U]);
            const std::int64_t crossing = terms[layout.crossings + std::size_t{row_tow} * tows + column_tow];
            if (phase < 2U) {
                // The tow down the tile floats over, in tow `phase` of its float.
                const std::int64_t along = terms[layout.row_along + std::size_t{phase} * inputs.height + y];
                for (std::uint32_t x = start; x < end; ++x) {
                    const std::int64_t shade = (terms[layout.column_across + x] * along) >> 16;
                    const std::int64_t weight =
                        ((carbon_down_level * shade) >> 16) + terms[layout.column_streak + x] + crossing;
                    const std::int32_t scaled = scaled_weight(weight, inputs.strength);
                    store_channels(pixel_at(destination, x, y), mix_channels(inputs.ground, inputs.figure, scaled));
                }
                continue;
            }
            // The tow across the tile floats over, in tow `phase` − 2 of its float.
            // Its glint, if this row is the crossing's glinting fibre, is
            // decided once per row and crossing: a level the float's profile
            // along it shapes.
            const std::size_t along_column = layout.column_along + std::size_t{phase - 2U} * inputs.width;
            const std::int32_t glinting = terms[layout.glints + std::size_t{row_tow} * tows + column_tow];
            const std::int64_t glint = glinting == terms[layout.row_fibre + y] ? carbon_glint_level : 0;
            for (std::uint32_t x = start; x < end; ++x) {
                const std::int64_t shade = (lit_row * terms[along_column + x]) >> 16;
                const std::int64_t sparkle = (glint * terms[along_column + x]) >> 16;
                const std::int64_t weight = shade + row_streak + crossing + sparkle;
                const std::int32_t scaled = scaled_weight(weight, inputs.strength);
                store_channels(pixel_at(destination, x, y), mix_channels(inputs.ground, inputs.figure, scaled));
            }
        }
    }
}

// One fresco bloom: its centre and radii in doubled device pixels (pixel
// centres are odd there).
struct Bloom final {
    std::int64_t centre_x{};
    std::int64_t centre_y{};
    std::int64_t radius_x{};
    std::int64_t radius_y{};
};

constexpr std::size_t fresco_blooms = 3U;

// A bloom's offset (2 index + 1 − centre)² over radius², in 16.16 and at
// most 65536 (outside it).
std::uint32_t bloom_term(std::uint32_t index, std::int64_t centre, std::int64_t radius) noexcept {
    const std::int64_t offset = 2 * static_cast<std::int64_t>(index) + 1 - centre;
    const std::int64_t term = (offset * offset * 65536) / (radius * radius);
    const std::int64_t bounded = std::min<std::int64_t>(term, 65536);
    return static_cast<std::uint32_t>(bounded);
}

// A fraction of `length` doubled pixels: `low` + `value` × `span`, all in
// 2^16.
std::int64_t placed_fraction(std::uint32_t length, std::uint32_t low, std::uint32_t span,
                             std::uint32_t value) noexcept {
    const std::uint64_t fraction = low + ((std::uint64_t{value} * span) >> 16U);
    const std::int64_t placed = static_cast<std::int64_t>((2U * std::uint64_t{length} * fraction) >> 16U);
    return placed;
}

// Fresco: a vertical gradient through the ground (top), the figure
// (middle) and the accent (bottom), and three blooms of `bloom` over it,
// each centred within [0.15, 0.85] of the width and [0.2, 0.8] of the
// height, with radii [0.25, 0.45] of the width and [0.5, 0.9] of the
// height; a bloom's weight is (1 − q)² where q is the pixel's squared
// elliptical distance, times the bloom's alpha and the strength.
void generate_fresco(const ProceduralFill& fill, const PatternInputs& inputs, const PartRasterView& destination,
                     ProceduralScratch& scratch) {
    std::array<Bloom, fresco_blooms> blooms{};
    for (std::uint32_t index = 0U; index < fresco_blooms; ++index) {
        const std::uint32_t placed = mix(fill.seed + index * golden);
        const std::uint32_t sized = mix(placed);
        Bloom& bloom = blooms[index];
        bloom.centre_x = placed_fraction(inputs.width, 9830U, 45875U, placed & fraction_mask);
        bloom.centre_y = placed_fraction(inputs.height, 13107U, 39322U, placed >> 16U);
        bloom.radius_x = std::max<std::int64_t>(placed_fraction(inputs.width, 16384U, 13107U, sized & fraction_mask), 1);
        bloom.radius_y = std::max<std::int64_t>(placed_fraction(inputs.height, 32768U, 26214U, sized >> 16U), 1);
    }
    scratch.across_weights.resize(fresco_blooms * inputs.width);
    scratch.down_weights.resize(fresco_blooms * inputs.height);
    for (std::size_t index = 0U; index < fresco_blooms; ++index) {
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            scratch.across_weights[index * inputs.width + x] =
                bloom_term(x, blooms[index].centre_x, blooms[index].radius_x);
        }
        for (std::uint32_t y = 0U; y < inputs.height; ++y) {
            scratch.down_weights[index * inputs.height + y] =
                bloom_term(y, blooms[index].centre_y, blooms[index].radius_y);
        }
    }
    const Channels accent = channels_of(fill.accent);
    const Channels bloom_colour = channels_of(fill.bloom);
    // The blooms' peak weight: their alpha times the strength.
    const std::int64_t peak = static_cast<std::int64_t>(fill.bloom.alpha) * inputs.strength / 255;
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        const std::uint64_t along = centre_position(y, inputs.height, 1U);
        Channels row{};
        if (along < 32768U) {
            row = mix_channels(inputs.ground, inputs.figure, static_cast<std::int32_t>(along * 2U));
        } else {
            row = mix_channels(inputs.figure, accent, static_cast<std::int32_t>((along - 32768U) * 2U));
        }
        for (std::uint32_t x = 0U; x < inputs.width; ++x) {
            Channels colour = row;
            for (std::size_t index = 0U; index < fresco_blooms; ++index) {
                const std::uint32_t q =
                    scratch.across_weights[index * inputs.width + x] + scratch.down_weights[index * inputs.height + y];
                if (q >= fixed_one) continue;
                const std::int64_t inside = fixed_one - q;
                const std::int64_t falloff = (inside * inside) >> 16;
                const std::int32_t weight = static_cast<std::int32_t>((falloff * peak) >> 16);
                colour = mix_channels(colour, bloom_colour, weight);
            }
            store_channels(pixel_at(destination, x, y), colour);
        }
    }
}

// Every pixel of `destination` in the ground colour.
void fill_ground(const PatternInputs& inputs, const PartRasterView& destination) noexcept {
    for (std::uint32_t y = 0U; y < inputs.height; ++y) {
        for (std::uint32_t x = 0U; x < inputs.width; ++x) store_channels(pixel_at(destination, x, y), inputs.ground);
    }
}

std::uint64_t generation_weight(ProceduralPattern pattern) noexcept {
    const ProceduralEffortWeights& weights = procedural_effort_weights;
    switch (pattern) {
    case ProceduralPattern::felt: return weights.felt;
    case ProceduralPattern::wood_grain: return weights.wood_grain;
    case ProceduralPattern::paper_fibre: return weights.paper_fibre;
    case ProceduralPattern::pinstripe: return weights.pinstripe;
    case ProceduralPattern::brushed_metal: return weights.brushed_metal;
    case ProceduralPattern::fresco: return weights.fresco;
    case ProceduralPattern::checker: return weights.checker;
    case ProceduralPattern::hatch: return weights.hatch;
    case ProceduralPattern::carbon_fibre: return weights.carbon_fibre;
    }
    // A pattern outside the enumeration is never accepted
    // (valid_surface_material): one pixel of it is past the budget.
    const std::uint64_t unbounded = maximum_material_bake_effort.units + 1U;
    return unbounded;
}

// A length in device pixels rounded, within [1, maximum_part_raster_side].
std::uint32_t device_length(double length) noexcept {
    const double bounded = std::clamp(length + 0.5, 1.0, static_cast<double>(maximum_part_raster_side));
    const std::uint32_t pixels = static_cast<std::uint32_t>(bounded);
    return pixels;
}

} // namespace

DeviceExtent procedural_extent(const ProceduralFill& fill, Rect box, double scale) noexcept {
    if (fill.pattern == ProceduralPattern::fresco) {
        const DeviceExtent covered{device_length(box.width), device_length(box.height)};
        return covered;
    }
    const std::uint32_t side = device_length(fill.tile * scale);
    const DeviceExtent tile{side, side};
    return tile;
}

BakeEffort procedural_generation_effort(const ProceduralFill& fill, DeviceExtent extent) noexcept {
    // At most maximum_part_raster_side squared pixels (procedural_extent)
    // times a weight of at most the budget: within 64 bits.
    const std::uint64_t pixels = std::uint64_t{extent.width} * extent.height;
    const BakeEffort effort{pixels * generation_weight(fill.pattern)};
    return effort;
}

bool procedural_pattern_bounded(const ProceduralFill& fill, DeviceExtent extent) noexcept {
    const std::uint64_t bytes = std::uint64_t{extent.width} * extent.height * part_raster_bytes_per_pixel;
    if (bytes > maximum_procedural_pattern_bytes) return false;
    const bool within = procedural_generation_effort(fill, extent) <= maximum_material_bake_effort;
    return within;
}

void generate_procedural(const ProceduralFill& fill, double scale, const PartRasterView& destination,
                         ProceduralScratch& scratch) {
    const PatternInputs inputs = inputs_of(fill, destination);
    if (inputs.width == 0U || inputs.height == 0U) return;
    switch (fill.pattern) {
    case ProceduralPattern::felt:
        generate_felt(fill, inputs, destination, scratch);
        return;
    case ProceduralPattern::wood_grain:
        generate_wood_grain(fill, inputs, destination, scratch);
        return;
    case ProceduralPattern::paper_fibre:
        generate_paper_fibre(fill, inputs, scale, destination, scratch);
        return;
    case ProceduralPattern::pinstripe:
        generate_pinstripe(inputs, scale, destination);
        return;
    case ProceduralPattern::brushed_metal:
        generate_brushed_metal(fill, inputs, destination, scratch);
        return;
    case ProceduralPattern::fresco:
        generate_fresco(fill, inputs, destination, scratch);
        return;
    case ProceduralPattern::checker:
        generate_checker(inputs, destination);
        return;
    case ProceduralPattern::hatch:
        generate_hatch(fill, inputs, destination);
        return;
    case ProceduralPattern::carbon_fibre:
        generate_carbon_fibre(fill, inputs, destination, scratch);
        return;
    }
    // A pattern outside the enumeration is never accepted: the ground.
    fill_ground(inputs, destination);
}

std::shared_ptr<PartRaster> make_procedural_pattern(const ProceduralFill& fill, double scale,
                                                    DeviceExtent extent) noexcept {
    if (extent.width == 0U || extent.height == 0U) return nullptr;
    if (extent.width > maximum_part_raster_side || extent.height > maximum_part_raster_side) return nullptr;
    try {
        std::shared_ptr<PartRaster> made = PartRaster::create(extent, DeviceScale{scale});
        if (!made) return nullptr;
        ProceduralScratch scratch{};
        generate_procedural(fill, scale, (*made).view(), scratch);
        return made;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void map_procedural_tile(double start, std::uint32_t tile_length, std::uint32_t outputs,
                         std::vector<std::uint32_t>& map) {
    map.resize(outputs);
    const std::int64_t origin = static_cast<std::int64_t>(std::floor(start));
    const std::int64_t length = tile_length;
    for (std::uint32_t index = 0U; index < outputs; ++index) {
        const std::int64_t along = static_cast<std::int64_t>(index) - origin;
        const std::int64_t wrapped = ((along % length) + length) % length;
        map[index] = static_cast<std::uint32_t>(wrapped);
    }
}

} // namespace gui_forms::engine_primitives
