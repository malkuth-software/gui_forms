#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

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

struct ThemeSourceRecord final {
    // The closure's sheets as one text (compile_closure's combined text),
    // and the name its diagnostics give it.
    std::string text{};
    std::string file{};
    std::vector<std::string> published_values{};
    std::vector<ThemeSourceImage> images{};
    // The control types the theme's selectors could name. Each anatomy's
    // spans refer to static storage (ControlAnatomy), so the copies stay
    // valid.
    std::vector<ControlAnatomy> controls{};
    EngineVersion requires_engine{};
};

} // namespace gui_forms::detail
