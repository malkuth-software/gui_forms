#pragma once

#include "backdrop_image.hpp"

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/types.hpp"
#include "gui_forms/window/backdrop/backdrop_chain.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// One window's (or popup's) cached backdrop (owner, 2026-11-05): the chain
// applied to what lies under the window's bounds, held as one part raster
// over those bounds, valid tile by tile. Tiles are computed only when an
// exposed rectangle needs them, and invalidated only by damage under them
// (grown by the chain's margin) or by a change of the chain, the bounds or
// the scale. Owned by the window's popup attachment; UI thread.
namespace gui_forms::detail {

// A strip of cache tiles to compute: device pixels of the cache raster.
struct BackdropStrip final {
    std::uint32_t left{};
    std::uint32_t top{};
    std::uint32_t width{};
    std::uint32_t height{};
};

class BackdropCache final {
public:
    // The side of a tile, in device pixels.
    static constexpr std::uint32_t tile_side = 64U;

    // Keeps what is cached when `chain`, `bounds` (window logical units)
    // and `scale` are those it holds; otherwise drops it, so that every tile
    // is computed again. False when no raster of the bounds can be made.
    [[nodiscard]] bool prepare(const BackdropChain& chain, Rect bounds, double scale);

    // Marks every tile under `damage` (window logical units), grown by the
    // chain's margin, for computation; returns how many were valid.
    std::uint64_t invalidate(Rect damage) noexcept;
    void invalidate_all() noexcept;

    // The strips of tiles under `exposed` (window logical units) that are
    // not valid, each a run of tiles along one tile row, appended to
    // `strips` (cleared first).
    void missing_strips(Rect exposed, std::vector<BackdropStrip>& strips) const;

    // Marks the tiles of `strip` valid.
    void mark_valid(const BackdropStrip& strip) noexcept;

    // The raster to write a strip into: copied first when a recorded
    // command still shares the current one, so a shared raster never
    // changes. Null before prepare succeeded.
    [[nodiscard]] PartRaster* writable_raster();
    // The raster to record a draw of.
    [[nodiscard]] const std::shared_ptr<PartRaster>& raster() const noexcept { return raster_; }

    [[nodiscard]] Rect bounds() const noexcept { return bounds_; }
    [[nodiscard]] double scale() const noexcept { return scale_; }
    [[nodiscard]] std::uint32_t margin() const noexcept { return margin_; }
    [[nodiscard]] const BackdropChain& chain() const noexcept { return chain_; }
    [[nodiscard]] BackdropWork& work() noexcept { return work_; }

private:
    // The tile columns and rows under a device rectangle of the raster,
    // clamped to it: [first, last) along each axis.
    struct TileSpan final {
        std::uint32_t first_column{};
        std::uint32_t last_column{};
        std::uint32_t first_row{};
        std::uint32_t last_row{};
    };
    [[nodiscard]] TileSpan tiles_under(Rect window_rect, double grown_device) const noexcept;

    BackdropChain chain_{};
    Rect bounds_{};
    double scale_{};
    std::uint32_t margin_{};
    std::shared_ptr<PartRaster> raster_{};
    std::uint32_t tile_columns_{};
    std::uint32_t tile_rows_{};
    // One flag per tile, row-major: 1 when its pixels are current.
    std::vector<std::uint8_t> valid_{};
    BackdropWork work_{};
};

} // namespace gui_forms::detail
