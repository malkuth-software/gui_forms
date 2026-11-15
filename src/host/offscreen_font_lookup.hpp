#pragma once

// The host's bundled font source for OffscreenRaster::create
// (src/runtime/offscreen/offscreen_raster_create.cpp): the source the host's
// own windows read their faces from, found the same way (each host documents
// its places): the embedded font pack, or a development build's directory.
// None only when the pack does not open; the lookup then records
// bundled_font_refusal. Defined by the macOS, Windows and Linux hosts; UI
// thread.

#include "../core/resources/fonts/bundled_font_set.hpp"

namespace gui_forms::host {

[[nodiscard]] gui_forms::detail::BundledFontLocation offscreen_bundled_font_location();

} // namespace gui_forms::host
