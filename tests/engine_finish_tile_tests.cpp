// World finish tiles (cp31, the node canvas's finish texture): a surface
// finish evaluated at world device pixels, painted tile by tile, joins with
// no seam. For four finishes, at two scales:
// - two horizontally adjacent tiles, two vertically adjacent ones and four
//   around the world's origin (negative world pixels included) are each
//   byte for byte the region of one bake across all of them, so their
//   boundary pixels (the last column or row of one tile and the first of
//   the next) are the single bake's;
// - the tiles are not all alike (the finish varies across the world);
// - the window's tile cache (EnginePartTable::finish_tile) queues a tile
//   it does not hold, paints it in generate_finish_tiles, notes the part
//   that waited, and lends the same bytes; a tile outside the world's
//   reach, or at a scale that does not draw, is never queued.

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/finish_fill/finish_fill.hpp"

#include "../src/core/engine/primitives/finish/finish_kernel.hpp"
#include "../src/core/engine/primitives/finish/finish_world.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

namespace {
using namespace gui_forms;
using engine_primitives::FinishScratch;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_finish_tile_tests: " << message << '\n';
    std::exit(1);
}

constexpr std::uint32_t side = finish_tile_side;

constexpr std::array<FinishKind, 4> tiled_kinds{FinishKind::orange_peel, FinishKind::cloth_weave,
                                                FinishKind::hammertone, FinishKind::dusty_matte};

constexpr std::array<double, 2> scales{1.0, 2.5};

FinishFill fill_of(FinishKind kind) {
    FinishFill fill{};
    fill.kind = kind;
    fill.seed = 11;
    fill.paint = Color::rgba(255, 255, 255);
    fill.secondary = Color::rgba(200, 200, 200);
    return fill;
}

// A world bake of `width` by `height` device pixels whose pixel (0, 0) is
// world pixel (`origin_x`, `origin_y`): its own buffer, every pixel written.
struct WorldBake final {
    std::vector<std::uint8_t> pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
};

WorldBake bake(const FinishFill& fill, double scale, std::int64_t origin_x, std::int64_t origin_y,
               std::uint32_t width, std::uint32_t height) {
    WorldBake made{};
    made.width = width;
    made.height = height;
    const std::size_t row_bytes = static_cast<std::size_t>(width) * part_raster_bytes_per_pixel;
    made.pixels.assign(row_bytes * height, std::uint8_t{0});
    const PartRasterView view{std::span<std::uint8_t>(made.pixels), DeviceExtent{width, height}, row_bytes};
    FinishScratch scratch{};
    engine_primitives::reserve_finish_scratch(scratch, width);
    const std::size_t written = engine_primitives::paint_world_finish(fill, scale, origin_x, origin_y, view, scratch);
    require(written == static_cast<std::size_t>(width) * height, "a world bake writes every pixel");
    return made;
}

// Whether the tile `part` equals the region of `whole` whose corner is
// (`x`, `y`): every byte.
bool same_region(const WorldBake& whole, std::uint32_t x, std::uint32_t y, std::span<const std::uint8_t> part,
                 std::size_t part_row_bytes, std::uint32_t width, std::uint32_t height) {
    const std::size_t whole_row_bytes = static_cast<std::size_t>(whole.width) * part_raster_bytes_per_pixel;
    const std::size_t span_bytes = static_cast<std::size_t>(width) * part_raster_bytes_per_pixel;
    for (std::uint32_t row = 0U; row < height; ++row) {
        const std::size_t whole_at = (static_cast<std::size_t>(y) + row) * whole_row_bytes +
                                     static_cast<std::size_t>(x) * part_raster_bytes_per_pixel;
        const std::size_t part_at = static_cast<std::size_t>(row) * part_row_bytes;
        for (std::size_t byte = 0U; byte < span_bytes; ++byte) {
            if (whole.pixels[whole_at + byte] != part[part_at + byte]) return false;
        }
    }
    return true;
}

bool same_tile(const WorldBake& whole, std::uint32_t x, std::uint32_t y, const WorldBake& tile) {
    const std::size_t row_bytes = static_cast<std::size_t>(tile.width) * part_raster_bytes_per_pixel;
    const bool same = same_region(whole, x, y, std::span<const std::uint8_t>(tile.pixels), row_bytes, tile.width,
                                  tile.height);
    return same;
}

// Column `column_a` of `bake_a` against column `column_b` of `bake_b`, over
// the rows both have: the boundary check, stated on its own.
bool same_column(const WorldBake& bake_a, std::uint32_t column_a, const WorldBake& bake_b, std::uint32_t column_b) {
    const std::size_t a_row = static_cast<std::size_t>(bake_a.width) * part_raster_bytes_per_pixel;
    const std::size_t b_row = static_cast<std::size_t>(bake_b.width) * part_raster_bytes_per_pixel;
    for (std::uint32_t row = 0U; row < bake_a.height && row < bake_b.height; ++row) {
        for (std::size_t channel = 0U; channel < part_raster_bytes_per_pixel; ++channel) {
            const std::size_t a_at = row * a_row + column_a * part_raster_bytes_per_pixel + channel;
            const std::size_t b_at = row * b_row + column_b * part_raster_bytes_per_pixel + channel;
            if (bake_a.pixels[a_at] != bake_b.pixels[b_at]) return false;
        }
    }
    return true;
}

void adjacent_tiles_join() {
    const std::int64_t tile = static_cast<std::int64_t>(side);
    for (const FinishKind kind : tiled_kinds) {
        const FinishFill fill = fill_of(kind);
        for (const double scale : scales) {
            // Across: tiles (0, 0) and (1, 0) against one bake of both.
            const WorldBake left = bake(fill, scale, 0, 0, side, side);
            const WorldBake right = bake(fill, scale, tile, 0, side, side);
            const WorldBake across = bake(fill, scale, 0, 0, 2U * side, side);
            require(same_column(left, side - 1U, across, side - 1U) && same_column(right, 0U, across, side),
                    "the boundary columns of two adjacent tiles are the single bake's");
            require(same_tile(across, 0U, 0U, left) && same_tile(across, side, 0U, right),
                    "each of two adjacent tiles is its region of the single bake");
            require(!(left.pixels == right.pixels), "the finish varies across the world");
            // Down: tiles (0, 0) and (0, 1).
            const WorldBake lower = bake(fill, scale, 0, tile, side, side);
            const WorldBake down = bake(fill, scale, 0, 0, side, 2U * side);
            require(same_tile(down, 0U, 0U, left) && same_tile(down, 0U, side, lower),
                    "two tiles one above the other are their regions of the single bake");
            // Around the origin: tiles (-1, -1) to (0, 0), negative world
            // pixels included.
            const WorldBake around = bake(fill, scale, -tile, -tile, 2U * side, 2U * side);
            const WorldBake north_west = bake(fill, scale, -tile, -tile, side, side);
            const WorldBake north_east = bake(fill, scale, 0, -tile, side, side);
            const WorldBake south_west = bake(fill, scale, -tile, 0, side, side);
            require(same_tile(around, 0U, 0U, north_west) && same_tile(around, side, 0U, north_east) &&
                        same_tile(around, 0U, side, south_west) && same_tile(around, side, side, left),
                    "four tiles around the world's origin are their regions of the single bake");
        }
    }
}

void cached_tiles_join() {
    EnginePartTable table{};
    const PartRecordHandle waiter{3U, 1U};
    FinishTileKey first{};
    first.finish = fill_of(FinishKind::orange_peel);
    first.display_scale = 2.0;
    first.scale = 3.0;
    first.column = -1;
    first.row = 4;
    FinishTileKey second = first;
    second.column = 0;
    require(table.finish_tile(first, waiter, 1U) == nullptr && table.finish_tile(second, waiter, 1U) == nullptr,
            "a tile not held is not lent");
    require(table.finish_tiles_waiting() && table.cache_statistics().finish_tiles_queued == 2U,
            "both tiles are queued, once each");
    require(table.finish_tile(first, waiter, 1U) == nullptr && table.cache_statistics().finish_tiles_queued == 2U,
            "a tile queued already is not queued again");
    FinishTileKey far = first;
    far.column = engine_primitives::finish_world_reach / static_cast<std::int64_t>(side);
    FinishTileKey unscaled = first;
    unscaled.scale = 0.0;
    require(table.finish_tile(far, waiter, 1U) == nullptr && table.finish_tile(unscaled, waiter, 1U) == nullptr &&
                table.cache_statistics().finish_tiles_queued == 2U,
            "a tile beyond the world's reach, or at no scale, is never queued");
    bool drained = false;
    for (int frame = 0; frame < 4 && table.finish_tiles_waiting(); ++frame) {
        drained = table.generate_finish_tiles(2U);
    }
    require(drained && !table.finish_tiles_waiting() && table.cache_statistics().finish_tiles == 2U,
            "idle frames paint the queue and report it drained");
    std::vector<PartRecordHandle> waiters{};
    table.take_finish_tile_waiters(waiters);
    require(waiters.size() == 1U && waiters[0] == waiter, "the part that waited is to be repainted, once");
    const std::shared_ptr<const PartRaster> west = table.finish_tile(first, waiter, 3U);
    const std::shared_ptr<const PartRaster> east = table.finish_tile(second, waiter, 3U);
    require(west != nullptr && east != nullptr && table.cache_statistics().finish_tile_hits == 2U,
            "the painted tiles are lent");
    if (west == nullptr || east == nullptr) return;
    require((*west).extent().width == side && (*west).extent().height == side,
            "a tile is finish_tile_side pixels square");
    // World pixels: column -1 starts at -side, row 4 at 4 * side.
    const std::int64_t tile = static_cast<std::int64_t>(side);
    const WorldBake across = bake(first.finish, first.scale, -tile, 4 * tile, 2U * side, side);
    const std::span<const std::uint8_t> west_pixels = (*west).pixels();
    const std::span<const std::uint8_t> east_pixels = (*east).pixels();
    require(same_region(across, 0U, 0U, west_pixels, (*west).row_bytes(), side, side) &&
                same_region(across, side, 0U, east_pixels, (*east).row_bytes(), side, side),
            "cached adjacent tiles are their regions of one bake across both: no seam at their boundary");
    FinishTileKey rescaled = first;
    rescaled.scale = 6.0;
    require(table.finish_tile(rescaled, waiter, 3U) == nullptr, "a tile at another scale is another tile");
}

} // namespace

int main() {
    adjacent_tiles_join();
    cached_tiles_join();
    std::cout << "gui_forms_engine_finish_tile_tests passed\n";
    return 0;
}
