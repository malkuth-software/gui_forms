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
    opaque_interior_ = PartRasterInterior{};
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

void PartRaster::record_opaque_interior(const PartRasterInterior interior) noexcept {
    opaque_interior_ = PartRasterInterior{};
    const bool inside = interior.left < interior.right && interior.top < interior.bottom &&
                        interior.right <= extent_.width && interior.bottom <= extent_.height;
    if (!inside) return;
    const std::size_t first = static_cast<std::size_t>(interior.left) * part_raster_bytes_per_pixel + 3U;
    const std::size_t end = static_cast<std::size_t>(interior.right) * part_raster_bytes_per_pixel;
    for (std::uint32_t y = interior.top; y < interior.bottom; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * row_bytes_;
        // The AND of the row's alpha bytes: no exit inside the row.
        std::uint8_t every = 255U;
        for (std::size_t offset = first; offset < end; offset += part_raster_bytes_per_pixel) {
            every = static_cast<std::uint8_t>(every & pixels_[row + offset]);
        }
        if (every != 255U) return;
    }
    opaque_interior_ = interior;
}

} // namespace gui_forms
