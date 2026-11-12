// Surface finishes (skeudoindustrial surfaces, phase 1;
// notes:planning/SKEUDOINDUSTRIAL_SURFACES.md): the finish kernel
// (src/core/engine/primitives/finish) and its material fill layer.
//
// - The kernel's bytes equal the JavaScript reference's
//   (tools/industrial_surfaces/finish_kernel.js): the table below holds the
//   FNV-1a hashes of the reference's pixels in RGBA order, printed by
//   `node tools/industrial_surfaces/finish_reference.mjs hashes`, for every
//   finish at scales 1, 2 and 3 and two cases with another grain and seed.
//   GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the kernel's table instead of
//   checking it; `finish_reference.mjs bytes <dir>` writes the reference's
//   pixels for a byte by byte comparison.
// - Damage invariance: at scales 1, 1.5, 2 and 3, a surface painted in
//   uneven bands of rows, uneven bands of columns, or uneven tiles in
//   reverse order holds exactly the whole paint's bytes; a surface offset in
//   its destination, or beginning above and left of it, is anchored to its
//   own origin; bytes outside the damage and the surface are left alone.
// - Two scratch rows are all the kernel needs, and too few samples paint
//   nothing.
// - The seed, the grain and the light change the bytes; equal parameters
//   give equal bytes; the cloth's warp and weft show both colours; the flat
//   finish is one colour.
// - Through the engine: a material whose fill is a finish shows the
//   kernel's pixels, anchored to the part's box, at scales 1 and 2; the bake
//   budget refuses a finish too large to paint.
// - The theme language's finish() compiles, and its refusals.
//
// `--measure` prints what each finish's effort weight (finish_kernel.hpp)
// is chosen from: the median of nine paints of a 256 x 256 surface at
// scale 1, in nanoseconds per device pixel; the ratio to a solid layer's
// (gui_forms_engine_material_tests --measure) times the solid layer's
// weight suggests the weight. `--preview <dir>` writes each finish at
// scales 1 and 2 as PNG files (finish-<kind>-<scale>x.png), for review
// without a window. Neither decides acceptance.

#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window.hpp"

#include "../src/core/engine/primitives/finish/finish_kernel.hpp"
#include "../src/core/engine/primitives/material/material_bake_policy.hpp"
#include "support/rgba_order.hpp"

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
#include <vector>

namespace {
using namespace gui_forms;
using engine_primitives::FinishRect;
using engine_primitives::FinishScratch;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_finish_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

constexpr std::array<FinishKind, 8> finish_kinds{
    FinishKind::dusty_matte, FinishKind::orange_peel, FinishKind::hammertone, FinishKind::wrinkle,
    FinishKind::bead_blast,  FinishKind::sand_cast,   FinishKind::flat,       FinishKind::cloth_weave,
};

const char* kind_name(FinishKind kind) {
    switch (kind) {
    case FinishKind::dusty_matte: return "dusty-matte";
    case FinishKind::orange_peel: return "orange-peel";
    case FinishKind::hammertone: return "hammertone";
    case FinishKind::wrinkle: return "wrinkle";
    case FinishKind::bead_blast: return "bead-blast";
    case FinishKind::sand_cast: return "sand-cast";
    case FinishKind::flat: return "flat";
    case FinishKind::cloth_weave: return "cloth-weave";
    default: break;
    }
    return "unknown";
}

// The study's fill for `kind`: its seed, paint and dust, and for the cloth
// its weft (#c8b98f), as finish_reference.mjs paints them.
FinishFill study_fill(FinishKind kind) {
    FinishFill fill{};
    fill.kind = kind;
    fill.seed = 7;
    fill.paint = Color::rgba(0x2f, 0x6e, 0x6c);
    fill.secondary = kind == FinishKind::cloth_weave ? Color::rgba(0xc8, 0xb9, 0x8f) : Color::rgba(0xbd, 0xb6, 0xa8);
    return fill;
}

// -- rasters -----------------------------------------------------------------------

std::shared_ptr<PartRaster> make_raster(std::uint32_t width, std::uint32_t height) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{width, height}, DeviceScale{1.0});
    require(raster != nullptr, "a test raster is made");
    return raster;
}

// Sets every byte of `raster` to `value`: a sentinel the kernel never
// writes as a whole pixel, since every pixel it writes is opaque.
void fill_bytes(PartRaster& raster, std::uint8_t value) {
    const PartRasterView view = raster.view();
    std::fill(view.pixels.begin(), view.pixels.end(), value);
}

std::size_t paint_into(const FinishFill& fill, double scale, FinishRect surface, FinishRect damage, PartRaster& raster,
                       FinishScratch& scratch) {
    const PartRasterView view = raster.view();
    const std::size_t painted = engine_primitives::paint_finish(fill, scale, surface, damage, view, scratch);
    return painted;
}

// `fill` painted whole over a `width` x `height` surface at the
// destination's origin.
std::shared_ptr<PartRaster> whole_finish(const FinishFill& fill, double scale, std::uint32_t width,
                                         std::uint32_t height) {
    std::shared_ptr<PartRaster> raster = make_raster(width, height);
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, width);
    const FinishRect whole{0, 0, static_cast<std::int64_t>(width), static_cast<std::int64_t>(height)};
    const std::size_t painted = paint_into(fill, scale, whole, whole, *raster, scratch);
    require(painted == static_cast<std::size_t>(width) * height, "a whole paint writes every pixel");
    return raster;
}

// FNV-1a over a raster's live bytes in RGBA order, row by row, as
// finish_reference.mjs hashes the reference's.
std::uint64_t raster_hash(const PartRaster& raster) {
    std::uint64_t hash = 14695981039346656037ULL;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const std::size_t live = static_cast<std::size_t>(raster.extent().width) * 4U;
    for (std::uint32_t row = 0U; row < raster.extent().height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes();
        for (const std::uint8_t byte : gui_forms::test_support::rgba_order(pixels.subspan(start, live))) {
            hash = hash ^ byte;
            hash = hash * 1099511628211ULL;
        }
    }
    return hash;
}

// Whether `width` x `height` pixels of `left` from (left_x, left_y) equal
// those of `right` from (right_x, right_y). Both areas lie inside.
bool same_area(const PartRaster& left, std::uint32_t left_x, std::uint32_t left_y, const PartRaster& right,
               std::uint32_t right_x, std::uint32_t right_y, std::uint32_t width, std::uint32_t height) {
    const std::span<const std::uint8_t> left_pixels = left.pixels();
    const std::span<const std::uint8_t> right_pixels = right.pixels();
    const std::size_t live = static_cast<std::size_t>(width) * 4U;
    for (std::uint32_t row = 0U; row < height; ++row) {
        const std::size_t left_start = static_cast<std::size_t>(left_y + row) * left.row_bytes() + left_x * 4U;
        const std::size_t right_start = static_cast<std::size_t>(right_y + row) * right.row_bytes() + right_x * 4U;
        const bool same = std::memcmp(left_pixels.data() + left_start, right_pixels.data() + right_start, live) == 0;
        if (!same) return false;
    }
    return true;
}

// Whether every byte of the area is `value`. An empty area holds.
bool area_is(const PartRaster& raster, std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
             std::uint8_t value) {
    const std::span<const std::uint8_t> pixels = raster.pixels();
    for (std::uint32_t row = 0U; row < height; ++row) {
        const std::size_t start = static_cast<std::size_t>(y + row) * raster.row_bytes() + x * 4U;
        for (std::size_t at = start; at < start + static_cast<std::size_t>(width) * 4U; ++at) {
            if (pixels[at] != value) return false;
        }
    }
    return true;
}

std::array<std::uint8_t, 4> pixel_of(const PartRaster& raster, std::uint32_t column, std::uint32_t row) {
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const std::size_t at = static_cast<std::size_t>(row) * raster.row_bytes() + static_cast<std::size_t>(column) * 4U;
    const std::array<std::uint8_t, 4> pixel{pixels[at], pixels[at + 1U], pixels[at + 2U], pixels[at + 3U]};
    return pixel;
}

std::uint32_t device_side(double logical, double scale) {
    const std::uint32_t side = static_cast<std::uint32_t>(std::floor(logical * scale + 0.5));
    return side;
}

// -- the byte reference ------------------------------------------------------------

struct ReferenceCase final {
    const char* name{};
    FinishKind kind{};
    double scale{};
    std::int32_t seed{};
    std::array<std::uint8_t, 3> paint{};
    double grain{};
    std::uint64_t hash{};
};

// The cases of finish_reference.mjs, in its order: a 48 x 32 logical
// surface, the light at 125 and 38 degrees, ambient 0.32.
constexpr std::array<ReferenceCase, 26> reference_cases{{
    {"dusty_matte_1", FinishKind::dusty_matte, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x1f9b6a6093755688ULL},
    {"dusty_matte_2", FinishKind::dusty_matte, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xc7aa33d691b0efadULL},
    {"dusty_matte_3", FinishKind::dusty_matte, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x79fe7ce13318abe1ULL},
    {"orange_peel_1", FinishKind::orange_peel, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xfadad77c77ec7f5eULL},
    {"orange_peel_2", FinishKind::orange_peel, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x5664632dd69e23e5ULL},
    {"orange_peel_3", FinishKind::orange_peel, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x22d7db476d23ba85ULL},
    {"hammertone_1", FinishKind::hammertone, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x80877a5c9154d951ULL},
    {"hammertone_2", FinishKind::hammertone, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xf8e6f0aba07205b1ULL},
    {"hammertone_3", FinishKind::hammertone, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x56566ba62c110c91ULL},
    {"wrinkle_1", FinishKind::wrinkle, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x253fac053ddee400ULL},
    {"wrinkle_2", FinishKind::wrinkle, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xb2710c19557e7063ULL},
    {"wrinkle_3", FinishKind::wrinkle, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x9c395a7720b0e2deULL},
    {"bead_blast_1", FinishKind::bead_blast, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x8088451a7ab5a21bULL},
    {"bead_blast_2", FinishKind::bead_blast, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x38956df0466b4479ULL},
    {"bead_blast_3", FinishKind::bead_blast, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x970bad5c1cc3549fULL},
    {"sand_cast_1", FinishKind::sand_cast, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xe369e24bfded62aeULL},
    {"sand_cast_2", FinishKind::sand_cast, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xf97904b79f2d1693ULL},
    {"sand_cast_3", FinishKind::sand_cast, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xac6132e41e4ad7a7ULL},
    {"flat_baseline_1", FinishKind::flat, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xc3e903f670eabb25ULL},
    {"flat_baseline_2", FinishKind::flat, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xcdaa18ad0d448325ULL},
    {"flat_baseline_3", FinishKind::flat, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x39013c0adb2f7b25ULL},
    {"cloth_weave_1", FinishKind::cloth_weave, 1.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xf591ee66b116fb5dULL},
    {"cloth_weave_2", FinishKind::cloth_weave, 2.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0xa692070379fba22dULL},
    {"cloth_weave_3", FinishKind::cloth_weave, 3.0, 7, {0x2f, 0x6e, 0x6c}, 1.0, 0x9ffe0e946ada1294ULL},
    {"orange_peel_grain_2", FinishKind::orange_peel, 2.0, 7, {0x2f, 0x6e, 0x6c}, 2.0, 0xefa6ad3c98b88ea4ULL},
    {"hammertone_seed_11", FinishKind::hammertone, 2.0, 11, {0x4d, 0x67, 0x6d}, 1.0, 0x619a31d3707c933aULL},
}};

void reference_bytes() {
    const bool printing = print_mode();
    bool all_match = true;
    for (const ReferenceCase& reference : reference_cases) {
        FinishFill fill = study_fill(reference.kind);
        fill.seed = reference.seed;
        fill.grain = reference.grain;
        fill.paint = Color::rgba(reference.paint[0], reference.paint[1], reference.paint[2]);
        const std::uint32_t width = device_side(48.0, reference.scale);
        const std::uint32_t height = device_side(32.0, reference.scale);
        const std::shared_ptr<PartRaster> raster = whole_finish(fill, reference.scale, width, height);
        const std::uint64_t hash = raster_hash(*raster);
        if (printing) {
            std::cout << reference.name << " 0x" << std::hex << hash << std::dec << "ULL\n";
            continue;
        }
        if (hash != reference.hash) {
            std::cerr << "gui_forms_engine_finish_tests: " << reference.name << " hashes 0x" << std::hex << hash
                      << ", the reference 0x" << reference.hash << std::dec << '\n';
            all_match = false;
        }
    }
    require(all_match, "the kernel's bytes equal the JavaScript reference's");
}

// -- damage invariance -------------------------------------------------------------

constexpr std::array<std::uint32_t, 7> uneven_steps{1U, 3U, 2U, 7U, 5U, 11U, 4U};

constexpr std::uint8_t sentinel = 0xcdU;

void bands_match(const FinishFill& fill, double scale, const PartRaster& whole, bool rows) {
    const std::uint32_t width = whole.extent().width;
    const std::uint32_t height = whole.extent().height;
    const FinishRect surface{0, 0, static_cast<std::int64_t>(width), static_cast<std::int64_t>(height)};
    std::shared_ptr<PartRaster> banded = make_raster(width, height);
    fill_bytes(*banded, sentinel);
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, width);
    const std::uint32_t length = rows ? height : width;
    std::uint32_t at = 0U;
    std::size_t step = 0U;
    while (at < length) {
        const std::uint32_t band = std::min(uneven_steps[step % uneven_steps.size()], length - at);
        FinishRect damage{0, static_cast<std::int64_t>(at), static_cast<std::int64_t>(width),
                          static_cast<std::int64_t>(band)};
        if (!rows) damage = FinishRect{static_cast<std::int64_t>(at), 0, static_cast<std::int64_t>(band),
                                       static_cast<std::int64_t>(height)};
        const std::size_t painted = paint_into(fill, scale, surface, damage, *banded, scratch);
        require(painted == static_cast<std::size_t>(band) * (rows ? width : height), "a band paints its pixels");
        at = at + band;
        step = step + 1U;
    }
    require(same_area(*banded, 0U, 0U, whole, 0U, 0U, width, height),
            rows ? "uneven bands of rows give the whole paint's bytes"
                 : "uneven bands of columns give the whole paint's bytes");
}

// Uneven tiles (the steps across and a shifted run of them down), painted
// from the last to the first.
void tiles_match(const FinishFill& fill, double scale, const PartRaster& whole) {
    const std::uint32_t width = whole.extent().width;
    const std::uint32_t height = whole.extent().height;
    const FinishRect surface{0, 0, static_cast<std::int64_t>(width), static_cast<std::int64_t>(height)};
    std::vector<FinishRect> tiles{};
    std::uint32_t top = 0U;
    std::size_t down = 2U;
    while (top < height) {
        const std::uint32_t tall = std::min(uneven_steps[down % uneven_steps.size()] * 2U, height - top);
        std::uint32_t left = 0U;
        std::size_t across = down;
        while (left < width) {
            const std::uint32_t wide = std::min(uneven_steps[across % uneven_steps.size()] * 3U, width - left);
            tiles.push_back(FinishRect{static_cast<std::int64_t>(left), static_cast<std::int64_t>(top),
                                       static_cast<std::int64_t>(wide), static_cast<std::int64_t>(tall)});
            left = left + wide;
            across = across + 1U;
        }
        top = top + tall;
        down = down + 1U;
    }
    std::shared_ptr<PartRaster> tiled = make_raster(width, height);
    fill_bytes(*tiled, sentinel);
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, width);
    for (std::size_t index = tiles.size(); index > 0U; --index) {
        static_cast<void>(paint_into(fill, scale, surface, tiles[index - 1U], *tiled, scratch));
    }
    require(same_area(*tiled, 0U, 0U, whole, 0U, 0U, width, height), "uneven tiles give the whole paint's bytes");
}

// The surface 5 right and 3 down in a larger destination, then 7 left and
// 4 above a smaller one: anchored to its own origin both times, and
// nothing outside it written.
void offsets_match(const FinishFill& fill, double scale, const PartRaster& whole) {
    const std::uint32_t width = whole.extent().width;
    const std::uint32_t height = whole.extent().height;
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, width + 13U);

    std::shared_ptr<PartRaster> larger = make_raster(width + 13U, height + 9U);
    fill_bytes(*larger, sentinel);
    const FinishRect inset{5, 3, static_cast<std::int64_t>(width), static_cast<std::int64_t>(height)};
    const FinishRect everything{0, 0, static_cast<std::int64_t>(width) + 13, static_cast<std::int64_t>(height) + 9};
    static_cast<void>(paint_into(fill, scale, inset, everything, *larger, scratch));
    require(same_area(*larger, 5U, 3U, whole, 0U, 0U, width, height), "an offset surface is anchored to its origin");
    require(area_is(*larger, 0U, 0U, width + 13U, 3U, sentinel) && area_is(*larger, 0U, 3U, 5U, height, sentinel) &&
                area_is(*larger, 5U + width, 3U, 8U, height, sentinel) &&
                area_is(*larger, 0U, 3U + height, width + 13U, 6U, sentinel),
            "nothing outside the surface is written");

    std::shared_ptr<PartRaster> smaller = make_raster(width - 7U, height - 4U);
    fill_bytes(*smaller, sentinel);
    const FinishRect before{-7, -4, static_cast<std::int64_t>(width), static_cast<std::int64_t>(height)};
    const FinishRect visible{0, 0, static_cast<std::int64_t>(width) - 7, static_cast<std::int64_t>(height) - 4};
    static_cast<void>(paint_into(fill, scale, before, visible, *smaller, scratch));
    require(same_area(*smaller, 0U, 0U, whole, 7U, 4U, width - 7U, height - 4U),
            "a surface beginning outside the destination is anchored to its origin");
}

void damage_invariance() {
    constexpr std::array<double, 4> scales{1.0, 1.5, 2.0, 3.0};
    for (const FinishKind kind : finish_kinds) {
        for (const double scale : scales) {
            FinishFill fill = study_fill(kind);
            fill.seed = 23;
            const std::uint32_t width = device_side(48.0, scale);
            const std::uint32_t height = device_side(32.0, scale);
            const std::shared_ptr<PartRaster> whole = whole_finish(fill, scale, width, height);
            bands_match(fill, scale, *whole, true);
            bands_match(fill, scale, *whole, false);
            tiles_match(fill, scale, *whole);
            offsets_match(fill, scale, *whole);
        }
    }
}

// Damage inside the surface writes only the damage; damage beside it
// writes nothing; too few scratch samples write nothing.
void writes_only_the_damage() {
    const FinishFill fill = study_fill(FinishKind::hammertone);
    const FinishRect surface{0, 0, 64, 40};
    std::shared_ptr<PartRaster> raster = make_raster(64U, 40U);
    fill_bytes(*raster, sentinel);
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, 64U);
    const std::size_t painted = paint_into(fill, 2.0, surface, FinishRect{10, 6, 20, 9}, *raster, scratch);
    require(painted == 180U, "the damage's pixels are painted");
    require(area_is(*raster, 0U, 0U, 64U, 6U, sentinel) && area_is(*raster, 0U, 15U, 64U, 25U, sentinel) &&
                area_is(*raster, 0U, 6U, 10U, 9U, sentinel) && area_is(*raster, 30U, 6U, 34U, 9U, sentinel),
            "nothing outside the damage is written");
    require(pixel_of(*raster, 10U, 6U)[3] == 255U && pixel_of(*raster, 29U, 14U)[3] == 255U,
            "every pixel written is opaque");

    std::shared_ptr<PartRaster> beside = make_raster(64U, 40U);
    fill_bytes(*beside, sentinel);
    const std::size_t outside =
        paint_into(fill, 2.0, FinishRect{0, 0, 20, 20}, FinishRect{30, 30, 10, 10}, *beside, scratch);
    require(outside == 0U && area_is(*beside, 0U, 0U, 64U, 40U, sentinel), "damage beside the surface paints nothing");

    FinishScratch narrow{};
    engine_primitives::reserve_finish_scratch(narrow, 4U);
    require(narrow.upper.size() == 5U && narrow.lower.size() == 5U, "the scratch holds the width plus one");
    std::shared_ptr<PartRaster> refused = make_raster(64U, 40U);
    fill_bytes(*refused, sentinel);
    const std::size_t short_painted = paint_into(fill, 2.0, surface, FinishRect{0, 0, 10, 10}, *refused, narrow);
    require(short_painted == 0U && area_is(*refused, 0U, 0U, 64U, 40U, sentinel),
            "too few scratch samples paint nothing");
}

// -- parameters --------------------------------------------------------------------

void parameters_change_bytes() {
    const FinishFill base = study_fill(FinishKind::hammertone);
    const std::uint64_t reference = raster_hash(*whole_finish(base, 2.0, 96U, 64U));
    require(raster_hash(*whole_finish(base, 2.0, 96U, 64U)) == reference, "equal parameters give equal bytes");
    FinishFill seeded = base;
    seeded.seed = 8;
    require(raster_hash(*whole_finish(seeded, 2.0, 96U, 64U)) != reference, "the seed changes the bytes");
    FinishFill coarser = base;
    coarser.grain = 2.0;
    require(raster_hash(*whole_finish(coarser, 2.0, 96U, 64U)) != reference, "the grain changes the bytes");
    FinishFill lit = base;
    lit.light_azimuth_degrees = 300;
    require(raster_hash(*whole_finish(lit, 2.0, 96U, 64U)) != reference, "the light changes the bytes");

    // The flat finish is one colour.
    const std::shared_ptr<PartRaster> flat = whole_finish(study_fill(FinishKind::flat), 2.0, 40U, 24U);
    const std::array<std::uint8_t, 4> first = pixel_of(*flat, 0U, 0U);
    bool uniform = true;
    for (std::uint32_t row = 0U; row < 24U; ++row) {
        for (std::uint32_t column = 0U; column < 40U; ++column) {
            if (!(pixel_of(*flat, column, row) == first)) uniform = false;
        }
    }
    require(uniform && first[3] == 255U, "the flat finish is one opaque colour");
}

// A red warp and a blue weft: both threads show, each over a good share
// of the cloth.
void cloth_shows_warp_and_weft() {
    FinishFill cloth = study_fill(FinishKind::cloth_weave);
    cloth.paint = Color::rgba(0xc0, 0x10, 0x10);
    cloth.secondary = Color::rgba(0x10, 0x10, 0xc0);
    const std::shared_ptr<PartRaster> woven = whole_finish(cloth, 2.0, 96U, 64U);
    std::size_t warp = 0U;
    std::size_t weft = 0U;
    for (std::uint32_t row = 0U; row < 64U; ++row) {
        for (std::uint32_t column = 0U; column < 96U; ++column) {
            // BGRA: red is byte 2, blue byte 0.
            const std::array<std::uint8_t, 4> pixel = pixel_of(*woven, column, row);
            if (pixel[2] > pixel[0]) warp = warp + 1U;
            if (pixel[0] > pixel[2]) weft = weft + 1U;
        }
    }
    const std::size_t tenth = 96U * 64U / 10U;
    require(warp > tenth && weft > tenth, "the warp's and the weft's colours both show");
}

// -- through the engine ------------------------------------------------------------

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
        rasters = rasters + 1U;
    }
    std::shared_ptr<const PartRaster> last{};
    std::size_t rasters{};
};

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, size.width, size.height};
        part.key = PartKey{"face", {}, {}};
        return part;
    }
    Size size{40.0, 24.0};
};

ThemeRule material_rule() {
    ThemeRule rule{};
    rule.label = "pad";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = "pad";
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    return rule;
}

// The professional theme with every button state's material one finish.
// The definition is held on the heap (some 105 KB, theme_types.hpp).
std::shared_ptr<const Theme> finish_theme(const FinishFill& fill) {
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    definition.id = "engine-finish-test";
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::finish_fill(fill));
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& recipe : button.ordinary) recipe.material = material;
    definition.rules.rules.push_back(material_rule());
    const std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
    ManualTime time{};
    RasterWatch painter{};
};

std::unique_ptr<Fixture> make_fixture(const FinishFill& fill, Size size, double scale) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.pad = make_control<Pad>(StableId("pad"));
    (*made.pad).size = size;
    (*made.pad).set_requested_bounds(Rect{0.0, 0.0, size.width + 60.0, size.height + 60.0});
    (*made.root).add_child(made.pad);
    made.window = std::make_unique<Window>(made.root, Size{size.width + 100.0, size.height + 100.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_scale(scale);
    window.set_theme(finish_theme(fill));
    require(window.theme().engine().rules().diagnostics().empty(), "the finish theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

// The part raster's box starts 8 x scale pixels in; the finish is painted
// in the paint that bakes the part (no idle frame), anchored to the box.
void material_shows_the_finish() {
    constexpr std::array<double, 2> scales{1.0, 2.0};
    for (const double scale : scales) {
        const FinishFill fill = study_fill(FinishKind::orange_peel);
        std::unique_ptr<Fixture> fixture = make_fixture(fill, Size{40.0, 24.0}, scale);
        const Fixture& made = *fixture;
        require(made.painter.last != nullptr, "the finished part composites a raster");
        const std::uint32_t inset = device_side(8.0, scale);
        const std::uint32_t width = device_side(40.0, scale);
        const std::uint32_t height = device_side(24.0, scale);
        const std::shared_ptr<PartRaster> expected = whole_finish(fill, scale, width, height);
        require(same_area(*made.painter.last, inset, inset, *expected, 0U, 0U, width, height),
                "the face shows the finish pixel for pixel, anchored to its box");
        require((*made.window).metrics_snapshot().engine_primitive_refusals == 0U, "the finish is not refused");
    }
}

// The bake budget: a 40 x 24 hammertone is accepted, a 2000 x 2000 one at
// scale 4 (sixty-four million pixels) refused.
void bake_budget_refuses_large_finishes() {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find("material");
    require(primitive != nullptr, "the built-in registry has the material primitive");
    const PrimitiveDescriptor& descriptor = (*primitive).descriptor();
    PrimitiveParameterBlock parameters = default_parameters(descriptor);
    require(set_choice_parameter(parameters, descriptor, "recipe", "button") == ParameterAssignment::assigned,
            "a recipe role is a choice");
    SurfaceMaterial finished{};
    finished.fills.clear();
    finished.fills.push_back(MaterialFillLayer::finish_fill(study_fill(FinishKind::hammertone)));
    PrimitiveTarget target{};
    target.logical_size = Size{40.0, 24.0};
    target.scale = DeviceScale{1.0};
    target.material = &finished;
    require((*primitive).accepts(parameters, target), "a small finish is within the budget");
    target.logical_size = Size{2000.0, 2000.0};
    target.scale = DeviceScale{4.0};
    require(!(*primitive).accepts(parameters, target), "a finish past the budget is refused");
    require(engine_primitives::finish_effort_weight(FinishKind::hammertone) >
                engine_primitives::finish_effort_weight(FinishKind::flat),
            "the hammertone weighs more than the flat finish");
}

// -- the theme language ------------------------------------------------------------

bool sheet_compiles_cleanly(std::string_view sheet) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemePackage package = theme_sheet_package(sheet, "finish-sheet");
    const ThemeCompileResult result = compile_theme_package(package, library, images, options);
    const bool clean = result.succeeded() && result.diagnostics().empty();
    return clean;
}

void theme_language_finish() {
    require(sheet_compiles_cleanly("button::part(face) { background-image: finish(hammertone, #4d676d, #bdb6a8) }\n"),
            "finish() with a name and two colours compiles");
    require(sheet_compiles_cleanly(
                "button::part(face) { background-image: finish(cloth-weave, #8a6f4a, #c8b98f, 11, 2) }\n"),
            "finish() with a seed and a grain compiles");
    require(!sheet_compiles_cleanly("button::part(face) { background-image: finish(glitter, #4d676d, #bdb6a8) }\n"),
            "an unknown finish is refused");
    require(!sheet_compiles_cleanly("button::part(face) { background-image: finish(wrinkle, #4d676d) }\n"),
            "a finish needs two colours");
    require(!sheet_compiles_cleanly(
                "button::part(face) { background-image: finish(wrinkle, #4d676d, #bdb6a8, 1.5) }\n"),
            "a seed is a whole number");
    require(!sheet_compiles_cleanly(
                "button::part(face) { background-image: finish(wrinkle, #4d676d, #bdb6a8, 3, 5) }\n"),
            "a grain past 4 is refused");
}

// -- measuring and previewing ------------------------------------------------------

void measure() {
    const double pixels = 256.0 * 256.0;
    std::shared_ptr<PartRaster> raster = make_raster(256U, 256U);
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, 256U);
    const FinishRect whole{0, 0, 256, 256};
    for (const FinishKind kind : finish_kinds) {
        const FinishFill fill = study_fill(kind);
        std::array<double, 9> samples{};
        for (double& sample : samples) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            static_cast<void>(paint_into(fill, 1.0, whole, whole, *raster, scratch));
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            sample = std::chrono::duration<double, std::nano>(end - start).count();
        }
        std::sort(samples.begin(), samples.end());
        std::cout << kind_name(kind) << ' ' << std::fixed << std::setprecision(2) << samples[4] / pixels
                  << " ns per pixel (weight now " << engine_primitives::finish_effort_weight(kind) << ")\n";
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

int preview(const std::filesystem::path& directory) {
    std::error_code made{};
    std::filesystem::create_directories(directory, made);
    if (made) {
        std::cerr << "gui_forms_engine_finish_tests: cannot make " << directory << '\n';
        return 1;
    }
    constexpr std::array<double, 2> scales{1.0, 2.0};
    for (const FinishKind kind : finish_kinds) {
        for (const double scale : scales) {
            const std::shared_ptr<PartRaster> raster =
                whole_finish(study_fill(kind), scale, device_side(96.0, scale), device_side(64.0, scale));
            const std::string name = std::string("finish-") + kind_name(kind) + "-" +
                                     std::to_string(static_cast<int>(scale)) + "x.png";
            const std::filesystem::path path = directory / name;
            if (!write_png(*raster, scale, path)) {
                std::cerr << "gui_forms_engine_finish_tests: cannot write " << path << '\n';
                return 1;
            }
            std::cout << path.string() << '\n';
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
    reference_bytes();
    if (print_mode()) return 0;
    damage_invariance();
    writes_only_the_damage();
    parameters_change_bytes();
    cloth_shows_warp_and_weft();
    material_shows_the_finish();
    bake_budget_refuses_large_finishes();
    theme_language_finish();
    std::cout << "gui_forms_engine_finish_tests passed\n";
    return 0;
}
