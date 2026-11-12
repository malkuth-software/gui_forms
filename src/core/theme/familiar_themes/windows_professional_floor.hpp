#pragma once

#include "gui_forms/theme/theme/theme.hpp"

namespace gui_forms::detail {

// The stock `windows-professional` theme's floor (owner, 2026-11-10): the
// structural definition (windows_professional_theme_definition) with its
// floor sheet's light records and its dark sheet's authored dark records
// (windows_professional_sheets.hpp). Packages compiled onto
// ThemeBase::windows_professional start from it, with its parts sheet first
// in their cascade. Evaluated once, on the first call (thread-safe), and
// kept for the life of the program; read only.
[[nodiscard]] const ThemeDefinition& windows_professional_floor_definition();

} // namespace gui_forms::detail
