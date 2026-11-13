#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace gui_forms {

// The default theme `gui-forms` (stage T4, decisions T4-2 and T4-3): Plan
// Paint's light chrome (malkuth-software/plan-paint at 5e28a5e) as a floor
// of built-in recipes, which every control painting from its family's
// recipe draws, and a light sheet in the theme language, compiled onto the
// floor at first use, which the parts painted through the engine draw.
// `windows_professional_theme_definition()` stays as a Windows-familiar
// theme; `default_theme()` returns this one (phase 6, owner decision
// 2026-11-08).

// The floor: Plan Paint's action plate for push buttons, the gold active
// tool for selected ones, recessed wells for entry fields and choices,
// near-white stock (#F4F4F5) for windows and panels, flat command members
// at rest, and enabled text colours that meet the contrast gate (4.5:1)
// on every face (disabled text is exempt). Its recipes are the floor sheet's
// (gui_forms_floor_sheet), evaluated once on the first call (thread-safe)
// over the C++ structure (owner option 1) and kept for the process: the
// reference stays valid until exit (S3). Id `gui-forms`; its rules are
// empty.
[[nodiscard]] GUI_FORMS_API const ThemeDefinition& gui_forms_theme_definition();

// The floor sheet's source text (`gui-forms/floor.css` in diagnostics):
// every family's recipe in every surface state, as `:family()` rules.
// Static storage.
[[nodiscard]] GUI_FORMS_API std::string_view gui_forms_floor_sheet() noexcept;

// The light sheet's source text (`gui-forms/light.css` in diagnostics):
// the colour roles of design language §5.4 and the rules for parts the
// engine paints. Static storage.
[[nodiscard]] GUI_FORMS_API std::string_view gui_forms_light_sheet() noexcept;

// The built theme and what building it measured. `diagnostics` holds the
// floor sheet's, then the light sheet's. When the light sheet is refused,
// `theme` is the floor alone; when the floor sheet is, the floor keeps
// windows-professional's recipes. The stock sheets compile with no
// diagnostics (tests/gui_forms_theme_tests).
struct GUI_FORMS_API GuiFormsThemeBuild final {
    std::shared_ptr<const Theme> theme{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    // The compiler's work for the sheet (ThemeCompileWork::operations), a
    // count independent of the machine.
    std::uint64_t compile_operations{};
    // The same for the floor sheet.
    std::uint64_t floor_compile_operations{};
    // Wall time from the floor's construction to the theme, on the first
    // call's thread: the startup cost decision T4-3 bounds.
    std::chrono::nanoseconds build_time{};
};

// The theme, built on the first call (thread-safe; later calls return the
// same build) and kept for the life of the program.
[[nodiscard]] GUI_FORMS_API const GuiFormsThemeBuild& gui_forms_theme_build();
[[nodiscard]] GUI_FORMS_API std::shared_ptr<const Theme> gui_forms_theme();

} // namespace gui_forms
