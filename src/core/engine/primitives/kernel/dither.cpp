#include "dither.hpp"

#include <algorithm>
#include <array>
#include <cmath>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

#include "blue_noise_ranks.inc"

constexpr std::uint32_t ordered_side = 8U;
constexpr std::uint32_t ordered_bits = 3U;
constexpr std::uint32_t blue_noise_side = 64U;

// The 8 × 8 Bayer matrix by bit interleaving: for each bit b of the cell's
// coordinates, lowest first, the 2 × 2 matrix [[0, 2], [3, 1]] at that bit,
// ((x ^ y) bit b) × 2 + (y bit b), is the next base-4 digit, most
// significant first. This is the recursion M(2n) = 4 M(n) + M(2) of the
// matrix's usual definition.
constexpr std::array<std::uint16_t, 64> ordered_rank_table_of() noexcept {
    std::array<std::uint16_t, 64> ranks{};
    for (std::uint32_t y = 0U; y < ordered_side; ++y) {
        for (std::uint32_t x = 0U; x < ordered_side; ++x) {
            const std::uint32_t crossed = x ^ y;
            std::uint32_t rank = 0U;
            for (std::uint32_t bit = 0U; bit < ordered_bits; ++bit) {
                const std::uint32_t high = (crossed >> bit) & 1U;
                const std::uint32_t low = (y >> bit) & 1U;
                rank = (rank << 2U) | (high << 1U) | low;
            }
            ranks[static_cast<std::size_t>(y) * ordered_side + x] = static_cast<std::uint16_t>(rank);
        }
    }
    return ranks;
}

constexpr std::array<std::uint16_t, 64> ordered_rank_table = ordered_rank_table_of();

static_assert(ordered_rank_table[0] == 0U && ordered_rank_table[1] == 32U && ordered_rank_table[8] == 48U &&
                  ordered_rank_table[9] == 16U && ordered_rank_table[63] == 21U,
              "the ordered table is the 8 x 8 Bayer matrix");
static_assert(blue_noise_rank_table.size() == std::size_t{blue_noise_side} * blue_noise_side,
              "the blue-noise table is one period square");

} // namespace

std::uint32_t dither_period(MaterialDither mode) noexcept {
    switch (mode) {
    case MaterialDither::none: return 1U;
    case MaterialDither::ordered: return ordered_side;
    case MaterialDither::blue_noise: return blue_noise_side;
    default: break; // Not a listed mode: rounding, which has no pattern.
    }
    return 1U;
}

std::span<const std::uint16_t> ordered_dither_ranks() noexcept {
    const std::span<const std::uint16_t> ranks{ordered_rank_table};
    return ranks;
}

std::span<const std::uint16_t> blue_noise_dither_ranks() noexcept {
    const std::span<const std::uint16_t> ranks{blue_noise_rank_table};
    return ranks;
}

DitherPlane dither_plane(MaterialDither mode, std::int64_t anchor_x, std::int64_t anchor_y) noexcept {
    DitherPlane plane{};
    plane.anchor_x = anchor_x;
    plane.anchor_y = anchor_y;
    switch (mode) {
    case MaterialDither::none: return plane;
    case MaterialDither::ordered:
        plane.ranks = ordered_rank_table.data();
        plane.side = ordered_side;
        plane.step = 1.0 / 64.0;
        break;
    case MaterialDither::blue_noise:
        plane.ranks = blue_noise_rank_table.data();
        plane.side = blue_noise_side;
        plane.step = 1.0 / 4096.0;
        break;
    default: return plane; // Not a listed mode: rounding.
    }
    plane.mask = plane.side - 1U;
    return plane;
}

std::uint32_t dither_cell(const DitherPlane& plane, std::int64_t position, std::int64_t anchor) noexcept {
    // Two's complement wraps a negative offset to a value congruent to it
    // modulo 2^64, so masking by a power of two takes the true modulus.
    const std::uint64_t offset = static_cast<std::uint64_t>(position) - static_cast<std::uint64_t>(anchor);
    const std::uint32_t cell = static_cast<std::uint32_t>(offset & plane.mask);
    return cell;
}

double dither_threshold(const DitherPlane& plane, std::uint16_t rank) noexcept {
    const double threshold = (static_cast<double>(rank) + 0.5) * plane.step;
    return threshold;
}

std::uint8_t to_byte_dithered(double value, double threshold) noexcept {
    const double clamped = std::clamp(value, 0.0, 1.0);
    const double scaled = std::floor(clamped * 255.0 + threshold);
    const std::uint8_t byte = static_cast<std::uint8_t>(scaled);
    return byte;
}

void store_dithered(const PartRasterView& destination, std::size_t offset, const Unit& unit,
                    double threshold) noexcept {
    destination.pixels[offset] = to_byte_dithered(unit.blue, threshold);
    destination.pixels[offset + 1U] = to_byte_dithered(unit.green, threshold);
    destination.pixels[offset + 2U] = to_byte_dithered(unit.red, threshold);
    destination.pixels[offset + 3U] = to_byte_dithered(unit.alpha, threshold);
}

} // namespace gui_forms::engine_primitives
