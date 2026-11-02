#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::detail {

// What compiling a built-in theme's sheet gave: the theme (null when the
// sheet was refused), every diagnostic in the order found, and the
// compiler's work (ThemeCompileWork::operations) for the sheet.
struct BuiltInThemeCompilation final {
    std::shared_ptr<const Theme> theme{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::uint64_t operations{};
};

// Compiles `sheet`, named `file` in its diagnostics, at the program
// theme's levels onto `floor` (its recipes, structure and other fields),
// against the control types `controls`, and makes the theme, which keeps
// the sheet as its source for inline styles (T3 P12b) as a compiled
// package's does. `floor` is read, never changed; a refused sheet makes no
// theme. The built-in theme `gui-forms` is made this way (stage T4,
// decision T4-3: its sheets compiled at first use).
[[nodiscard]] BuiltInThemeCompilation compile_built_in_theme(const ThemeDefinition& floor, std::string_view file,
                                                             std::string_view sheet,
                                                             std::span<const ControlAnatomy> controls);

} // namespace gui_forms::detail
