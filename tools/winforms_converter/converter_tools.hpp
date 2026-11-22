#pragma once

// The converter's model tools (stage MCP G7): the operations a model drives
// a conversion with, described and run independently of any transport.
// Track M2's dispatcher binds each spec to its handler; the request and
// reply records are what it decodes from and encodes to JSON.
//
//   winforms.list_forms  the forms of a project                  read-only
//   winforms.convert     one form: document, theme, C# binding    writes files only when asked
//   winforms.validate    a form document's compiler diagnostics   read-only
//   winforms.diff        two form documents, structurally         read-only
//   winforms.preview     a headless capture (form_preview.hpp)    writes one PNG
//
// The loop they serve: convert, validate (convert already validates what
// it wrote), edit the document to fix what validate reports, diff against
// the converter's output to review the edit, preview beside the original.

#include "conversion_diagnostics.hpp"
#include "winforms_conversion.hpp"

#include "../../src/core/form_language/form_model/form_model.hpp"
#include "../../src/core/theme_language/diagnostics/theme_diagnostics.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms::winforms_converter {

struct ConverterToolSpec final {
    std::string_view name{};
    std::string_view title{};
    std::string_view description{};
    // A JSON Schema object for the tool's arguments.
    std::string_view input_schema{};
    bool read_only{};
    bool destructive{};
};

// The five specs, in the order above.
[[nodiscard]] std::span<const ConverterToolSpec> converter_tool_specs() noexcept;

// What a tool returns: a short text for the model, and the same content
// as a JSON object (structured content).
struct ConverterToolReply final {
    bool succeeded{};
    std::string text{};
    std::string json{};
};

struct ListFormsRequest final {
    std::string project{};
};

struct ConvertRequest final {
    std::string project{};
    // The form's name (Form1) or its designer file's path.
    std::string form{};
    // When not empty, the three outputs are written there too.
    std::string output_directory{};
    ConversionOptions options{};
};

struct ValidateRequest final {
    // The name diagnostics cite.
    std::string file{};
    std::string document{};
};

struct DiffRequest final {
    std::string before{};
    std::string after{};
};

[[nodiscard]] ConverterToolReply run_list_forms(const ListFormsRequest& request);
[[nodiscard]] ConverterToolReply run_convert(const ConvertRequest& request, const form_language::FormSchema& schema);
[[nodiscard]] ConverterToolReply run_validate(const ValidateRequest& request,
                                              const form_language::FormSchema& schema);
[[nodiscard]] ConverterToolReply run_diff(const DiffRequest& request);

// read_form_model's diagnostics of `document` against `schema`, into
// `diagnostics`; true when the model was built.
[[nodiscard]] bool validate_form_document(std::string_view file, std::string_view document,
                                          const form_language::FormSchema& schema,
                                          theme_language::ThemeDiagnosticList& diagnostics);

// One line per diagnostic in the form compiler's spelling:
// `main.form:4:7: error: E812 form_generation_refused: ...`.
[[nodiscard]] std::string theme_diagnostic_line(const theme_language::ThemeDiagnostic& record);

// `text` as a JSON string literal, quotes included.
[[nodiscard]] std::string json_string(std::string_view text);

} // namespace gui_forms::winforms_converter
