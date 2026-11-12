// Gradient dithering (MaterialDither; src/core/engine/primitives/kernel/
// dither.hpp): the material kernel's final quantization with a threshold
// per device pixel, ordered (8 x 8 Bayer) or blue noise (64 x 64).
//
// - The tables: the ordered one is the Bayer matrix, the blue-noise one a
//   permutation of its 4096 ranks whose every level is spread evenly over
//   8 x 8 blocks; both hashes are fixed (tools/dither_tables.py writes the
//   blue-noise table; `--check` there fails when it is stale).
// - The threshold keeps exact bytes exact, and a threshold of one half is
//   plain rounding.
// - Banding: a 1024-pixel gradient across four byte steps (#808080 to
//   #848484) shows 4 steps and runs of 256 equal bytes undithered; dithered,
//   hundreds of steps per row, runs a fraction as long, and 8 x 8 block means
//   within a sixteenth of a byte of the exact ramp where rounding misses by
//   nearly half a byte.
// - Determinism across hosts: the three bakes' FNV-1a hashes are constants
//   computed outside the kernel (an emulation of its arithmetic in Python's
//   IEEE doubles, recorded 2026-11-12), so every host must give these bytes.
//   GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the kernel's hashes instead.
// - Damage invariance at scales 1, 2 and 3: a rounded, bordered, shadowed
//   panel baked in uneven tiles holds the whole bake's bytes, and the bake
//   with repeated spans (whole periods copied) equals the bake of every
//   pixel.
// - Slices keep the pattern: a dithered raster assembled from a shorter
//   reference (SliceAxis::period) equals the bake of every pixel, across,
//   down and both, at odd lengths.
// - No swimming: a tall panel scrolled under a viewport shows, at every
//   offset, the bytes of its content at that offset.
// - The bake budget: a dither gives way before the material does. A tall
//   face (500 x 600 units, a horizontal gradient, sliced down) whose blue
//   noise would pass the budget at scale 3 (its reference 64 pixels a
//   period, about 30 million units) is drawn by its material with the
//   ordered table (about 14 million), never the built-in recipe: no
//   refusal, one degradation step counted per paint, and its bytes those
//   of the material baked whole with the ordered table. At scale 1 it keeps
//   blue noise and counts none. bake_material_raster lowers it the same way.
// - gradient-dither and the metallic-fade recipe compile; the recipe's fade
//   bands undithered and does not dithered.
//
// `--measure` prints the dither's cost per pixel beside a solid layer's
// (the bake weight material_effort_weights.dither is chosen from it; see
// gui_forms_engine_material_tests --measure). `--preview <dir>` writes a
// large panel with the metallic fade, undithered, ordered and blue noise,
// in light and dark at scales 1 and 2, as PNG files for review. Neither
// decides acceptance.

#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window.hpp"

#include "../src/core/engine/primitives/kernel/dither.hpp"
#include "../src/core/engine/primitives/kernel/pixel_kernel.hpp"
#include "../src/core/engine/primitives/material/material_primitive.hpp"
#include "../src/core/engine/primitives/material/material_raster.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;
using engine_primitives::DitherPlane;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_dither_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

constexpr std::array<MaterialDither, 3> every_dither{MaterialDither::none, MaterialDither::ordered,
                                                     MaterialDither::blue_noise};
constexpr std::array<MaterialDither, 2> dithering{MaterialDither::ordered, MaterialDither::blue_noise};

const char* dither_name(MaterialDither dither) {
    switch (dither) {
    case MaterialDither::none: return "none";
    case MaterialDither::ordered: return "ordered";
    case MaterialDither::blue_noise: return "blue-noise";
    default: break;
    }
    return "unknown";
}

// FNV-1a over bytes, 64-bit.
constexpr std::uint64_t fnv_offset = 0xcbf29ce484222325ULL;
constexpr std::uint64_t fnv_prime = 0x100000001b3ULL;

std::uint64_t fnv_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = fnv_offset;
    for (const std::uint8_t byte : bytes) {
        hash = hash ^ byte;
        hash = hash * fnv_prime;
    }
    return hash;
}

// FNV-1a over 16-bit ranks, each low byte first.
std::uint64_t fnv_ranks(std::span<const std::uint16_t> ranks) {
    std::uint64_t hash = fnv_offset;
    for (const std::uint16_t rank : ranks) {
        hash = hash ^ static_cast<std::uint64_t>(rank & 0xffU);
        hash = hash * fnv_prime;
        hash = hash ^ static_cast<std::uint64_t>(rank >> 8U);
        hash = hash * fnv_prime;
    }
    return hash;
}

// -- rasters -----------------------------------------------------------------------

std::shared_ptr<PartRaster> make_raster(std::uint32_t width, std::uint32_t height, double scale) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{width, height}, DeviceScale{scale});
    require(raster != nullptr, "a test raster is made");
    return raster;
}

std::uint32_t device_side(double logical, double scale) {
    const double side = std::ceil(logical * scale);
    return static_cast<std::uint32_t>(side);
}

PrimitiveTarget target_of(Size size, double scale) {
    PrimitiveTarget target{};
    target.logical_size = size;
    target.scale = DeviceScale{scale};
    return target;
}

// `material` over a box of `size` at `scale` whose top-left corner is the
// device origin, every pixel of the region of `extent` whose top-left pixel
// lies at `origin`.
std::shared_ptr<PartRaster> region_bake(const SurfaceMaterial& material, Size size, double scale, Point origin,
                                        DeviceExtent extent) {
    std::shared_ptr<PartRaster> raster = make_raster(extent.width, extent.height, scale);
    const PrimitiveTarget target = target_of(size, scale);
    engine_primitives::bake_material_region(material, target, origin, (*raster).view());
    return raster;
}

// The box and the material outsets on every side, in device pixels.
DeviceExtent outset_extent(Size size, double scale) {
    const double grown = engine_primitives::material_outset * 2.0;
    const DeviceExtent extent{device_side(size.width + grown, scale), device_side(size.height + grown, scale)};
    return extent;
}

// Whether `width` × `height` pixels of `left` from (left_x, left_y) equal
// those of `right` from (right_x, right_y).
bool same_area(const PartRaster& left, std::uint32_t left_x, std::uint32_t left_y, const PartRaster& right,
               std::uint32_t right_x, std::uint32_t right_y, std::uint32_t width, std::uint32_t height) {
    const std::span<const std::uint8_t> left_pixels = left.pixels();
    const std::span<const std::uint8_t> right_pixels = right.pixels();
    const std::size_t length = static_cast<std::size_t>(width) * part_raster_bytes_per_pixel;
    for (std::uint32_t row = 0U; row < height; ++row) {
        const std::size_t from_left = static_cast<std::size_t>(left_y + row) * left.row_bytes() +
                                      static_cast<std::size_t>(left_x) * part_raster_bytes_per_pixel;
        const std::size_t from_right = static_cast<std::size_t>(right_y + row) * right.row_bytes() +
                                       static_cast<std::size_t>(right_x) * part_raster_bytes_per_pixel;
        if (std::memcmp(left_pixels.data() + from_left, right_pixels.data() + from_right, length) != 0) return false;
    }
    return true;
}

bool same_raster(const PartRaster& left, const PartRaster& right) {
    const DeviceExtent extent = left.extent();
    const bool shaped = extent.width == right.extent().width && extent.height == right.extent().height;
    if (!shaped) return false;
    const bool same = same_area(left, 0U, 0U, right, 0U, 0U, extent.width, extent.height);
    return same;
}

// -- materials ---------------------------------------------------------------------

std::vector<GradientStop> two_stops(Color first, Color last) {
    const std::vector<GradientStop> stops{GradientStop{0.0, first}, GradientStop{1.0, last}};
    return stops;
}

// A gradient alone, from `start` to `end` of the box (normalized), dithered.
SurfaceMaterial gradient_material(Point start, Point end, Color first, Color last, MaterialDither dither) {
    SurfaceMaterial material{};
    material.fills.assign(1U, MaterialFillLayer::linear(start, end, two_stops(first, last)));
    material.dither = dither;
    return material;
}

// The banding case: #808080 to #848484 across the box, four byte steps.
SurfaceMaterial banding_material(MaterialDither dither) {
    const SurfaceMaterial material = gradient_material(Point{0.0, 0.0}, Point{1.0, 0.0}, Color::rgba(128, 128, 128),
                                                       Color::rgba(132, 132, 132), dither);
    return material;
}

// A panel: a low-contrast vertical fade, rounded, bordered, with an outer
// shadow, so that every layer kind near the edges meets the dither.
SurfaceMaterial panel_material(MaterialDither dither) {
    SurfaceMaterial material = gradient_material(Point{0.0, 0.0}, Point{0.0, 1.0}, Color::rgba(236, 236, 234),
                                                 Color::rgba(222, 222, 219), dither);
    material.corner_radius = 6.0;
    material.border = MaterialBorder{Color::rgba(150, 152, 156), 1.0};
    MaterialShadow shadow{};
    shadow.offset = Point{0.0, 2.0};
    shadow.blur_radius = 6.0;
    shadow.color = Color::rgba(0, 0, 0, 60);
    material.shadows.push_back(shadow);
    return material;
}

// The metallic fade (the gallery theme's `.metallic-fade`; dither.hpp's
// positioning): over the face's colour, a thin fade from slightly lighter
// at the top through clear to slightly darker at the foot, a one-unit
// edge catch along the top and a fainter shade along the foot. The same
// amounts as the sheet's custom properties.
struct FadeAmounts final {
    Color light{};
    Color shade{};
    Color catch_light{};
    Color foot{};
};

// Light: 8 %, 5 %, 50 % and 8 % (the sheet's light amounts); dark: 5 %,
// 12 %, 18 % and 25 % (its dark amounts), as alpha bytes.
constexpr FadeAmounts light_fade{Color::rgba(255, 255, 255, 20), Color::rgba(0, 0, 0, 13),
                                 Color::rgba(255, 255, 255, 128), Color::rgba(0, 0, 0, 20)};
constexpr FadeAmounts dark_fade{Color::rgba(255, 255, 255, 13), Color::rgba(0, 0, 0, 31),
                                Color::rgba(255, 255, 255, 46), Color::rgba(0, 0, 0, 64)};

SurfaceMaterial metallic_fade(Color base, const FadeAmounts& amounts, Size size, MaterialDither dither) {
    const Color clear = Color::rgba(0, 0, 0, 0);
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(base));
    const std::vector<GradientStop> fade{GradientStop{0.0, amounts.light}, GradientStop{0.45, clear},
                                         GradientStop{1.0, amounts.shade}};
    material.fills.push_back(MaterialFillLayer::linear_css_angle(180.0, fade));
    // The foot: one logical unit up from the bottom edge.
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, size.height}, Point{0.0, size.height - 1.0},
                                                       two_stops(amounts.foot, clear),
                                                       MaterialCoordinateSpace::logical));
    // The catch: one logical unit down from the top edge.
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0},
                                                       two_stops(amounts.catch_light, clear),
                                                       MaterialCoordinateSpace::logical));
    material.dither = dither;
    return material;
}

// -- the tables --------------------------------------------------------------------

// The 8 x 8 Bayer matrix by its recursion, M(2n) = [[4M, 4M + 2], [4M + 3,
// 4M + 1]], for comparison with the kernel's bit interleaving.
std::uint16_t bayer_by_recursion(std::uint32_t x, std::uint32_t y, std::uint32_t side) {
    if (side == 1U) return 0U;
    const std::uint32_t half = side / 2U;
    const std::uint16_t inner = bayer_by_recursion(x % half, y % half, half);
    const std::array<std::uint16_t, 4> offsets{0U, 2U, 3U, 1U};
    const std::uint32_t quadrant = (y / half) * 2U + (x / half);
    const std::uint16_t rank = static_cast<std::uint16_t>(inner * 4U + offsets[quadrant]);
    return rank;
}

bool is_permutation_of_levels(std::span<const std::uint16_t> ranks) {
    std::vector<std::uint8_t> seen(ranks.size(), 0U);
    for (const std::uint16_t rank : ranks) {
        if (rank >= ranks.size() || seen[rank] != 0U) return false;
        seen[rank] = 1U;
    }
    return true;
}

void tables() {
    const std::span<const std::uint16_t> ordered = engine_primitives::ordered_dither_ranks();
    require(ordered.size() == 64U && is_permutation_of_levels(ordered), "the ordered table holds 0 to 63 once");
    for (std::uint32_t y = 0U; y < 8U; ++y) {
        for (std::uint32_t x = 0U; x < 8U; ++x) {
            require(ordered[static_cast<std::size_t>(y) * 8U + x] == bayer_by_recursion(x, y, 8U),
                    "the ordered table is the Bayer matrix");
        }
    }
    const std::span<const std::uint16_t> blue = engine_primitives::blue_noise_dither_ranks();
    require(blue.size() == 4096U && is_permutation_of_levels(blue), "the blue-noise table holds 0 to 4095 once");
    // Every level is spread: each aligned 8 x 8 block holds within a few
    // of its share of the cells below the level (white noise would swing
    // by twice as much; 2026-11-12 table: 6-10, 14-18, 29-36, 44-51).
    const std::array<std::uint32_t, 4> levels{512U, 1024U, 2048U, 3072U};
    const std::array<std::uint32_t, 4> slack{3U, 3U, 5U, 5U};
    for (std::size_t level = 0U; level < levels.size(); ++level) {
        const std::uint32_t share = levels[level] / 64U;
        for (std::uint32_t block = 0U; block < 64U; ++block) {
            const std::uint32_t left = (block % 8U) * 8U;
            const std::uint32_t top = (block / 8U) * 8U;
            std::uint32_t below = 0U;
            for (std::uint32_t y = top; y < top + 8U; ++y) {
                for (std::uint32_t x = left; x < left + 8U; ++x) {
                    if (blue[static_cast<std::size_t>(y) * 64U + x] < levels[level]) below = below + 1U;
                }
            }
            const std::uint32_t apart = below > share ? below - share : share - below;
            require(apart <= slack[level], "the blue-noise table spreads every level over 8 x 8 blocks");
        }
    }
    if (print_mode()) {
        std::cout << "ordered table " << std::hex << fnv_ranks(ordered) << " blue-noise table " << fnv_ranks(blue)
                  << std::dec << '\n';
        return;
    }
    require(fnv_ranks(ordered) == 0xb872899e883b20a5ULL, "the ordered table's hash is fixed");
    require(fnv_ranks(blue) == 0x6bea65d71bd1eb7dULL, "the blue-noise table's hash is fixed");
    require(engine_primitives::dither_period(MaterialDither::none) == 1U &&
                engine_primitives::dither_period(MaterialDither::ordered) == 8U &&
                engine_primitives::dither_period(MaterialDither::blue_noise) == 64U,
            "each mode's period");
}

// -- the threshold -----------------------------------------------------------------

void thresholds() {
    for (const MaterialDither dither : dithering) {
        const DitherPlane plane = engine_primitives::dither_plane(dither, 0, 0);
        const std::size_t cells = static_cast<std::size_t>(plane.side) * plane.side;
        for (std::size_t cell = 0U; cell < cells; ++cell) {
            const double threshold = engine_primitives::dither_threshold(plane, plane.ranks[cell]);
            require(threshold > 0.0 && threshold < 1.0, "a threshold lies strictly inside (0, 1)");
            for (std::uint32_t byte = 0U; byte < 256U; ++byte) {
                const double exact = static_cast<double>(byte) / 255.0;
                require(engine_primitives::to_byte_dithered(exact, threshold) == byte,
                        "an exact byte value is kept under every threshold");
            }
            require(engine_primitives::to_byte_dithered(-0.5, threshold) == 0U &&
                        engine_primitives::to_byte_dithered(1.5, threshold) == 255U,
                    "values outside [0, 1] are clamped");
        }
    }
    for (std::uint32_t step = 0U; step <= 4096U; ++step) {
        const double value = static_cast<double>(step) / 4096.0;
        require(engine_primitives::to_byte_dithered(value, 0.5) == engine_primitives::to_byte(value),
                "a threshold of one half rounds");
    }
    const DitherPlane none = engine_primitives::dither_plane(MaterialDither::none, 0, 0);
    require(none.ranks == nullptr && none.side == 1U, "no dither has no table");
    // The cell is the offset from the anchor modulo the period, negative
    // offsets included.
    const DitherPlane blue = engine_primitives::dither_plane(MaterialDither::blue_noise, 5, -3);
    require(engine_primitives::dither_cell(blue, 5, 5) == 0U && engine_primitives::dither_cell(blue, 4, 5) == 63U &&
                engine_primitives::dither_cell(blue, 69, 5) == 0U && engine_primitives::dither_cell(blue, -60, 5) == 63U,
            "a cell wraps both ways from its anchor");
}

// -- banding -----------------------------------------------------------------------

struct BandingMetrics final {
    // The mean over rows of the steps (changes of byte) along a row, and
    // the longest run of equal bytes along any row.
    double steps_per_row{};
    std::uint32_t longest_run{};
    // The largest gap between an aligned 8 x 8 block's mean byte and the
    // exact ramp's mean over it, and between the whole raster's mean and
    // the exact ramp's.
    double block_error{};
    double mean_error{};
};

// The green channel's metrics of a raster of the banding material, whose
// exact value at column x is `low` + (`high` − `low`) × (x + 1/2) / width.
BandingMetrics banding_metrics(const PartRaster& raster, double low, double high) {
    const DeviceExtent extent = raster.extent();
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const std::size_t green = 1U;
    BandingMetrics metrics{};
    std::uint64_t steps = 0U;
    double total = 0.0;
    for (std::uint32_t row = 0U; row < extent.height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes();
        std::uint32_t run = 1U;
        for (std::uint32_t column = 0U; column < extent.width; ++column) {
            const std::uint8_t byte = pixels[start + static_cast<std::size_t>(column) * 4U + green];
            total = total + static_cast<double>(byte);
            if (column == 0U) continue;
            const std::uint8_t before = pixels[start + static_cast<std::size_t>(column - 1U) * 4U + green];
            if (byte == before) {
                run = run + 1U;
            } else {
                steps = steps + 1U;
                run = 1U;
            }
            metrics.longest_run = std::max(metrics.longest_run, run);
        }
    }
    metrics.steps_per_row = static_cast<double>(steps) / static_cast<double>(extent.height);
    const double width = static_cast<double>(extent.width);
    const double exact_mean = (low + high) * 0.5;
    metrics.mean_error = std::abs(total / (width * static_cast<double>(extent.height)) - exact_mean);
    for (std::uint32_t top = 0U; top + 8U <= extent.height; top += 8U) {
        for (std::uint32_t left = 0U; left + 8U <= extent.width; left += 8U) {
            double sum = 0.0;
            double exact = 0.0;
            for (std::uint32_t row = top; row < top + 8U; ++row) {
                const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes();
                for (std::uint32_t column = left; column < left + 8U; ++column) {
                    sum = sum + static_cast<double>(pixels[start + static_cast<std::size_t>(column) * 4U + green]);
                    exact = exact + low + (high - low) * ((static_cast<double>(column) + 0.5) / width);
                }
            }
            const double error = std::abs(sum - exact) / 64.0;
            metrics.block_error = std::max(metrics.block_error, error);
        }
    }
    return metrics;
}

constexpr std::uint32_t banding_width = 1024U;
constexpr std::uint32_t banding_height = 128U;

std::shared_ptr<PartRaster> banding_bake(MaterialDither dither) {
    const SurfaceMaterial material = banding_material(dither);
    const Size size{static_cast<double>(banding_width), static_cast<double>(banding_height)};
    std::shared_ptr<PartRaster> raster =
        region_bake(material, size, 1.0, Point{0.0, 0.0}, DeviceExtent{banding_width, banding_height});
    return raster;
}

void banding() {
    const std::shared_ptr<PartRaster> plain = banding_bake(MaterialDither::none);
    const std::shared_ptr<PartRaster> ordered = banding_bake(MaterialDither::ordered);
    const std::shared_ptr<PartRaster> blue = banding_bake(MaterialDither::blue_noise);
    const BandingMetrics rounded = banding_metrics(*plain, 128.0, 132.0);
    const BandingMetrics bayer = banding_metrics(*ordered, 128.0, 132.0);
    const BandingMetrics noise = banding_metrics(*blue, 128.0, 132.0);
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "banding none: steps/row " << rounded.steps_per_row << ", longest run " << rounded.longest_run
              << ", block error " << rounded.block_error << '\n';
    std::cout << "banding ordered: steps/row " << bayer.steps_per_row << ", longest run " << bayer.longest_run
              << ", block error " << bayer.block_error << '\n';
    std::cout << "banding blue-noise: steps/row " << noise.steps_per_row << ", longest run " << noise.longest_run
              << ", block error " << noise.block_error << '\n';
    // Undithered: five bytes, four steps, a run of 256 between them, and a
    // block mean up to almost half a byte off the ramp (the band).
    require(rounded.steps_per_row == 4.0 && rounded.longest_run >= 250U, "undithered, the ramp shows four bands");
    require(rounded.block_error > 0.4, "undithered, a block misses the ramp by almost half a byte");
    // Dithered (2026-11-12 emulation: ordered 512 steps, run 98, block error
    // 1/64; blue noise 415 steps, run 60, block error 3/64).
    require(bayer.steps_per_row >= 256.0 && bayer.longest_run <= 128U, "ordered, the bands break up");
    require(noise.steps_per_row >= 256.0 && noise.longest_run <= 96U, "blue noise, the bands break up");
    require(bayer.block_error <= 1.0 / 32.0, "ordered, every 8 x 8 block keeps the ramp's mean");
    require(noise.block_error <= 1.0 / 16.0, "blue noise, every 8 x 8 block keeps the ramp's mean");
    require(bayer.mean_error <= 0.01 && noise.mean_error <= 0.01, "dithering keeps the mean");
}

// -- determinism -------------------------------------------------------------------

// The banding bakes' hashes, BGRA bytes row by row (no padding: the width
// is a multiple of every row alignment), from an emulation of the kernel's
// arithmetic (stop_colour, premultiply, to_byte_dithered) in Python's IEEE
// doubles, 2026-11-12.
constexpr std::array<std::uint64_t, 3> banding_hashes{0x11ef8013f7242325ULL, 0xffd6d52740c9fb25ULL,
                                                      0x0eb20c3012b03db5ULL};

std::uint64_t packed_hash(const PartRaster& raster) {
    const DeviceExtent extent = raster.extent();
    const std::size_t live = static_cast<std::size_t>(extent.width) * part_raster_bytes_per_pixel;
    std::vector<std::uint8_t> packed(live * extent.height);
    const std::span<const std::uint8_t> pixels = raster.pixels();
    for (std::uint32_t row = 0U; row < extent.height; ++row) {
        std::memcpy(packed.data() + static_cast<std::size_t>(row) * live,
                    pixels.data() + static_cast<std::size_t>(row) * raster.row_bytes(), live);
    }
    const std::uint64_t hash = fnv_bytes(packed);
    return hash;
}

void determinism() {
    for (std::size_t index = 0U; index < every_dither.size(); ++index) {
        const std::shared_ptr<PartRaster> first = banding_bake(every_dither[index]);
        const std::shared_ptr<PartRaster> second = banding_bake(every_dither[index]);
        require(same_raster(*first, *second), "a bake repeated gives the same bytes");
        const std::uint64_t hash = packed_hash(*first);
        if (print_mode()) {
            std::cout << "banding " << dither_name(every_dither[index]) << ' ' << std::hex << hash << std::dec << '\n';
            continue;
        }
        require(hash == banding_hashes[index], "the bake's bytes are the reference's on every host");
    }
}

// -- damage invariance ---------------------------------------------------------------

// Uneven cuts of one side of `length` pixels: 0, a few in, a third, and
// the end.
std::array<std::uint32_t, 4> cuts_of(std::uint32_t length) {
    const std::array<std::uint32_t, 4> cuts{0U, std::min(length, 13U), std::max(std::min(length, 13U), length / 3U),
                                            length};
    return cuts;
}

void damage_invariance() {
    const Size size{180.0, 120.0};
    constexpr std::array<double, 3> scales{1.0, 2.0, 3.0};
    for (const MaterialDither dither : dithering) {
        const SurfaceMaterial material = panel_material(dither);
        for (const double scale : scales) {
            const DeviceExtent extent = outset_extent(size, scale);
            const double origin = -engine_primitives::material_outset * scale;
            const std::shared_ptr<PartRaster> whole = region_bake(material, size, scale, Point{origin, origin}, extent);
            // The bake with repeated spans, a whole period at a time.
            std::shared_ptr<PartRaster> spans = make_raster(extent.width, extent.height, scale);
            engine_primitives::bake_material(material, target_of(size, scale), (*spans).view());
            require(same_raster(*spans, *whole), "the bake with repeated spans keeps the dither's pattern");
            // Uneven tiles, last first.
            const std::array<std::uint32_t, 4> columns = cuts_of(extent.width);
            const std::array<std::uint32_t, 4> rows = cuts_of(extent.height);
            for (std::size_t down = 3U; down > 0U; --down) {
                for (std::size_t across = 3U; across > 0U; --across) {
                    const std::uint32_t left = columns[across - 1U];
                    const std::uint32_t top = rows[down - 1U];
                    const std::uint32_t width = columns[across] - left;
                    const std::uint32_t height = rows[down] - top;
                    if (width == 0U || height == 0U) continue;
                    const Point tile_origin{origin + static_cast<double>(left), origin + static_cast<double>(top)};
                    const std::shared_ptr<PartRaster> tile =
                        region_bake(material, size, scale, tile_origin, DeviceExtent{width, height});
                    require(same_area(*tile, 0U, 0U, *whole, left, top, width, height),
                            "a repaint of any rectangle gives the full repaint's bytes");
                }
            }
        }
    }
}

// -- slices ------------------------------------------------------------------------

void slices_keep_the_pattern() {
    const Color light = Color::rgba(238, 238, 236);
    const Color dark = Color::rgba(226, 226, 223);
    const std::array<Size, 2> sizes{Size{300.0, 160.0}, Size{257.0, 141.0}};
    constexpr std::array<double, 2> scales{1.0, 2.0};
    for (const MaterialDither dither : every_dither) {
        std::vector<SurfaceMaterial> materials{};
        // Down: sliced across. Across: sliced down. A translucent solid:
        // sliced both ways, its fractional bytes dithered everywhere.
        materials.push_back(gradient_material(Point{0.0, 0.0}, Point{0.0, 1.0}, light, dark, dither));
        materials.push_back(gradient_material(Point{0.0, 0.0}, Point{1.0, 0.0}, light, dark, dither));
        SurfaceMaterial translucent{};
        translucent.fills.assign(1U, MaterialFillLayer::solid(Color::rgba(40, 90, 160, 77)));
        translucent.corner_radius = 4.0;
        translucent.dither = dither;
        materials.push_back(translucent);
        const std::array<bool, 3> across_sliced{true, false, true};
        const std::array<bool, 3> down_sliced{false, true, true};
        for (std::size_t index = 0U; index < materials.size(); ++index) {
            const SurfaceMaterial& material = materials[index];
            for (const Size size : sizes) {
                for (const double scale : scales) {
                    const double outset = detail::material_raster_outset(material);
                    const DeviceExtent extent = outset_extent(size, scale);
                    const PrimitiveSlicePlan plan =
                        engine_primitives::material_slice_plan(material, target_of(size, scale), extent, outset);
                    const std::uint32_t period = engine_primitives::dither_period(dither);
                    const bool across = plan.across.outcome == SliceAxisOutcome::sliced;
                    const bool down = plan.down.outcome == SliceAxisOutcome::sliced;
                    require(across == across_sliced[index] && down == down_sliced[index],
                            "the material slices along the axes it does not vary along");
                    require((!across || plan.across.period == period) && (!down || plan.down.period == period),
                            "a sliced axis repeats whole periods of the dither");
                    const std::shared_ptr<const PartRaster> assembled =
                        detail::bake_material_raster(material, size, DeviceScale{scale}, outset);
                    require(assembled != nullptr, "the material is within the bake budget");
                    const double origin = -outset * scale;
                    const std::shared_ptr<PartRaster> whole =
                        region_bake(material, size, scale, Point{origin, origin}, extent);
                    require(same_raster(*assembled, *whole), "a raster assembled from slices keeps the pattern");
                }
            }
        }
    }
}

// -- scrolling ---------------------------------------------------------------------

void no_swimming_under_scroll() {
    const Size content{160.0, 640.0};
    constexpr std::array<double, 2> scales{1.0, 2.0};
    constexpr std::array<std::uint32_t, 6> offsets{0U, 1U, 5U, 63U, 64U, 130U};
    for (const MaterialDither dither : dithering) {
        const SurfaceMaterial material = gradient_material(Point{0.0, 0.0}, Point{0.0, 1.0}, Color::rgba(70, 72, 76),
                                                           Color::rgba(56, 58, 61), dither);
        for (const double scale : scales) {
            const std::uint32_t width = device_side(content.width, scale);
            const std::uint32_t height = device_side(content.height, scale);
            const std::uint32_t viewport = device_side(200.0, scale);
            const std::shared_ptr<PartRaster> whole =
                region_bake(material, content, scale, Point{0.0, 0.0}, DeviceExtent{width, height});
            for (const std::uint32_t offset : offsets) {
                const std::uint32_t scrolled = device_side(static_cast<double>(offset), scale);
                // The viewport shows the content from `scrolled` down: the
                // content is anchored to its own box, not to the viewport.
                const std::shared_ptr<PartRaster> shown =
                    region_bake(material, content, scale, Point{0.0, static_cast<double>(scrolled)},
                                DeviceExtent{width, viewport});
                require(same_area(*shown, 0U, 0U, *whole, 0U, scrolled, width, viewport),
                        "a scrolled surface shows its content's own bytes: the dither does not swim");
            }
        }
    }
}

// -- the bake budget ---------------------------------------------------------------

// The tall face: 1548 x 1848 device pixels at scale 3 with its outsets. Its
// gradient runs across, so only the down axis slices; the reference is
// 3 + 0, 8 + 0 or 64 + 8 device pixels long (period plus phase) undithered,
// ordered or blue noise, 51, 56 or 120 with the outsets, at 155, 160 and
// 160 units a pixel: 12.2, 13.9 and 29.7 million units against the
// budget's 20 million. At scale 1 the blue-noise reference is 104 pixels
// tall and 516 wide, 8.6 million units.
constexpr Size tall_face{500.0, 600.0};

SurfaceMaterial tall_material(MaterialDither dither) {
    const SurfaceMaterial material = gradient_material(Point{0.0, 0.0}, Point{1.0, 0.0}, Color::rgba(196, 198, 202),
                                                       Color::rgba(178, 180, 185), dither);
    return material;
}

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A painter that keeps the last part raster composited, and draws nothing.
class RasterWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        last = raster;
    }
    std::shared_ptr<const PartRaster> last{};
};

// One button-family face of tall_face at (20, 20), drawn by the material
// primitive with the button recipe.
class TallFace final : public Control {
public:
    explicit TallFace(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, tall_face.width, tall_face.height};
        part.key = PartKey{"face", {}, {}};
        return part;
    }
};

ThemeRule tall_face_rule() {
    ThemeRule rule{};
    rule.label = "tall-face";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = "tall";
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    return rule;
}

// The professional theme with every button state's material the tall
// face's, blue noise. The definition is held on the heap (some 105 KB,
// theme_types.hpp).
std::shared_ptr<const Theme> tall_face_theme() {
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    definition.id = "engine-dither-budget-test";
    const SurfaceMaterial material = tall_material(MaterialDither::blue_noise);
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& recipe : button.ordinary) recipe.material = material;
    definition.rules.rules.push_back(tall_face_rule());
    const std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

void primitive_lowers_the_dither() {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find("material");
    require(primitive != nullptr, "the built-in registry has the material primitive");
    const PrimitiveDescriptor& descriptor = (*primitive).descriptor();
    PrimitiveParameterBlock parameters = default_parameters(descriptor);
    require(set_choice_parameter(parameters, descriptor, "recipe", "button") == ParameterAssignment::assigned,
            "a recipe role is a choice");
    const SurfaceMaterial blue = tall_material(MaterialDither::blue_noise);
    const SurfaceMaterial ordered = tall_material(MaterialDither::ordered);
    PrimitiveTarget target = target_of(tall_face, 3.0);
    target.material = &blue;
    require(!(*primitive).accepts(parameters, target), "the tall face's blue noise is past the budget at scale 3");
    require((*primitive).affordable_dither(parameters, target) == MaterialDither::ordered,
            "blue noise gives way to the ordered table");
    target.material = &ordered;
    require((*primitive).accepts(parameters, target), "the ordered table is within the budget");
    target = target_of(tall_face, 1.0);
    target.material = &blue;
    require((*primitive).affordable_dither(parameters, target) == MaterialDither::blue_noise,
            "within the budget, blue noise is kept");

    // The material painted outside the engine: lowered, not dropped.
    const double outset = detail::material_raster_outset(blue);
    const std::shared_ptr<const PartRaster> baked =
        detail::bake_material_raster(blue, tall_face, DeviceScale{3.0}, outset);
    require(baked != nullptr, "the tall face's raster is baked with a lowered dither");
    const double origin = -outset * 3.0;
    const std::shared_ptr<PartRaster> whole =
        region_bake(ordered, tall_face, 3.0, Point{origin, origin}, outset_extent(tall_face, 3.0));
    require(same_raster(*baked, *whole), "the raster is the material with the ordered table");
}

void tall_face_stays_themed() {
    constexpr std::array<double, 2> scales{1.0, 3.0};
    for (const double scale : scales) {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
        const std::shared_ptr<TallFace> face = make_control<TallFace>(StableId("tall"));
        (*face).set_requested_bounds(Rect{0.0, 0.0, tall_face.width + 60.0, tall_face.height + 60.0});
        (*root).add_child(face);
        // The clock and the painter outlive the window that borrows them.
        ManualTime time{};
        RasterWatch painter{};
        Window window(root, Size{tall_face.width + 100.0, tall_face.height + 100.0});
        window.motion_clock().set_time_source(&time);
        window.set_scale(scale);
        window.set_theme(tall_face_theme());
        require(window.theme().engine().rules().diagnostics().empty(), "the tall face's theme compiles cleanly");
        window.perform_layout();
        window.paint(painter);
        const MetricsSnapshot metrics = window.metrics_snapshot();
        require(metrics.engine_primitive_refusals == 0U, "the tall face is drawn by its material, never refused");
        require(painter.last != nullptr, "the tall face composites a raster");
        const bool lowered = scale > 2.0;
        if (lowered) {
            require(metrics.engine_dither_degradations >= 1U, "the lowered dither is counted");
        } else {
            require(metrics.engine_dither_degradations == 0U, "a face within the budget keeps its dither");
        }
        const MaterialDither shown = lowered ? MaterialDither::ordered : MaterialDither::blue_noise;
        const DeviceExtent extent = outset_extent(tall_face, scale);
        const double origin = -engine_primitives::material_outset * scale;
        const std::shared_ptr<PartRaster> whole =
            region_bake(tall_material(shown), tall_face, scale, Point{origin, origin}, extent);
        require(same_raster(*painter.last, *whole), "the face shows its material with the affordable dither");
    }
}

// -- the theme language and the recipe ---------------------------------------------

bool sheet_compiles_cleanly(std::string_view sheet) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemePackage package = theme_sheet_package(sheet, "dither-sheet");
    const ThemeCompileResult result = compile_theme_package(package, library, images, options);
    const bool clean = result.succeeded() && result.diagnostics().empty();
    return clean;
}

// The gallery theme's recipe as it writes it (demo/themes/gallery/
// gallery.theme.css), on a panel's face.
constexpr std::string_view metallic_fade_sheet =
    ":root {\n"
    "  --metallic-fade-light: rgb(255 255 255 / 8%);\n"
    "  --metallic-fade-shade: rgb(0 0 0 / 5%);\n"
    "  --metallic-fade-catch: rgb(255 255 255 / 50%);\n"
    "  --metallic-fade-foot: rgb(0 0 0 / 8%);\n"
    "}\n"
    ".metallic-fade::part(face) {\n"
    "  background-image:\n"
    "    linear-gradient(180deg, var(--metallic-fade-catch) 0px, transparent 1px),\n"
    "    linear-gradient(0deg, var(--metallic-fade-foot) 0px, transparent 1px),\n"
    "    linear-gradient(180deg, var(--metallic-fade-light), transparent 45%, var(--metallic-fade-shade));\n"
    "  gradient-dither: blue-noise;\n"
    "}\n";

void theme_language() {
    require(sheet_compiles_cleanly("button::part(face) { background: linear-gradient(#808080, #848484); "
                                   "gradient-dither: ordered }\n"),
            "gradient-dither compiles on a part with a gradient");
    require(sheet_compiles_cleanly("button::part(face) { gradient-dither: blue-noise }\n"),
            "gradient-dither compiles alone");
    require(!sheet_compiles_cleanly("button::part(face) { gradient-dither: stipple }\n"),
            "an unknown dither is refused");
    require(sheet_compiles_cleanly(metallic_fade_sheet), "the metallic-fade recipe compiles");
}

// The fade's own banding: a 540-unit panel's middle column, top to
// bottom, steps only where the fade crosses a byte undithered, and
// dithered breaks each band into many short runs.
void metallic_fade_does_not_band() {
    const Size size{96.0, 540.0};
    const Color base = Color::rgba(232, 232, 230);
    std::array<std::uint32_t, 3> runs{};
    for (std::size_t index = 0U; index < every_dither.size(); ++index) {
        const SurfaceMaterial material = metallic_fade(base, light_fade, size, every_dither[index]);
        const std::shared_ptr<PartRaster> raster =
            region_bake(material, size, 1.0, Point{0.0, 0.0}, DeviceExtent{96U, 540U});
        const std::span<const std::uint8_t> pixels = (*raster).pixels();
        const std::size_t column = 48U * 4U + 1U;
        std::uint32_t run = 1U;
        std::uint32_t longest = 1U;
        // Past the catch and above the foot: rows 2 to 537.
        for (std::uint32_t row = 3U; row < 538U; ++row) {
            const std::uint8_t byte = pixels[static_cast<std::size_t>(row) * (*raster).row_bytes() + column];
            const std::uint8_t before = pixels[static_cast<std::size_t>(row - 1U) * (*raster).row_bytes() + column];
            run = byte == before ? run + 1U : 1U;
            longest = std::max(longest, run);
        }
        runs[index] = longest;
    }
    std::cout << "metallic fade longest run: none " << runs[0] << ", ordered " << runs[1] << ", blue-noise "
              << runs[2] << '\n';
    require(runs[0] >= 24U, "undithered, the fade shows bands");
    require(runs[1] * 2U <= runs[0] && runs[2] * 2U <= runs[0], "dithered, the fade's bands break up");
}

// -- measuring and previewing ------------------------------------------------------

double median_bake_nanoseconds(const SurfaceMaterial& material) {
    const Size logical{240.0, 240.0};
    const DeviceExtent extent = outset_extent(logical, 1.0);
    std::shared_ptr<PartRaster> raster = make_raster(extent.width, extent.height, 1.0);
    const PrimitiveTarget target = target_of(logical, 1.0);
    const PartRasterView view = (*raster).view();
    std::array<double, 9> samples{};
    for (double& sample : samples) {
        const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
        engine_primitives::bake_material(material, target, view);
        const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
        sample = std::chrono::duration<double, std::nano>(end - start).count();
    }
    std::sort(samples.begin(), samples.end());
    return samples[4];
}

// The dither's work per pixel: a diagonal gradient (no repeated spans)
// dithered, less the same gradient rounded; and a solid layer's, for the
// weight's scale (gui_forms_engine_material_tests --measure's method).
void measure() {
    const double pixels = 256.0 * 256.0;
    const Color red = Color::rgba(255, 0, 0);
    const Color blue = Color::rgba(0, 0, 255);
    SurfaceMaterial base{};
    base.fills.assign(1U, MaterialFillLayer::solid(red));
    SurfaceMaterial solid = base;
    solid.fills.push_back(MaterialFillLayer::solid(blue));
    const SurfaceMaterial rounded =
        gradient_material(Point{0.0, 0.0}, Point{1.0, 1.0}, red, blue, MaterialDither::none);
    const double base_time = median_bake_nanoseconds(base);
    const double solid_per_pixel = (median_bake_nanoseconds(solid) - base_time) / pixels;
    const double rounded_time = median_bake_nanoseconds(rounded);
    const std::uint32_t solid_weight = engine_primitives::material_effort_weights.solid;
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "solid " << solid_per_pixel << " ns per pixel (weight " << solid_weight << ")\n";
    for (const MaterialDither dither : dithering) {
        const SurfaceMaterial dithered = gradient_material(Point{0.0, 0.0}, Point{1.0, 1.0}, red, blue, dither);
        const double per_pixel = (median_bake_nanoseconds(dithered) - rounded_time) / pixels;
        const double weight = per_pixel / solid_per_pixel * static_cast<double>(solid_weight);
        std::cout << "dither " << dither_name(dither) << ' ' << per_pixel << " ns per pixel, weight " << weight
                  << " (now " << engine_primitives::material_effort_weights.dither << ")\n";
    }
}

// `raster` as a PNG at `path`; false when it cannot be encoded or written.
bool write_png(const PartRaster& raster, double scale, const std::filesystem::path& path) {
    WindowCapture capture{};
    capture.pixel_width = raster.extent().width;
    capture.pixel_height = raster.extent().height;
    capture.scale = scale;
    capture.logical_size = Size{static_cast<double>(capture.pixel_width) / scale,
                                static_cast<double>(capture.pixel_height) / scale};
    const std::size_t live = static_cast<std::size_t>(capture.pixel_width) * 4U;
    capture.bgra.resize(live * capture.pixel_height);
    const std::span<const std::uint8_t> pixels = raster.pixels();
    for (std::uint32_t row = 0U; row < capture.pixel_height; ++row) {
        const std::size_t source = static_cast<std::size_t>(row) * raster.row_bytes();
        std::memcpy(capture.bgra.data() + static_cast<std::size_t>(row) * live, pixels.data() + source, live);
    }
    std::vector<std::byte> png{};
    if (!encode_window_capture_png(capture, png)) return false;
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    const bool written = static_cast<bool>(file);
    return written;
}

// A 960 x 540 panel with the metallic fade over the gallery's navigation
// paper, light (#e8e8e6) and dark (#2a2a28), each dither, at scales 1 and
// 2: metallic-fade-<scheme>-<dither>-<scale>x.png.
int preview(const std::filesystem::path& directory) {
    std::error_code made{};
    std::filesystem::create_directories(directory, made);
    if (made) {
        std::cerr << "gui_forms_engine_dither_tests: cannot make " << directory << '\n';
        return 1;
    }
    const Size size{960.0, 540.0};
    const std::array<Color, 2> bases{Color::rgba(0xe8, 0xe8, 0xe6), Color::rgba(0x2a, 0x2a, 0x28)};
    const std::array<const FadeAmounts*, 2> amounts{&light_fade, &dark_fade};
    const std::array<const char*, 2> schemes{"light", "dark"};
    constexpr std::array<double, 2> scales{1.0, 2.0};
    for (std::size_t scheme = 0U; scheme < bases.size(); ++scheme) {
        for (const MaterialDither dither : every_dither) {
            const SurfaceMaterial material = metallic_fade(bases[scheme], *amounts[scheme], size, dither);
            for (const double scale : scales) {
                const DeviceExtent extent{device_side(size.width, scale), device_side(size.height, scale)};
                const std::shared_ptr<PartRaster> raster = region_bake(material, size, scale, Point{0.0, 0.0}, extent);
                const std::string name = std::string("metallic-fade-") + schemes[scheme] + "-" + dither_name(dither) +
                                         "-" + std::to_string(static_cast<int>(scale)) + "x.png";
                const std::filesystem::path path = directory / name;
                if (!write_png(*raster, scale, path)) {
                    std::cerr << "gui_forms_engine_dither_tests: cannot write " << path << '\n';
                    return 1;
                }
                std::cout << path.string() << '\n';
            }
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        measure();
        return 0;
    }
    if (argc > 2 && std::strcmp(argv[1], "--preview") == 0) return preview(argv[2]);
    tables();
    determinism();
    if (print_mode()) return 0;
    thresholds();
    banding();
    damage_invariance();
    slices_keep_the_pattern();
    no_swimming_under_scroll();
    primitive_lowers_the_dither();
    tall_face_stays_themed();
    theme_language();
    metallic_fade_does_not_band();
    std::cout << "gui_forms_engine_dither_tests passed\n";
    return 0;
}
