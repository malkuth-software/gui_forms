#pragma once

#include "../../../core/resources/fonts/bundled_font_set.hpp"

namespace gui_forms::host {

// The font set's page capability on macOS and Linux: anonymous private mmap pages, made
// read-only with mprotect. A write to sealed pages raises SIGSEGV or SIGBUS.
[[nodiscard]] gui_forms::detail::SealedPageFunctions posix_sealed_pages() noexcept;

} // namespace gui_forms::host
