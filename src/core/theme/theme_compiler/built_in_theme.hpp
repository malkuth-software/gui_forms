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
// against the control types `controls` (with the node canvas's anatomy
// where they lack it; cp31), and makes the theme, which keeps
// the sheet as its source for inline styles (T3 P12b) as a compiled
// package's does. `floor` is read, never changed; a refused sheet makes no
// theme. The built-in theme `gui-forms` is made this way (stage T4,
// decision T4-3: its sheets compiled at first use).
[[nodiscard]] BuiltInThemeCompilation compile_built_in_theme(const ThemeDefinition& floor, std::string_view file,
                                                             std::string_view sheet,
                                                             std::span<const ControlAnatomy> controls);

// What evaluating a floor sheet found (evaluate_floor_sheet): every
// diagnostic in the order found, the compiler's work, and whether the
// sheet compiled and so replaced the families' recipes.
struct FloorSheetEvaluation final {
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::uint64_t operations{};
    bool evaluated{};
};

// Compiles floor sheet `sheet`, named `file` in its diagnostics, onto
// `definition` (read for the compile) against the stock control types and,
// when it compiles, replaces every family's recipes in `definition` with
// the sheet's, evaluated over `structure` (fill_family_recipes; T4 floor
// sheet, owner option 1). A refused sheet leaves `definition` unchanged.
// The built-in themes `gui-forms` and the Windows-familiar ones make their
// floors this way.
[[nodiscard]] FloorSheetEvaluation evaluate_floor_sheet(std::string_view file, std::string_view sheet,
                                                        const ControlVisualRecipe& structure,
                                                        ThemeDefinition& definition);

} // namespace gui_forms::detail
