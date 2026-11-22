#include "converter_tools.hpp"

#include "form_document_diff.hpp"
#include "winforms_project.hpp"

#include "../../src/core/theme_language/source_text/theme_source_text.hpp"

#include <array>
#include <cstdio>
#include <utility>

namespace gui_forms::winforms_converter {

namespace {

constexpr std::array<ConverterToolSpec, 5U> specs = {{
    {"winforms.list_forms", "List a Windows Forms project's forms",
     "Lists the forms of a Windows Forms project (each *.Designer.cs, with its .resx when present).",
     R"({"type":"object","properties":{"project":{"type":"string","description":"The project directory or .csproj."}},"required":["project"]})",
     true, false},
    {"winforms.convert", "Convert a Windows Forms form",
     "Converts one form of a Windows Forms project into a GUI.Forms form document, a theme sheet and a C# binding, "
     "validates the document, and returns all three with the conversion's and the validation's diagnostics. "
     "Writes the files only when output_directory is given.",
     R"({"type":"object","properties":{"project":{"type":"string"},"form":{"type":"string","description":"Form1, or the designer file's path."},"output_directory":{"type":"string"},"form_id":{"type":"string"},"csharp_namespace":{"type":"string"}},"required":["project","form"]})",
     false, false},
    {"winforms.validate", "Validate a form document",
     "Reads a GUI.Forms form document as the form compiler does and returns its diagnostics (E8xx codes with lines).",
     R"({"type":"object","properties":{"file":{"type":"string"},"document":{"type":"string"}},"required":["document"]})",
     true, false},
    {"winforms.diff", "Compare two form documents",
     "Compares two GUI.Forms form documents by stable ID: objects added, removed, moved or retyped, and attributes "
     "and bindings changed.",
     R"({"type":"object","properties":{"before":{"type":"string"},"after":{"type":"string"}},"required":["before","after"]})",
     true, false},
    {"winforms.preview", "Preview a form document",
     "Loads a GUI.Forms form document headlessly (bindings left out), captures it as a PNG and reports it beside "
     "the original form's screenshot when one is given.",
     R"({"type":"object","properties":{"document":{"type":"string"},"file":{"type":"string"},"output_png":{"type":"string"},"original_png":{"type":"string"},"scale":{"type":"number"}},"required":["document","output_png"]})",
     false, false},
}};

void append_conversion_diagnostics(const ConversionDiagnosticList& diagnostics, std::string& text, std::string& json) {
    json.append("[");
    bool first = true;
    for (const ConversionDiagnostic& record : diagnostics.records()) {
        const std::string line = conversion_diagnostic_line(record);
        text.append(line);
        text.push_back('\n');
        if (!first) json.push_back(',');
        first = false;
        json.append("{\"code\":");
        json.append(json_string(conversion_code_label(record.code)));
        json.append(",\"name\":");
        json.append(json_string(conversion_code_name(record.code)));
        json.append(",\"severity\":");
        json.append(json_string(conversion_severity_name(conversion_severity(record.code))));
        json.append(",\"file\":");
        json.append(json_string(record.file));
        json.append(",\"line\":" + std::to_string(record.line) + ",\"column\":" + std::to_string(record.column));
        json.append(",\"subject\":");
        json.append(json_string(record.subject));
        json.append(",\"detail\":");
        json.append(json_string(record.detail));
        json.append("}");
    }
    json.append("]");
}

void append_theme_diagnostics(const theme_language::ThemeDiagnosticList& diagnostics, std::string& text,
                              std::string& json) {
    json.append("[");
    bool first = true;
    for (const theme_language::ThemeDiagnostic& record : diagnostics.records()) {
        text.append(theme_diagnostic_line(record));
        text.push_back('\n');
        if (!first) json.push_back(',');
        first = false;
        const bool error = theme_language::diagnostic_severity(record.code) == theme_language::DiagnosticSeverity::error;
        json.append("{\"code\":");
        json.append(json_string(theme_language::diagnostic_code_label(record.code)));
        json.append(",\"name\":");
        json.append(json_string(theme_language::diagnostic_code_name(record.code)));
        json.append(",\"severity\":");
        json.append(error ? "\"error\"" : "\"warning\"");
        json.append(",\"line\":" + std::to_string(record.position.line.value) +
                    ",\"column\":" + std::to_string(record.position.column.value));
        json.append(",\"rule\":");
        json.append(json_string(record.rule));
        json.append(",\"property\":");
        json.append(json_string(record.property));
        json.append(",\"detail\":");
        json.append(json_string(record.detail));
        json.append("}");
    }
    json.append("]");
}

[[nodiscard]] ConverterToolReply refused_reply(const ConversionDiagnosticList& diagnostics) {
    ConverterToolReply reply{};
    std::string listed{};
    append_conversion_diagnostics(diagnostics, reply.text, listed);
    reply.json = "{\"diagnostics\":" + listed + "}";
    return reply;
}

[[nodiscard]] std::string joined_path(std::string_view directory, std::string_view name) {
    std::string path(directory);
    if (!path.empty() && path.back() != '/') path.push_back('/');
    path.append(name);
    return path;
}

} // namespace

std::span<const ConverterToolSpec> converter_tool_specs() noexcept {
    const std::span<const ConverterToolSpec> all(specs);
    return all;
}

std::string json_string(std::string_view text) {
    std::string quoted = "\"";
    quoted.reserve(text.size() + 2U);
    for (const char byte : text) {
        const unsigned char code = static_cast<unsigned char>(byte);
        switch (byte) {
        case '"': quoted.append("\\\""); break;
        case '\\': quoted.append("\\\\"); break;
        case '\n': quoted.append("\\n"); break;
        case '\r': quoted.append("\\r"); break;
        case '\t': quoted.append("\\t"); break;
        default:
            if (code < 0x20U) {
                char escape[8]{};
                std::snprintf(escape, sizeof(escape), "\\u%04X", static_cast<unsigned int>(code));
                quoted.append(escape);
            } else {
                quoted.push_back(byte);
            }
            break;
        }
    }
    quoted.push_back('"');
    return quoted;
}

std::string theme_diagnostic_line(const theme_language::ThemeDiagnostic& record) {
    const bool error = theme_language::diagnostic_severity(record.code) == theme_language::DiagnosticSeverity::error;
    std::string line = record.file + ":" + std::to_string(record.position.line.value) + ":" +
                       std::to_string(record.position.column.value) + ": " + (error ? "error" : "warning") + ": ";
    line.append(theme_language::diagnostic_code_label(record.code));
    line.push_back(' ');
    line.append(theme_language::diagnostic_code_name(record.code));
    line.append(": ");
    line.append(record.rule);
    if (!record.property.empty()) {
        line.push_back(' ');
        line.append(record.property);
    }
    line.append(": ");
    line.append(record.detail);
    return line;
}

bool validate_form_document(std::string_view file, std::string_view document, const form_language::FormSchema& schema,
                            theme_language::ThemeDiagnosticList& diagnostics) {
    theme_language::ThemeSource source{};
    source.file = file;
    source.text = document;
    form_language::FormSyntax syntax{};
    form_language::FormModel model{};
    const form_language::FormModelOutcome built =
        form_language::read_form_model(source, syntax, schema, model, diagnostics);
    const bool valid = built == form_language::FormModelOutcome::model_built;
    return valid;
}

ConverterToolReply run_list_forms(const ListFormsRequest& request) {
    ConversionDiagnosticList diagnostics{};
    WinFormsProject project{};
    if (find_winforms_project(request.project, project, diagnostics) != ProjectOutcome::found) {
        return refused_reply(diagnostics);
    }
    ConverterToolReply reply{};
    reply.succeeded = true;
    reply.json = "{\"root\":" + json_string(project.root) + ",\"forms\":[";
    for (std::size_t index = 0U; index < project.forms.size(); ++index) {
        const ProjectForm& form = project.forms[index];
        reply.text.append(form.name + "  " + form.designer_file + (form.resx_file.empty() ? "" : "  (resx)") + "\n");
        if (index != 0U) reply.json.push_back(',');
        reply.json.append("{\"name\":" + json_string(form.name) + ",\"designer_file\":" +
                          json_string(form.designer_file) + ",\"resx_file\":" + json_string(form.resx_file) + "}");
    }
    reply.json.append("]}");
    return reply;
}

ConverterToolReply run_convert(const ConvertRequest& request, const form_language::FormSchema& schema) {
    ConversionDiagnosticList diagnostics{};
    WinFormsProject project{};
    if (find_winforms_project(request.project, project, diagnostics) != ProjectOutcome::found) {
        return refused_reply(diagnostics);
    }
    const ProjectForm* const form = project.find_form(request.form);
    if (form == nullptr) {
        ConversionDiagnostic missing{};
        missing.code = ConversionCode::form_not_found;
        missing.file = project.root;
        missing.subject = request.form;
        missing.detail = "the project has no such form; winforms.list_forms lists them";
        diagnostics.add(std::move(missing));
        return refused_reply(diagnostics);
    }
    CSharpClassIndex classes{};
    read_project_classes(project, classes, diagnostics);
    ConvertedForm converted{};
    if (convert_project_form(*form, classes, schema, request.options, converted, diagnostics) !=
        ConversionOutcome::converted) {
        return refused_reply(diagnostics);
    }
    theme_language::ThemeDiagnosticList validation{};
    const bool valid = validate_form_document(converted.form_file, converted.form_document, schema, validation);
    std::string written{};
    bool wrote = true;
    if (!request.output_directory.empty()) {
        const std::string form_path = joined_path(request.output_directory, converted.form_file);
        const std::string binding_path = joined_path(request.output_directory, converted.binding_file);
        wrote = write_text_file(form_path, converted.form_document, diagnostics);
        wrote = write_text_file(binding_path, converted.csharp_binding, diagnostics) && wrote;
        written = json_string(form_path) + "," + json_string(binding_path);
        if (!converted.theme_file.empty()) {
            const std::string theme_path = joined_path(request.output_directory, converted.theme_file);
            wrote = write_text_file(theme_path, converted.theme_sheet, diagnostics) && wrote;
            written.append("," + json_string(theme_path));
        }
    }
    ConverterToolReply reply{};
    reply.succeeded = wrote;
    const ConversionCounts& counts = converted.counts;
    reply.text = converted.class_name + " -> " + converted.form_file + ": " + std::to_string(counts.objects) +
                 " objects (" + std::to_string(counts.unmapped_objects) + " unmapped), " +
                 std::to_string(counts.properties) + " properties, " + std::to_string(counts.bindings) +
                 " bindings, " + std::to_string(counts.events) + " handlers, " +
                 std::to_string(counts.theme_declarations) + " theme declarations, " +
                 std::to_string(counts.code_items) + " items left to code, " + std::to_string(counts.dropped) +
                 " dropped; " + (valid ? "the document validates" : "the document does not validate") + ".\n";
    std::string conversion_json{};
    append_conversion_diagnostics(diagnostics, reply.text, conversion_json);
    std::string validation_json{};
    append_theme_diagnostics(validation, reply.text, validation_json);
    reply.json = "{\"class_name\":" + json_string(converted.class_name) + ",\"form_id\":" +
                 json_string(converted.form_id) + ",\"form_file\":" + json_string(converted.form_file) +
                 ",\"form_document\":" + json_string(converted.form_document) + ",\"theme_file\":" +
                 json_string(converted.theme_file) + ",\"theme_sheet\":" + json_string(converted.theme_sheet) +
                 ",\"binding_file\":" + json_string(converted.binding_file) + ",\"csharp_binding\":" +
                 json_string(converted.csharp_binding) + ",\"valid\":" + (valid ? "true" : "false") +
                 ",\"written\":[" + written + "],\"sources\":[";
    for (std::size_t index = 0U; index < converted.sources.size(); ++index) {
        const ModelSource& source = converted.sources[index];
        if (index != 0U) reply.json.push_back(',');
        reply.json.append("{\"name\":" + json_string(source.name) + ",\"kind\":" +
                          (source.kind == ModelSourceKind::binding_source ? "\"binding_source\"" : "\"value\"") +
                          ",\"winforms_field\":" + json_string(source.winforms_field) + "}");
    }
    reply.json.append("],\"counts\":{\"objects\":" + std::to_string(counts.objects) +
                      ",\"unmapped_objects\":" + std::to_string(counts.unmapped_objects) +
                      ",\"properties\":" + std::to_string(counts.properties) +
                      ",\"bindings\":" + std::to_string(counts.bindings) + ",\"events\":" +
                      std::to_string(counts.events) + ",\"theme_declarations\":" +
                      std::to_string(counts.theme_declarations) + ",\"code_items\":" +
                      std::to_string(counts.code_items) + ",\"dropped\":" + std::to_string(counts.dropped) +
                      "},\"diagnostics\":" + conversion_json + ",\"validation\":" + validation_json + "}");
    return reply;
}

ConverterToolReply run_validate(const ValidateRequest& request, const form_language::FormSchema& schema) {
    theme_language::ThemeDiagnosticList diagnostics{};
    const std::string file = request.file.empty() ? std::string("form.form") : request.file;
    const bool valid = validate_form_document(file, request.document, schema, diagnostics);
    ConverterToolReply reply{};
    reply.succeeded = true;
    reply.text = valid ? "valid: " + std::to_string(diagnostics.warning_count()) + " warnings\n"
                       : "not valid: " + std::to_string(diagnostics.error_count()) + " errors\n";
    std::string listed{};
    append_theme_diagnostics(diagnostics, reply.text, listed);
    reply.json = std::string("{\"valid\":") + (valid ? "true" : "false") + ",\"diagnostics\":" + listed + "}";
    return reply;
}

ConverterToolReply run_diff(const DiffRequest& request) {
    FormDiff diff{};
    ConverterToolReply reply{};
    if (diff_form_documents(request.before, request.after, diff) != FormDiffOutcome::compared) {
        reply.text = diff.problem + "\n";
        reply.json = "{\"problem\":" + json_string(diff.problem) + "}";
        return reply;
    }
    reply.succeeded = true;
    reply.text = diff.entries.empty() ? std::string("no difference\n") : form_diff_text(diff);
    reply.json = "{\"entries\":[";
    for (std::size_t index = 0U; index < diff.entries.size(); ++index) {
        const FormDiffEntry& entry = diff.entries[index];
        if (index != 0U) reply.json.push_back(',');
        reply.json.append("{\"kind\":" + json_string(form_diff_kind_name(entry.kind)) + ",\"id\":" +
                          json_string(entry.id) + ",\"name\":" + json_string(entry.name) + ",\"before\":" +
                          json_string(entry.before) + ",\"after\":" + json_string(entry.after) + "}");
    }
    reply.json.append("],\"dropped\":" + std::to_string(diff.dropped) + "}");
    return reply;
}

} // namespace gui_forms::winforms_converter
