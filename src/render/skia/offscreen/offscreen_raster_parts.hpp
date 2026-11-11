#pragma once

// OffscreenRaster's private state and its maker, shared by the renderer's
// half (offscreen_raster.cpp, in the Skia library) and the font lookup's
// half (src/runtime/offscreen/offscreen_raster_create.cpp, in the
// application library, which knows the host's font places).

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"

#include "../raster/skia_raster.hpp"

#include <filesystem>
#include <memory>

namespace gui_forms {

// Declared after the font set it registered, so the raster ends first.
struct OffscreenRaster::Parts final {
    std::shared_ptr<detail::BundledFontSet> fonts{};
    std::filesystem::path directory{};
    render::SkiaRaster raster{};
};

namespace detail {

struct OffscreenRasterAccess final {
    // Makes the raster over `fonts` (read from `directory`): fonts_refused
    // when the raster refuses the set, raster_refused when it cannot make
    // its first surface. UI thread.
    [[nodiscard]] static OffscreenRasterCreateResult make(std::shared_ptr<BundledFontSet> fonts,
                                                          std::filesystem::path directory);
};

} // namespace detail
} // namespace gui_forms
