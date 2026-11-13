// OffscreenRaster::create: finds the bundled faces, reads them into the
// process's shared set and hands it to the renderer's half
// (src/render/skia/offscreen/offscreen_raster.cpp). Compiled into the
// application library, where the host says where its faces are, and into
// tests, which name the directory (without a host, an empty directory is
// fonts_not_found).

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/metrics/font_sources/bundled_font_sources.hpp"

#include "../../render/skia/offscreen/offscreen_raster_parts.hpp"

#if defined(_WIN32)
#include "../../host/windows/fonts/sealed_pages_windows.hpp"
#else
#include "../../host/posix/fonts/sealed_pages_posix.hpp"
#endif

#if defined(GUI_FORMS_APPLICATION_MACOS) || defined(GUI_FORMS_APPLICATION_WINDOWS) || \
    defined(GUI_FORMS_APPLICATION_LINUX)
#define GUI_FORMS_OFFSCREEN_HOST_FONTS 1
#include "../../host/offscreen_font_lookup.hpp"
#endif

#include <memory>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

detail::SealedPageFunctions platform_sealed_pages() noexcept {
#if defined(_WIN32)
    const detail::SealedPageFunctions pages = host::windows_sealed_pages();
#else
    const detail::SealedPageFunctions pages = host::posix_sealed_pages();
#endif
    return pages;
}

// The directory the options name, else the host's; empty when neither has
// one.
std::filesystem::path chosen_font_directory(const OffscreenRasterOptions& options) {
    std::filesystem::path directory = options.font_directory;
#if defined(GUI_FORMS_OFFSCREEN_HOST_FONTS)
    if (directory.empty()) directory = host::offscreen_bundled_font_directory();
#endif
    return directory;
}

} // namespace

OffscreenRasterCreateResult OffscreenRaster::create(const OffscreenRasterOptions& options) {
    const std::filesystem::path directory = chosen_font_directory(options);
    if (directory.empty()) {
        std::string detail = "no bundled font directory";
        const std::string refusal = bundled_font_refusal();
        if (!refusal.empty()) detail += ": " + refusal;
        OffscreenRasterCreateResult result =
            OffscreenRasterCreateResult::refused(OffscreenRasterError::fonts_not_found, std::move(detail));
        return result;
    }
    if (!detail::directory_holds_required_faces(directory)) {
        OffscreenRasterCreateResult result = OffscreenRasterCreateResult::refused(
            OffscreenRasterError::fonts_not_found, "the required faces are not all in " + directory.string());
        return result;
    }
    const detail::SealedPageFunctions pages = platform_sealed_pages();
    std::shared_ptr<detail::BundledFontSet> fonts = detail::shared_bundled_font_set(directory, pages);
    OffscreenRasterCreateResult result = detail::OffscreenRasterAccess::make(std::move(fonts), directory);
    return result;
}

} // namespace gui_forms
