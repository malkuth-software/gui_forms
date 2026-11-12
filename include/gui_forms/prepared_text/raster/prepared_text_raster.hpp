#pragma once
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/prepared_text/layout/prepared_text_layout.hpp"

namespace gui_forms {
// Synchronous raster only: consumes immutable prepared device glyph positions,
// does not shape. Caller executor owns all native FT resources for this call.
// Checks authority/profile before work and before replacement. Failure preserves
// output. Empty/space-only success has 0x0 pixels and retains logical metrics.
// Immutable layout may be used on a separate raster executor with exclusive
// access to output. Publication holds the authority lock: cancellation/desire
// linearizes either before commit (stale) or after a successful commit. Success
// does not promise that authority remains current after the call returns.
[[nodiscard]] GUI_FORMS_API PreparedTextStatus rasterize_prepared_text(const PreparedTextLayout& layout,
    LayoutAuthority expected, GrayTextMask& output);
} // namespace gui_forms
