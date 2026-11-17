#include "drawing_platform.hpp"

namespace gui_drawing::abi::platform {

// No native surfaces here, so nothing to stage.
struct SurfaceStaging final {};

void SurfaceStagingRelease::operator()(SurfaceStaging* staging) const noexcept {
    delete staging;
}

gd_result export_hbitmap(Bitmap&, Color, std::uintptr_t&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result import_hbitmap(std::uintptr_t, std::unique_ptr<Bitmap>&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result capture_surface(std::uintptr_t, std::uint32_t, CapturedSurface&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result refresh_surface(std::uintptr_t, std::uint32_t, Bitmap&, SurfaceStagingOwner&, RectF&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result publish_retained_surface(std::uintptr_t, std::uint32_t, Bitmap&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result present_surface(std::uintptr_t, std::uint32_t, Bitmap&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result acquire_hdc(Bitmap&, std::uintptr_t&, std::uint64_t&) {
    return GD_ERROR_NOT_AVAILABLE;
}

gd_result release_hdc(Bitmap&, std::uint64_t) {
    return GD_ERROR_NOT_AVAILABLE;
}

bool has_hdc_lease(Bitmap&) { return false; }

} // namespace gui_drawing::abi::platform
