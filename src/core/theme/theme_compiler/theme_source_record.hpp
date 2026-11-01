#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Internal: what a compiled theme package keeps of its source (T3 P12b),
// so that a control's inline style defining custom properties can be
// compiled against the theme: the theme's rules reading those properties
// are lowered again with the style's definitions.

namespace gui_forms::detail {

// One image resource of the theme, as its compilation read it. The image
// is the theme's own (its lease keeps it in the registry while the theme
// lives).
struct ThemeSourceImage final {
    std::string resource{};
    ImageId image{};
    Size pixel_size{};
};

// One sheet of a closure's combined text: the name its diagnostics give
// it, and where its bytes are in ThemeSourceRecord::text.
struct ThemeSourceSheet final {
    std::string file{};
    std::size_t start{};
    std::size_t length{};
};

// Every theme keeps one (owner, 2026-10-31: the default theme keeps its
// source like any other). A theme made from a definition alone, the
// built-in default among them, keeps the empty sheet's: its rules read no
// custom properties, so an inline style compiled against it reads its own
// definitions only. Once the default theme is written as sheets (T4),
// their text goes here as a package's does.
struct ThemeSourceRecord final {
    // The closure's sheets as one text (compile_closure's combined text),
    // and the name its diagnostics give it.
    std::string text{};
    std::string file{};
    // The closure's sheets in `text`, in order, to place the diagnostics of
    // a compilation of it in their own sheets; empty for one sheet.
    std::vector<ThemeSourceSheet> sheets{};
    // The custom property names the theme defines, sorted: an inline style
    // reading one it does not define reads the theme's (P12b).
    std::vector<std::string> defined_names{};
    std::vector<std::string> published_values{};
    std::vector<ThemeSourceImage> images{};
    // The control types the theme's selectors could name. Each anatomy's
    // spans refer to static storage (ControlAnatomy), so the copies stay
    // valid.
    std::vector<ControlAnatomy> controls{};
    EngineVersion requires_engine{};
};

// The record of a theme made from a definition alone: the empty sheet's.
[[nodiscard]] std::shared_ptr<const ThemeSourceRecord> empty_theme_source();

} // namespace gui_forms::detail
