#pragma once

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
// theme; `default_theme()` changes to this one at phase 6, not before.

// The floor: Plan Paint's action plate for push buttons, the gold active
// tool for selected ones, recessed wells for entry fields and choices,
// near-white stock (#F4F4F5) for windows and panels, flat command members
// at rest, and text colours that meet the contrast gate (4.5:1, 3:1 for
// disabled text) on every face. Id `gui-forms`; its rules are empty.
[[nodiscard]] ThemeDefinition gui_forms_theme_definition();

// The light sheet's source text (`gui-forms/light.css` in diagnostics):
// the colour roles of design language §5.4 and the rules for parts the
// engine paints. Static storage.
[[nodiscard]] std::string_view gui_forms_light_sheet() noexcept;

// The built theme and what building it measured. When the sheet is
// refused, `theme` is the floor alone and `diagnostics` says why; the
// stock sheet compiles with no diagnostics (tests/gui_forms_theme_tests).
struct GuiFormsThemeBuild final {
    std::shared_ptr<const Theme> theme{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    // The compiler's work for the sheet (ThemeCompileWork::operations), a
    // count independent of the machine.
    std::uint64_t compile_operations{};
    // Wall time from the floor's construction to the theme, on the first
    // call's thread: the startup cost decision T4-3 bounds.
    std::chrono::nanoseconds build_time{};
};

// The theme, built on the first call (thread-safe; later calls return the
// same build) and kept for the life of the program.
[[nodiscard]] const GuiFormsThemeBuild& gui_forms_theme_build();
[[nodiscard]] std::shared_ptr<const Theme> gui_forms_theme();

} // namespace gui_forms
