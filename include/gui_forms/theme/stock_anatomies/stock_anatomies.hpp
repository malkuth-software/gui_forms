#pragma once

#include "gui_forms/anatomy/types/anatomy_types.hpp"

#include <span>

namespace gui_forms {

// The anatomies of the stock controls (stage T4, phase 3): every control
// type of the control schema (`manifests/control-schema.json`), in schema
// order, generated into the core by `tools/stock_anatomies.py` so that a
// sheet the core compiles may name stock control types, parts and states
// without the controls library. Static storage: the spans stay valid for
// the life of the program (ControlAnatomy). An application compiling a
// theme package passes these, and its own controls' anatomies, as
// ThemeCompileOptions::controls.
[[nodiscard]] std::span<const ControlAnatomy> stock_control_anatomies() noexcept;

} // namespace gui_forms
