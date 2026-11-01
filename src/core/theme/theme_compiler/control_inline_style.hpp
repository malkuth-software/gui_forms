#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Internal: a control's compiled inline style (T3 P9, R2-10), which the
// control owns and the cascade reads through PartFacts::inline_rules.

namespace gui_forms {
class Control;
} // namespace gui_forms

namespace gui_forms::detail {

struct ControlInlineStyle final {
    // The text as set.
    std::string text{};
    // Its rules, at the inline levels (CompiledThemeRules::compile_inline).
    CompiledThemeRules rules{};
    // Whether it defines custom properties, and so was compiled against
    // the theme in effect for its control (rules.theme_serial()), to be
    // compiled again when that theme changes (P12b).
    bool defines{};
};

// Compiles `text` as the inline style of the control `stable_id` of
// `anatomy`, against `theme`, the theme in effect for the control (P12b:
// where the style defines custom properties, the theme's rules reading
// them are lowered again with its definitions). Its diagnostics name
// `file` (`inline:<control>`) or the theme's sheet, appended to
// `diagnostics`: the compiled style, or null where an error refuses it.
[[nodiscard]] std::shared_ptr<const ControlInlineStyle> compile_inline_style(
    std::string_view text, const ControlAnatomy& anatomy, std::string_view stable_id, std::string_view file,
    const Theme& theme, std::vector<ThemeCompileDiagnostic>& diagnostics);

// A control's inline style compiled again for another theme in effect
// (P12b), until installed. The control is borrowed: collection and
// installation run with no user code between.
struct InlineRestyle final {
    Control* control{};
    std::shared_ptr<const ControlInlineStyle> style{};
};

// The invalid_argument message refusing `change` (as `application theme`)
// because an inline style does not compile against the new theme: the
// first error of `diagnostics`.
[[nodiscard]] std::string inline_restyle_refusal(std::string_view change,
                                                 std::span<const ThemeCompileDiagnostic> diagnostics);

} // namespace gui_forms::detail
