#pragma once

// The host's bundled font directory for OffscreenRaster::create
// (src/runtime/offscreen/offscreen_raster_create.cpp): the directory the
// host's own windows read their faces from, found the same way (each host
// documents its places). Empty when none holds the required faces; the
// lookup then records bundled_font_refusal. Defined by the macOS, Windows
// and Linux hosts; UI thread.

#include <filesystem>

namespace gui_forms::host {

[[nodiscard]] std::filesystem::path offscreen_bundled_font_directory();

} // namespace gui_forms::host
