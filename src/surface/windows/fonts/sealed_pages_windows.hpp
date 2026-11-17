#pragma once

#include "../../../core/resources/fonts/bundled_font_set.hpp"

namespace gui_forms::host {

// The font set's page capability on Windows: committed VirtualAlloc pages, made read-only with
// VirtualProtect. A write to sealed pages raises an access violation.
[[nodiscard]] gui_forms::detail::SealedPageFunctions windows_sealed_pages() noexcept;

} // namespace gui_forms::host
