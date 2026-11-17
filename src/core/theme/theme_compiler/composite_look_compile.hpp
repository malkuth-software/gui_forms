#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Internal: a composite control's look lowered with the theme in effect
// (cp34 phase 3), for a look that reads the theme's application roles or
// custom properties (detail::CompositeLook keeps one per theme serial).

namespace gui_forms::detail {

// Compiles the look `text` (named `file` in diagnostics) with the text of
// `theme`, the theme in effect: the theme's sheets, then the look as sheet
// rules after them, against the theme's control types and `controls` (the
// stock anatomies and the composite's own; borrowed for the call). The
// result (CompiledThemeRules::compile_look with the theme's rules) holds
// the look's rules and copies of the theme's definitions of the custom
// properties they read. The look's findings, placed in `text`, and any
// error are appended to `diagnostics`; the theme's own warnings are not
// repeated. Null where an error refuses the look under this theme.
[[nodiscard]] std::shared_ptr<const CompiledThemeRules> compile_theme_look(
    std::string_view text, std::string_view file, std::span<const ControlAnatomy> controls, const Theme& theme,
    std::vector<ThemeCompileDiagnostic>& diagnostics);

} // namespace gui_forms::detail
