#pragma once

// The bundled fonts every AppKit window and the splashscreen render with
// (macos_bundled_fonts.mm).

#include "macos_host_private.hpp"

namespace gui_forms::host::macos {

// The process's one bundled font set, shared by every view and held by the
// application run: each face is decoded (or read) once, checked and sealed
// read-only. Null only when the embedded pack does not open (and no
// development directory holds the required faces).
std::shared_ptr<gui_forms::detail::BundledFontSet> bundle_font_set();

} // namespace gui_forms::host::macos

using gui_forms::host::macos::bundle_font_set;
