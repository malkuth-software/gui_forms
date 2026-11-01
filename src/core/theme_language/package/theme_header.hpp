#pragma once

// A user theme's `@theme` header (T3 user themes, notes:planning/stages/
// T3-theme-language/USER_THEMES.md section 2): the descriptors of the
// theme's implied manifest, written in CSS descriptor syntax as the sheet's
// first item.
//
//   @theme { engine: ">=0.3"; author: "..."; provenance: "..."; version: "1.0.0"; }

#include "../diagnostics/theme_diagnostics.hpp"
#include "../source_text/theme_source_text.hpp"
#include "../syntax_tree/theme_syntax_tree.hpp"
#include "../tokenizer/theme_tokenizer.hpp"
#include "gui_forms/engine/types/engine_types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace gui_forms::theme_language {

struct ThemeHeader final {
    // Whether the sheet has the header; a CSS file in `themes/` without it
    // is not a theme.
    bool present{};
    std::string version{};
    // The engine bound, as a manifest's `engine` (">=0.3"); none where the
    // header names none.
    std::optional<EngineVersion> engine{};
    std::string author{};
    std::string provenance{};
};

enum class ThemeHeaderOutcome : std::uint8_t { read, refused };

// Reads the header of `tree`, parsed from `source` into `tokens`, into
// `header` (reset first). Each descriptor's value is one string, the
// engine's a bound as parse_engine_bound reads it; a value of another form
// is value_syntax (E401) and refuses. An unknown descriptor is a warning
// (unknown_property, W101) and is dropped; a repeated one takes the later
// value. Diagnostics are appended to `diagnostics`.
[[nodiscard]] ThemeHeaderOutcome read_theme_header(const ThemeSource& source, const ThemeTokenBuffer& tokens,
                                                   const LineStartTable& lines, const ThemeSyntaxTree& tree,
                                                   ThemeHeader& header, ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::theme_language
