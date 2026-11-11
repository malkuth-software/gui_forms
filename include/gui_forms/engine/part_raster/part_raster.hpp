#pragma once

#include "gui_forms/engine/types/engine_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// One part's pixels as the engine keeps them (T2 plan, PartRaster):
// premultiplied BGRA8 in device pixels, in PartRasterView's layout, made for
// one display scale. Rows are tight: row_bytes is width * 4.
//
// Lifetime: create() makes a raster with every pixel transparent; its maker
// fills it through view() and then shares it read-only, as
// std::shared_ptr<const PartRaster>, with every recorded command that
// composites it (Painter::draw_part_raster). Recorded commands keep it
// alive until their display chunk is rebuilt or retired.
class PartRaster final {
public:
    // A transparent raster of `extent` for display scale `scale`, or null
    // when either side exceeds maximum_part_raster_side or the scale is not
    // a finite value in [minimum_device_scale, maximum_device_scale]. An
    // empty extent is allowed and has no pixels.
    [[nodiscard]] static std::shared_ptr<PartRaster> create(DeviceExtent extent, DeviceScale scale);

    PartRaster(const PartRaster&) = delete;
    PartRaster& operator=(const PartRaster&) = delete;

    // The raster as a destination for a primitive: valid while the raster
    // lives and is not shared read-only. Clears the recorded opacity, since
    // the pixels may change through the view.
    [[nodiscard]] PartRasterView view() noexcept;

    // Records whether every pixel's alpha is 255. The maker calls it after
    // its last write through view(), before sharing the raster; the
    // composite then copies the raster's rows rather than blending them.
    void record_opacity() noexcept;
    // Whether record_opacity found every pixel opaque since the last view().
    [[nodiscard]] bool opaque() const noexcept { return opaque_; }

    [[nodiscard]] std::span<const std::uint8_t> pixels() const& noexcept { return pixels_; }
    std::span<const std::uint8_t> pixels() const&& = delete;
    [[nodiscard]] DeviceExtent extent() const noexcept { return extent_; }
    [[nodiscard]] std::size_t row_bytes() const noexcept { return row_bytes_; }
    [[nodiscard]] DeviceScale scale() const noexcept { return scale_; }

private:
    PartRaster(DeviceExtent extent, DeviceScale scale);

    std::vector<std::uint8_t> pixels_;
    DeviceExtent extent_{};
    std::size_t row_bytes_{};
    DeviceScale scale_{};
    bool opaque_{};
};

} // namespace gui_forms
