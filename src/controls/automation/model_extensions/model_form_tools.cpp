#include "model_form_tools.hpp"

#include "../core/automation_internal.hpp"
#include "../../form_catalog/form_catalog.hpp"
#include "../../form_document_export/process_form_exports.hpp"
#include "../../../core/form_language/form_model/form_model.hpp"
#include "../../../core/model_inspection/model_tool_specs.hpp"
#include "../../../core/theme_language/diagnostics/theme_diagnostics.hpp"

#include "gui_forms/window/window.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::automation {

namespace {

// The process's registry schema (made by the first call, on the UI
// thread, where every forms tool runs).
const form_language::FormSchema& model_tools_schema() {
    return form_catalog::process_registry_schema();
}

// `text` as a JSON string, quoted and escaped.
[[nodiscard]] std::string json_string(std::string_view text) {
    std::string quoted{};
    detail::append_json_string(text, quoted);
    return quoted;
}

// One theme diagnostic as a compiler-style line: file:line:column:
// severity: label name: rule property: detail.
[[nodiscard]] std::string theme_diagnostic_line(const theme_language::ThemeDiagnostic& record) {
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

// The arguments object, or a failure naming the problem.
struct Arguments final {
    detail::JsonValue object{};
    std::string error{};
};

[[nodiscard]] bool read(const ModelToolCall& call, Arguments& arguments) {
    const std::string_view json(call.arguments_json, call.arguments_bytes);
    const bool read = detail::read_arguments(json, arguments.object, arguments.error);
    return read;
}

[[nodiscard]] std::string text_or_empty(const Arguments& arguments, std::string_view name) {
    const std::optional<std::string> text = detail::text_member(arguments.object, name);
    std::string value = text ? *text : std::string();
    return value;
}

[[nodiscard]] ToolReply bad_arguments(std::string message) {
    return ToolReply::failure(ToolError::invalid_arguments, std::move(message));
}

// ----- forms.* (UI thread) -----

// The window the call names (`window`: its stable ID or title; empty: the
// active one), and its root control.
struct FormsTarget final {
    Window* window{};
    Control::Ptr root{};
};

[[nodiscard]] bool forms_target(const ModelToolCall& call, const Arguments& arguments, FormsTarget& target,
                                ToolReply& failure) {
    // The dispatcher resolved the call's `window` argument (else the active
    // window) before calling.
    Window* const window = call.window;
    if (window == nullptr) {
        const std::string name = text_or_empty(arguments, "window");
        failure = ToolReply::failure(ToolError::window_gone, name.empty() ? "no window" : "no window " + name,
                                     "the windows tool lists them");
        return false;
    }
    target.window = window;
    target.root = (*window).root();
    if (target.root == nullptr || !form_language::process_form_exports().registered(*target.root)) {
        failure = ToolReply::failure(ToolError::unsupported_action,
                                     "the window's form is not registered",
                                     "build the program with GUI_FORMS_FORM_EXPORT=ON");
        return false;
    }
    return true;
}

[[nodiscard]] ToolReply diagnostics_reply(form_language::FormExportOutcome outcome,
                                          const theme_language::ThemeDiagnosticList& diagnostics,
                                          std::string done_text) {
    std::string text{};
    std::string listed = "[";
    for (std::size_t index = 0U; index < diagnostics.records().size(); ++index) {
        const std::string line = theme_diagnostic_line(diagnostics.records()[index]);
        text += line + "\n";
        if (index != 0U) listed.push_back(',');
        listed += json_string(line);
    }
    listed += "]";
    const std::string name(form_language::form_export_outcome_name(outcome));
    if (outcome == form_language::FormExportOutcome::target_not_found) {
        return ToolReply::failure(ToolError::not_found, "no control with that target stable ID under the window");
    }
    if (outcome != form_language::FormExportOutcome::done) {
        return ToolReply::failure(ToolError::invalid_arguments, "refused; the window is unchanged\n" + text,
                                  "fix what the diagnostics name and call again");
    }
    ToolSuccess success{};
    success.text = std::move(done_text) + text;
    success.structured_json = "{\"outcome\":" + json_string(name) + ",\"diagnostics\":" +
                              listed + "}";
    return ToolReply::success(std::move(success));
}

ToolReply forms_export_tool(void* context, const ModelToolCall& call) {
    static_cast<void>(context);
    Arguments arguments{};
    if (!read(call, arguments)) return bad_arguments(arguments.error);
    FormsTarget target{};
    ToolReply failure{};
    if (!forms_target(call, arguments, target, failure)) return failure;
    std::string document{};
    static_cast<void>(form_language::process_form_exports().export_document(*target.root, document));
    const bool replaced = form_language::process_form_exports().replaced(*target.root);
    ToolSuccess success{};
    success.text = document;
    success.structured_json = "{\"document\":" + json_string(document) +
                              ",\"replaced\":" + (replaced ? "true" : "false") + "}";
    return ToolReply::success(std::move(success));
}

ToolReply forms_validate_tool(void* context, const ModelToolCall& call) {
    static_cast<void>(context);
    Arguments arguments{};
    if (!read(call, arguments)) return bad_arguments(arguments.error);
    const std::optional<std::string> document = detail::text_member(arguments.object, "document");
    if (!document) return bad_arguments("document is required");
    FormsTarget target{};
    ToolReply failure{};
    if (!forms_target(call, arguments, target, failure)) return failure;
    theme_language::ThemeDiagnosticList diagnostics{};
    const form_language::FormExportOutcome outcome = form_language::process_form_exports().validate(
        *target.root, model_tools_schema(), "candidate.form", *document, diagnostics);
    return diagnostics_reply(outcome, diagnostics, "valid\n");
}

ToolReply forms_apply_tool(void* context, const ModelToolCall& call) {
    static_cast<void>(context);
    Arguments arguments{};
    if (!read(call, arguments)) return bad_arguments(arguments.error);
    const std::optional<std::string> document = detail::text_member(arguments.object, "document");
    if (!document) return bad_arguments("document is required");
    FormsTarget target{};
    ToolReply failure{};
    if (!forms_target(call, arguments, target, failure)) return failure;
    const std::string part = text_or_empty(arguments, "target");
    theme_language::ThemeDiagnosticList diagnostics{};
    const form_language::FormExportOutcome outcome = form_language::process_form_exports().apply(
        *target.root, model_tools_schema(), "candidate.form", *document, part, diagnostics);
    return diagnostics_reply(outcome, diagnostics, part.empty() ? "applied to the window\n" : "applied to " + part + "\n");
}

ToolReply forms_revert_tool(void* context, const ModelToolCall& call) {
    static_cast<void>(context);
    Arguments arguments{};
    if (!read(call, arguments)) return bad_arguments(arguments.error);
    FormsTarget target{};
    ToolReply failure{};
    if (!forms_target(call, arguments, target, failure)) return failure;
    theme_language::ThemeDiagnosticList diagnostics{};
    const form_language::FormExportOutcome outcome =
        form_language::process_form_exports().revert(*target.root, model_tools_schema(), diagnostics);
    return diagnostics_reply(outcome, diagnostics, "reverted to the registered form\n");
}

[[nodiscard]] ToolSpec forms_spec(const model_inspection::ModelToolSpec& spec, std::string title) {
    ToolSpec made{};
    made.name = std::string(spec.name);
    made.title = std::move(title);
    made.description = std::string(spec.description);
    made.input_schema = std::string(spec.input_schema);
    made.access = spec.read_only ? ToolAccess::read_only : ToolAccess::acting;
    made.destructive = spec.destructive;
    // The forms tools read and change windows.
    made.thread = ToolThread::ui;
    made.settles = !spec.read_only;
    return made;
}

// Calls a handler written against a ToolReply (the library's own) and
// gives its reply through the public ModelToolReply.
template <ToolReply (*Body)(void* context, const ModelToolCall& call)>
void reply_tool(void* context, const ModelToolCall& call, ModelToolReply& reply) {
    const ToolReply made = Body(context, call);
    const ToolFailure* const failure = made.failure_value();
    if (failure != nullptr) {
        reply.fail((*failure).error, (*failure).message.c_str(), (*failure).hint.c_str());
        return;
    }
    const ToolSuccess* const success = made.success_value();
    if (success == nullptr) return;
    static_cast<void>(reply.text((*success).text.data(), (*success).text.size()));
    if (!(*success).structured_json.empty()) {
        static_cast<void>(reply.structured((*success).structured_json.data(), (*success).structured_json.size()));
    }
}

[[nodiscard]] std::size_t add(ToolRegistry& tools, ToolSpec spec, ToolHandler handler) {
    const bool added = tools.add(std::move(spec), handler, nullptr) == ToolRegistration::added;
    return added ? 1U : 0U;
}

} // namespace

std::size_t register_model_form_tools(ToolRegistry& tools) {
    std::size_t added = 0U;
    const ToolHandler forms_handlers[] = {&reply_tool<&forms_export_tool>, &reply_tool<&forms_validate_tool>,
                                          &reply_tool<&forms_apply_tool>, &reply_tool<&forms_revert_tool>};
    const char* const forms_titles[] = {"Export a window's form", "Validate a form document", "Apply a form document",
                                        "Revert a window's form"};
    for (std::size_t index = 0U; index < model_inspection::forms_tool_specs.size(); ++index) {
        added = added + add(tools, forms_spec(model_inspection::forms_tool_specs[index], forms_titles[index]),
                            forms_handlers[index]);
    }
    return added;
}

} // namespace gui_forms::automation
