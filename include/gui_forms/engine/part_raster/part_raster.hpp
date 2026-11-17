#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/engine/types/engine_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms {

// A rectangle of a part raster's pixels, [left, right) by [top, bottom);
// empty when right <= left or bottom <= top.
struct GUI_FORMS_API PartRasterInterior final {
    std::uint32_t left{};
    std::uint32_t top{};
    std::uint32_t right{};
    std::uint32_t bottom{};
};

// One part's pixels as the engine keeps them (T2 plan, PartRaster):
// premultiplied BGRA8 in device pixels, in PartRasterView's layout, made for
// one display scale. Rows are tight: row_bytes is width * 4.
//
// Lifetime: create() makes a raster with every pixel transparent; its maker
// fills it through view() and then shares it read-only, as
// std::shared_ptr<const PartRaster>, with every recorded command that
// composites it (Painter::draw_part_raster). Recorded commands keep it
// alive until their display chunk is rebuilt or retired.
class GUI_FORMS_API PartRaster final {
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
    // the pixels may change through the view, and the opaque interior.
    [[nodiscard]] PartRasterView view() noexcept;

    // Records whether every pixel's alpha is 255. The maker calls it after
    // its last write through view(), before sharing the raster; the
    // composite then copies the raster's rows rather than blending them.
    void record_opacity() noexcept;
    // Whether record_opacity found every pixel opaque since the last view().
    [[nodiscard]] bool opaque() const noexcept { return opaque_; }
    // Records `interior` as the raster's opaque interior when it lies inside
    // the raster and every pixel in it has alpha 255 (checked here, one pass
    // that stops at the first row that is not), else records none. The maker
    // calls it after its last write through view(), with the rectangle its
    // geometry promises (a material's box less its corners); the composite
    // then copies those pixels' rows at full opacity instead of blending
    // them, which writes the same bytes (blend study, 2026-11-16).
    void record_opaque_interior(PartRasterInterior interior) noexcept;
    // What record_opaque_interior recorded since the last view(), or empty.
    [[nodiscard]] PartRasterInterior opaque_interior() const noexcept { return opaque_interior_; }

    // Borrowed from the raster: the sanitizer tree reports a use after the
    // raster died (the last shared owner let it go).
    [[nodiscard]] BorrowedSpan<std::uint8_t> pixels() const& noexcept { return detail::lend_view(pixels_); }
    BorrowedSpan<std::uint8_t> pixels() const&& = delete;
    [[nodiscard]] DeviceExtent extent() const noexcept { return extent_; }
    [[nodiscard]] std::size_t row_bytes() const noexcept { return row_bytes_; }
    [[nodiscard]] DeviceScale scale() const noexcept { return scale_; }

private:
    PartRaster(DeviceExtent extent, DeviceScale scale);

    detail::LendingVector<std::uint8_t> pixels_;
    DeviceExtent extent_{};
    std::size_t row_bytes_{};
    DeviceScale scale_{};
    bool opaque_{};
    PartRasterInterior opaque_interior_{};
};

} // namespace gui_forms
