// load_window: a form document made into a window at run time (stage B).

#include "form_window_loader.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace gui_forms::form_language {

namespace {

// The storage one load fills, borrowed by the diagnostics' locations only
// during the call.
struct DocumentWorkspace final {
    theme_language::ThemeSource source{};
    theme_language::ThemeTokenBuffer tokens{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeSyntaxTree tree{};
    FormModel model{};
};

// The loaded control with `stable_id`, or null for an empty ID. The model
// has checked that a named button is one of the document's objects.
Control::Ptr loaded_control(const FormModel& model, const LoadedForm& loaded, const std::string& stable_id) {
    if (stable_id.empty()) return nullptr;
    for (std::size_t index = 0U; index < model.objects.size(); ++index) {
        if (model.objects[index].stable_id != stable_id) continue;
        const Control::Ptr control = std::dynamic_pointer_cast<Control>(loaded.objects[index]);
        return control;
    }
    return nullptr;
}

// Reads, models and loads the document; the caller makes the window over
// the loaded root.
WindowLoadError load_document(DocumentWorkspace& workspace, std::string_view file, std::string_view document,
                              const FormSchema& schema, const FormSources& sources, LoadedForm& loaded,
                              theme_language::ThemeDiagnosticList& diagnostics) {
    workspace.source.file = file;
    workspace.source.text = document;
    static_cast<void>(theme_language::tokenize_theme_sheet(workspace.source, workspace.tokens, workspace.lines,
                                                           diagnostics));
    const theme_language::ParseOutcome parsed =
        theme_language::parse_theme_sheet(workspace.source, workspace.tokens, workspace.lines, workspace.tree,
                                          diagnostics, theme_language::ThemeSheetForm::form_document);
    if (parsed != theme_language::ParseOutcome::tree_complete) return WindowLoadError::refused;
    const FormModelOutcome built = build_form_model(workspace.source, workspace.tokens, workspace.lines,
                                                    workspace.tree, schema, workspace.model, diagnostics);
    if (built != FormModelOutcome::model_built) return WindowLoadError::refused;
    // A control root makes no window: refused before anything is made.
    const std::size_t root_type = workspace.model.objects[0].schema_type;
    if (schema.types[root_type].name != form_root_type_name) {
        return WindowLoadError::not_a_window;
    }
    const FormLoadOutcome outcome = load_form(workspace.source, workspace.tokens, workspace.lines, workspace.tree,
                                              schema, workspace.model, sources, loaded, diagnostics);
    if (outcome != FormLoadOutcome::loaded) return WindowLoadError::refused;
    return WindowLoadError::none;
}

// Gives the made window its buttons and the result the rest of the load.
void finish(LoadedWindowResult& result, const FormModel& model, LoadedForm& loaded) {
    const LoadedWindow& described = *loaded.window;
    const Control::Ptr accept = loaded_control(model, loaded, described.accept_button);
    const Control::Ptr cancel = loaded_control(model, loaded, described.cancel_button);
    if (accept) (*result.window).set_accept_button(accept);
    if (cancel) (*result.window).set_cancel_button(cancel);
    result.title = described.title;
    for (Component::Ptr& object : loaded.objects) {
        if (!object || std::dynamic_pointer_cast<Control>(object)) continue;
        result.components.push_back(std::move(object));
    }
}

} // namespace

LoadedWindowResult load_window(Application& application, std::string_view file, std::string_view document,
                               const FormSchema& schema, const FormSources& sources,
                               theme_language::ThemeDiagnosticList& diagnostics) {
    LoadedWindowResult result{};
    DocumentWorkspace workspace{};
    LoadedForm loaded{};
    result.error = load_document(workspace, file, document, schema, sources, loaded, diagnostics);
    if (result.error != WindowLoadError::none) return result;
    const Size client_size = (*loaded.window).client_size.value_or(Size{});
    result.window = std::make_unique<Window>(application, loaded.root, client_size);
    finish(result, workspace.model, loaded);
    return result;
}

LoadedWindowResult load_window(std::string_view file, std::string_view document, const FormSchema& schema,
                               const FormSources& sources, theme_language::ThemeDiagnosticList& diagnostics) {
    LoadedWindowResult result{};
    DocumentWorkspace workspace{};
    LoadedForm loaded{};
    result.error = load_document(workspace, file, document, schema, sources, loaded, diagnostics);
    if (result.error != WindowLoadError::none) return result;
    const Size client_size = (*loaded.window).client_size.value_or(Size{});
    result.window = std::make_unique<Window>(loaded.root, client_size);
    finish(result, workspace.model, loaded);
    return result;
}

} // namespace gui_forms::form_language
