#pragma once
// The Windows host's installed-font probe, for the GUI.Drawing raster's
// families the substitution table does not map (ADR-004 phase 7): the
// Windows counterpart of the CoreText probe
// (src/render/skia/executor/core_text_font_bytes.hpp, defined in src/surface/macos/fonts/). Win32 types stay in
// this directory; the raster sees UTF-8 names and bytes only.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::host {

// What the probe found: the face's file bytes (empty when the host has no
// face of that name, or it is larger than the caller's limit) and the name
// the host selected, for the raster's font trace ("<family> (registry)"
// when the bytes came from the registry's file).
struct GdiFontBytes final {
    std::vector<std::byte> bytes;
    std::string selected;
};

// GDI's face for `family` (UTF-8) at `style` (bit 0 bold, bit 1 italic),
// accepted only when GetTextFaceW names the requested family, since GDI
// otherwise selects an arbitrary fallback; then the file the registry lists
// under the family's own name. Neither is accepted above `maximum_bytes`.
// Any thread; it makes and releases its own device context.
[[nodiscard]] GdiFontBytes gdi_font_bytes(std::string_view family, std::uint32_t style, std::size_t maximum_bytes);

} // namespace gui_forms::host
