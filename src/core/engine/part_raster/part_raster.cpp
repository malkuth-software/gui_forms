#include "gui_forms/engine/part_raster/part_raster.hpp"

#include <cmath>

namespace gui_forms {

PartRaster::PartRaster(const DeviceExtent extent, const DeviceScale scale)
    : extent_(extent), row_bytes_(static_cast<std::size_t>(extent.width) * part_raster_bytes_per_pixel),
      scale_(scale) {
    // Both sides are at most maximum_part_raster_side, so the product stays
    // far inside std::size_t (engine_types.hpp).
    const std::size_t size = row_bytes_ * static_cast<std::size_t>(extent.height);
    pixels_.assign(size, std::uint8_t{0});
}

std::shared_ptr<PartRaster> PartRaster::create(const DeviceExtent extent, const DeviceScale scale) {
    if (extent.width > maximum_part_raster_side || extent.height > maximum_part_raster_side) return nullptr;
    if (!std::isfinite(scale.value) || scale.value < minimum_device_scale || scale.value > maximum_device_scale) {
        return nullptr;
    }
    // The constructor is private, so make_shared cannot reach it.
    std::shared_ptr<PartRaster> raster(new PartRaster(extent, scale));
    return raster;
}

PartRasterView PartRaster::view() noexcept {
    opaque_ = false;
    const PartRasterView borrowed{std::span<std::uint8_t>(pixels_.data(), pixels_.size()), extent_, row_bytes_};
    return borrowed;
}

void PartRaster::record_opacity() noexcept {
    // The AND of every alpha byte (the fourth of each BGRA pixel); an empty
    // raster has no pixel that is not opaque, but nothing to copy either.
    std::uint8_t every = 255U;
    for (std::size_t offset = 3U; offset < pixels_.size(); offset += part_raster_bytes_per_pixel) {
        every = static_cast<std::uint8_t>(every & pixels_[offset]);
    }
    opaque_ = !pixels_.empty() && every == 255U;
}

} // namespace gui_forms
