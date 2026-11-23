#pragma once

// The specs of stage MCP G6's tools (study section 18), for track M2's
// dispatcher to bind: the `inspect` additions as values of its `what`
// argument, and the four `forms` tools over FormExportRegistry
// (src/core/form_language/form_export). Transport-agnostic: a name, a
// description, a JSON Schema for the arguments and the hints.

#include <array>
#include <string_view>

namespace gui_forms::model_inspection {

struct ModelToolSpec final {
    std::string_view name{};
    std::string_view description{};
    std::string_view input_schema{};
    bool read_only{};
    bool destructive{};
};

// The values `inspect` gains for `what` (model_inspection.hpp).
inline constexpr std::array<std::string_view, 3U> inspect_additions = {"summary", "layout", "resources"};

inline constexpr std::array<ModelToolSpec, 4U> forms_tool_specs = {{
    {"forms.export",
     "Returns the form document of a window as the program has it (the applied one after forms.apply).",
     R"({"type":"object","properties":{"window":{"type":"string"}},"required":["window"]})", true, false},
    {"forms.validate",
     "Checks a candidate form document against the program's registry and the sources it exposes to the window; "
     "returns the diagnostics.",
     R"({"type":"object","properties":{"window":{"type":"string"},"document":{"type":"string"}},"required":["window","document"]})",
     true, false},
    {"forms.apply",
     "Replaces a window's structure, or the control named by target, with a valid form document, keeping its "
     "bindings to the program's data and commands. A document that does not validate or load changes nothing.",
     R"({"type":"object","properties":{"window":{"type":"string"},"document":{"type":"string"},"target":{"type":"string"}},"required":["window","document"]})",
     false, true},
    {"forms.revert", "Puts a window back to the form it was built from.",
     R"({"type":"object","properties":{"window":{"type":"string"}},"required":["window"]})", false, true},
}};

} // namespace gui_forms::model_inspection
