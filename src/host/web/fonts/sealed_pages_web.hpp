#pragma once

#include "../../../core/resources/fonts/bundled_font_set.hpp"

namespace gui_forms::host {

// The font set's page capability in a browser. WebAssembly's linear memory
// has no page protection, so the faces cannot be made read-only: `seal`
// accepts and changes nothing, and a stray write to a face's bytes is not
// caught as it is on the desktop hosts (POSIX mprotect, Windows
// VirtualProtect). The pages are 64 KiB-aligned zeroed heap blocks, the
// wasm page size, so a later protection scheme (STXlib's platform layer)
// can replace these three functions without moving the faces.
[[nodiscard]] gui_forms::detail::SealedPageFunctions web_sealed_pages() noexcept;

} // namespace gui_forms::host
