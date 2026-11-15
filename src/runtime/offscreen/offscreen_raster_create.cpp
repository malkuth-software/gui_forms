// OffscreenRaster::create: finds the bundled faces, takes the process's
// shared set of them and hands it to the renderer's half
// (src/render/skia/offscreen/offscreen_raster.cpp). Compiled into the
// application library, where the host says where its faces are (the
// embedded font pack, or a development build's directory), and into tests,
// which name a directory or, without a host, take the embedded pack through
// the core's own lookup.

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/metrics/font_sources/bundled_font_sources.hpp"

#include "../../render/skia/offscreen/offscreen_raster_parts.hpp"

#if defined(_WIN32)
#include "../../host/windows/fonts/sealed_pages_windows.hpp"
#elif defined(__EMSCRIPTEN__)
#include "../../host/web/fonts/sealed_pages_web.hpp"
#else
#include "../../host/posix/fonts/sealed_pages_posix.hpp"
#endif

#if defined(GUI_FORMS_APPLICATION_MACOS) || defined(GUI_FORMS_APPLICATION_WINDOWS) || \
    defined(GUI_FORMS_APPLICATION_LINUX) || defined(GUI_FORMS_APPLICATION_WEB)
#define GUI_FORMS_OFFSCREEN_HOST_FONTS 1
#include "../../host/offscreen_font_lookup.hpp"
#endif

#include <memory>
#include <span>
#include <string>
#include <utility>

namespace gui_forms {
namespace {

detail::SealedPageFunctions platform_sealed_pages() noexcept {
#if defined(_WIN32)
    const detail::SealedPageFunctions pages = host::windows_sealed_pages();
#elif defined(__EMSCRIPTEN__)
    const detail::SealedPageFunctions pages = host::web_sealed_pages();
#else
    const detail::SealedPageFunctions pages = host::posix_sealed_pages();
#endif
    return pages;
}

// The host's source, or without a host the core's own lookup, which is
// offered no directory and so answers the embedded pack.
detail::BundledFontLocation default_font_location() {
#if defined(GUI_FORMS_OFFSCREEN_HOST_FONTS)
    const detail::BundledFontLocation location = host::offscreen_bundled_font_location();
#else
    const std::span<const detail::BundledFontCandidate> no_candidates{};
    const detail::BundledFontLocation location = detail::locate_bundled_fonts(no_candidates);
#endif
    return location;
}

// A raster over the faces of the directory the options name.
OffscreenRasterCreateResult create_from_directory(const std::filesystem::path& directory) {
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

} // namespace

OffscreenRasterCreateResult OffscreenRaster::create(const OffscreenRasterOptions& options) {
    if (!options.font_directory.empty()) {
        OffscreenRasterCreateResult named = create_from_directory(options.font_directory);
        return named;
    }
    const detail::BundledFontLocation location = default_font_location();
    if (location.source == BundledFontSource::none) {
        std::string detail = "no bundled font source";
        const std::string refusal = bundled_font_refusal();
        if (!refusal.empty()) detail += ": " + refusal;
        OffscreenRasterCreateResult result =
            OffscreenRasterCreateResult::refused(OffscreenRasterError::fonts_not_found, std::move(detail));
        return result;
    }
    const detail::SealedPageFunctions pages = platform_sealed_pages();
    std::shared_ptr<detail::BundledFontSet> fonts = detail::shared_bundled_font_set(location, pages);
    OffscreenRasterCreateResult result = detail::OffscreenRasterAccess::make(std::move(fonts), location.directory);
    return result;
}

} // namespace gui_forms
