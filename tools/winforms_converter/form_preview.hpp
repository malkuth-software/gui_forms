#pragma once

// The `winforms.preview` tool (stage MCP G7): a form document loaded
// headlessly and captured as a PNG, to set beside a screenshot of the
// Windows Forms original. Needs GUIForms::Application (the CPU raster), so
// it is apart from converter_tools.hpp, which needs only the controls.
//
// The capture shows structure and layout. Bindings are left out (each
// `<bind>` element is blanked, keeping line numbers, since a preview has
// no model), and the theme is the stock windows-professional one: the
// converted theme sheet is not applied.

#include "converter_tools.hpp"

#include "../../src/core/form_language/form_model/form_model.hpp"

#include <string>

namespace gui_forms::winforms_converter {

struct PreviewRequest final {
    std::string file{};
    std::string document{};
    // Where the PNG is written.
    std::string output_png{};
    // A screenshot of the original form, reported beside the capture.
    std::string original_png{};
    // Device pixels per logical unit, in (0, 8].
    double scale{1.0};
};

// Loads, captures and writes; the reply gives both pictures' paths and
// pixel sizes, and the load's diagnostics on a refusal. UI thread; makes
// its own application when the program has none.
[[nodiscard]] ConverterToolReply run_preview(const PreviewRequest& request, const form_language::FormSchema& schema);

// `document` with each `<bind .../>` element blanked to spaces (line
// breaks kept).
[[nodiscard]] std::string without_bindings(std::string_view document);

} // namespace gui_forms::winforms_converter
