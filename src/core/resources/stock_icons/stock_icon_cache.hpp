#pragma once
// The process's baked stock icons (stock_icons.hpp): coverage per icon
// drawing and device size, and coloured part rasters made from it per
// display scale and colour. One cache for the process, shared by every UI
// thread that paints (windows and sub-surfaces), guarded by one mutex; a
// bake runs under it, once per entry. Bounded: past its capacity the oldest
// entry goes, to be baked again if shown again.

#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/resources/stock_icons/stock_icons.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms::detail {

// `coverage` (side * side bytes) in `colour`, as a part raster for `scale`;
// null when the side, the coverage's size or the scale is out of range.
[[nodiscard]] std::shared_ptr<const PartRaster> coloured_stock_icon(std::span<const std::uint8_t> coverage,
                                                                    std::uint32_t side, DeviceScale scale,
                                                                    Color colour);

// The raster of `vectors` `side` pixels square for `scale` in `colour`:
// from the cache, else coloured from cached coverage, else rasterized.
// Null as coloured_stock_icon is, or when `vectors` is null.
[[nodiscard]] std::shared_ptr<const PartRaster> cached_stock_icon(const std::shared_ptr<const StockIconVectors>& vectors,
                                                                  std::uint32_t side, DeviceScale scale, Color colour);

// Bakes the coverage of the status icons (stock_icon_information to
// stock_icon_success, solid, on each authored grid, at that size) for
// `scale`, once per scale: Window calls it when it is made and when its
// scale changes, so the icons a window shows first are baked at launch.
void prebake_stock_icons(double scale);

} // namespace gui_forms::detail
