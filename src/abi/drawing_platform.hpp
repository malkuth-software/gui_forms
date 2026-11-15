#ifndef GUI_FORMS_DRAWING_PLATFORM_HPP
#define GUI_FORMS_DRAWING_PLATFORM_HPP

#include "gui_forms/drawing.hpp"
#include "gui_forms/drawing_c_api.h"

#include <cstdint>
#include <memory>

namespace gui_drawing::abi::platform {

struct CapturedSurface final {
    std::unique_ptr<Bitmap> bitmap;
    RectF bounds;
};

// What a native surface refresh copies through (on Windows a memory device
// context and its DIB section; nothing elsewhere): one per bitmap, held by
// the bitmap's handle record, made on the bitmap's first copying refresh
// and kept at its size. Destroyed once, with the handle record, on
// whichever thread lets the handle go last; never per thread, as a
// thread's end on Windows runs its releases under the loader lock, where
// GDI may not be called. Used only under the record's operation lock.
struct SurfaceStaging;
struct SurfaceStagingRelease final {
    void operator()(SurfaceStaging* staging) const noexcept;
};
using SurfaceStagingOwner = std::unique_ptr<SurfaceStaging, SurfaceStagingRelease>;

gd_result export_hbitmap(Bitmap& bitmap, Color background, std::uintptr_t& output);
gd_result import_hbitmap(std::uintptr_t source, std::unique_ptr<Bitmap>& output);
gd_result capture_surface(std::uintptr_t source, std::uint32_t kind,
                          CapturedSurface& output);
// Copies the surface into bitmap again, through staging (made here when
// empty; a failure leaves what was made, for the next refresh).
gd_result refresh_surface(std::uintptr_t source, std::uint32_t kind,
                          Bitmap& bitmap, SurfaceStagingOwner& staging, RectF& bounds);
gd_result publish_retained_surface(std::uintptr_t destination,
                                   std::uint32_t kind, Bitmap& bitmap);
gd_result present_surface(std::uintptr_t destination, std::uint32_t kind,
                          Bitmap& bitmap);
gd_result acquire_hdc(Bitmap& bitmap, std::uintptr_t& output,
                      std::uint64_t& lease_token);
gd_result release_hdc(Bitmap& bitmap, std::uint64_t lease_token);
bool has_hdc_lease(Bitmap& bitmap);

} // namespace gui_drawing::abi::platform

#endif
