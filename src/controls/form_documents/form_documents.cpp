#include "gui_forms/forms/form_documents/form_documents.hpp"

#include "../form_catalog/form_catalog.hpp"

#include "../../core/form_language/form_loader/form_window_loader.hpp"
#include "../../core/form_language/form_model/form_model.hpp"
#include "../../core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../core/theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace detail {

// A schema's types, borrowed by the schema object.
struct FormDocumentSchemaState final {
    const form_language::FormSchema* schema{};
};

// One diagnostic as the check keeps it: the record, and its code's label
// (made per record by the diagnostics' formatter).
struct FormDocumentCheckRecord final {
    theme_language::ThemeDiagnostic record{};
    std::string label{};
};

struct FormDocumentCheckState final {
    FormDocumentCheckState() noexcept = default;

    bool compiled{};
    std::size_t errors{};
    std::vector<FormDocumentCheckRecord> records{};
};

// A loaded window, with the components only the load owns, and its title.
struct FormDocumentWindowState final {
    FormDocumentWindowState() noexcept = default;

    // Declared before the window, which ends first.
    std::vector<Component::Ptr> components{};
    std::unique_ptr<Window> window{};
    std::string title{};
};

struct FormDocumentAccess final {
    [[nodiscard]] static const FormDocumentSchemaState* state(const FormDocumentSchema& schema) noexcept {
        return schema.state_;
    }
    [[nodiscard]] static FormDocumentSchema* make_schema(const FormDocumentSchemaState* state) {
        return new FormDocumentSchema(state);
    }
    [[nodiscard]] static FormDocumentCheck make_check(stxlib::Unique<FormDocumentCheckState> state) noexcept {
        FormDocumentCheck check(std::move(state));
        return check;
    }
    [[nodiscard]] static FormDocumentWindow make_window(stxlib::Unique<FormDocumentWindowState> state,
                                                        FormDocumentCheck check) noexcept {
        FormDocumentWindow window(std::move(state), std::move(check));
        return window;
    }
};

} // namespace detail

namespace {

[[nodiscard]] stxlib::View<char> view_of(std::string_view text) noexcept {
    const stxlib::View<char> view(text.data(), static_cast<stxlib::usize>(text.size()));
    return view;
}

template <typename Element>
[[nodiscard]] stxlib::View<Element> view_of(const std::vector<Element>& elements) noexcept {
    const stxlib::View<Element> view(elements.data(), static_cast<stxlib::usize>(elements.size()));
    return view;
}

[[nodiscard]] std::string_view text_of(const char* text, stxlib::usize bytes) noexcept {
    if (text == nullptr) return std::string_view();
    const std::string_view view(text, static_cast<std::size_t>(bytes));
    return view;
}

// The diagnostics kept, in order; empty when the state could not be made.
[[nodiscard]] stxlib::Unique<detail::FormDocumentCheckState> check_state(
    const theme_language::ThemeDiagnosticList& diagnostics, bool compiled) {
    stxlib::OperationResult<stxlib::Unique<detail::FormDocumentCheckState>> made =
        stxlib::Unique<detail::FormDocumentCheckState>::make();
    if (made.failed()) return stxlib::Unique<detail::FormDocumentCheckState>();
    stxlib::Unique<detail::FormDocumentCheckState> state = std::move(made).take_value();
    (*state).compiled = compiled;
    (*state).errors = diagnostics.error_count();
    (*state).records.reserve(diagnostics.records().size());
    for (const theme_language::ThemeDiagnostic& record : diagnostics.records()) {
        detail::FormDocumentCheckRecord kept{};
        kept.record = record;
        kept.label = theme_language::diagnostic_code_label(record.code);
        (*state).records.push_back(std::move(kept));
    }
    return state;
}

// The schema's types; the registry's when the schema holds none.
[[nodiscard]] const form_language::FormSchema& schema_of(const detail::FormDocumentSchemaState* state) {
    if (state == nullptr || (*state).schema == nullptr) return form_catalog::process_registry_schema();
    return *(*state).schema;
}

} // namespace

// ----- FormDocumentType -----

stxlib::View<char> FormDocumentType::name() const noexcept {
    return view_of(std::string_view((*(*state_).schema).types[index_].name));
}

bool FormDocumentType::holds_children() const noexcept {
    return (*(*state_).schema).types[index_].holds_children;
}

stxlib::View<PropertyDescriptor> FormDocumentType::properties() const noexcept {
    return view_of((*(*state_).schema).types[index_].properties);
}

stxlib::View<PropertyDescriptor> FormDocumentType::child_properties() const noexcept {
    return view_of((*(*state_).schema).types[index_].child_properties);
}

stxlib::usize FormDocumentType::interaction_count() const noexcept {
    return static_cast<stxlib::usize>((*(*state_).schema).types[index_].interactions.size());
}

stxlib::View<char> FormDocumentType::interaction(stxlib::usize index) const noexcept {
    const std::vector<std::string>& interactions = (*(*state_).schema).types[index_].interactions;
    if (index >= interactions.size()) return stxlib::View<char>();
    return view_of(std::string_view(interactions[static_cast<std::size_t>(index)]));
}

// ----- FormDocumentSchema -----

const FormDocumentSchema& FormDocumentSchema::registry() {
    // Never destroyed, like the schema it borrows.
    static const detail::FormDocumentSchemaState* const state =
        new detail::FormDocumentSchemaState{&form_catalog::process_registry_schema()};
    static const FormDocumentSchema* const schema = detail::FormDocumentAccess::make_schema(state);
    return *schema;
}

stxlib::usize FormDocumentSchema::type_count() const noexcept {
    return static_cast<stxlib::usize>((*(*state_).schema).types.size());
}

FormDocumentType FormDocumentSchema::type(stxlib::usize index) const noexcept {
    const FormDocumentType type(state_, index < type_count() ? index : 0U);
    return type;
}

stxlib::usize FormDocumentSchema::find(const char* name, stxlib::usize name_bytes) const noexcept {
    const std::string_view wanted = text_of(name, name_bytes);
    const std::vector<form_language::FormSchemaType>& types = (*(*state_).schema).types;
    for (std::size_t index = 0U; index < types.size(); ++index) {
        if (types[index].name == wanted) return static_cast<stxlib::usize>(index);
    }
    return type_count();
}

// ----- FormDocumentCheck -----

FormDocumentCheck::FormDocumentCheck() noexcept = default;
FormDocumentCheck::FormDocumentCheck(FormDocumentCheck&& other) noexcept = default;
FormDocumentCheck& FormDocumentCheck::operator=(FormDocumentCheck&& other) noexcept = default;
FormDocumentCheck::~FormDocumentCheck() = default;

FormDocumentCheck::FormDocumentCheck(stxlib::Unique<detail::FormDocumentCheckState> state) noexcept
    : state_(std::move(state)) {}

bool FormDocumentCheck::compiled() const noexcept {
    return state_.present() && (*state_).compiled;
}

stxlib::usize FormDocumentCheck::count() const noexcept {
    if (!state_.present()) return 0U;
    return static_cast<stxlib::usize>((*state_).records.size());
}

stxlib::usize FormDocumentCheck::error_count() const noexcept {
    if (!state_.present()) return 0U;
    return static_cast<stxlib::usize>((*state_).errors);
}

FormDocumentDiagnostic FormDocumentCheck::diagnostic(stxlib::usize index) const& noexcept {
    FormDocumentDiagnostic diagnostic{};
    if (index >= count()) return diagnostic;
    const detail::FormDocumentCheckRecord& kept = (*state_).records[static_cast<std::size_t>(index)];
    const theme_language::ThemeDiagnostic& record = kept.record;
    diagnostic.error =
        theme_language::diagnostic_severity(record.code) == theme_language::DiagnosticSeverity::error;
    diagnostic.line = static_cast<stxlib::usize>(record.position.line.value);
    diagnostic.column = static_cast<stxlib::usize>(record.position.column.value);
    diagnostic.code_label = view_of(std::string_view(kept.label));
    diagnostic.code_name = view_of(theme_language::diagnostic_code_name(record.code));
    diagnostic.file = view_of(std::string_view(record.file));
    diagnostic.rule = view_of(std::string_view(record.rule));
    diagnostic.property = view_of(std::string_view(record.property));
    diagnostic.detail = view_of(std::string_view(record.detail));
    return diagnostic;
}

FormDocumentCheck check_form_document(const FormDocumentSchema& schema, const char* file, stxlib::usize file_bytes,
                                      const char* document, stxlib::usize document_bytes) {
    theme_language::ThemeSource source{};
    source.file = text_of(file, file_bytes);
    source.text = text_of(document, document_bytes);
    form_language::FormSyntax syntax{};
    form_language::FormModel model{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const form_language::FormModelOutcome built =
        form_language::read_form_model(source, syntax, schema_of(detail::FormDocumentAccess::state(schema)), model, diagnostics);
    const bool compiled = built == form_language::FormModelOutcome::model_built;
    return detail::FormDocumentAccess::make_check(check_state(diagnostics, compiled));
}

// ----- FormDocumentWindow -----

FormDocumentWindow::FormDocumentWindow() noexcept = default;
FormDocumentWindow::FormDocumentWindow(FormDocumentWindow&& other) noexcept = default;
FormDocumentWindow& FormDocumentWindow::operator=(FormDocumentWindow&& other) noexcept = default;
FormDocumentWindow::~FormDocumentWindow() = default;

FormDocumentWindow::FormDocumentWindow(stxlib::Unique<detail::FormDocumentWindowState> state,
                                       FormDocumentCheck check) noexcept
    : state_(std::move(state)), check_(std::move(check)) {}

Window* FormDocumentWindow::window() const& noexcept {
    if (!state_.present()) return nullptr;
    return (*state_).window.get();
}

stxlib::View<char> FormDocumentWindow::title() const& noexcept {
    if (!state_.present()) return stxlib::View<char>();
    return view_of(std::string_view((*state_).title));
}

FormDocumentWindow load_form_document_window(const FormDocumentSchema& schema, const char* file,
                                             stxlib::usize file_bytes, const char* document,
                                             stxlib::usize document_bytes) {
    const form_language::FormSources sources{};
    theme_language::ThemeDiagnosticList diagnostics{};
    form_language::LoadedWindowResult loaded = form_language::load_window(
        text_of(file, file_bytes), text_of(document, document_bytes), schema_of(detail::FormDocumentAccess::state(schema)), sources,
        diagnostics);
    const bool made = static_cast<bool>(loaded);
    FormDocumentCheck check = detail::FormDocumentAccess::make_check(check_state(diagnostics, made));
    if (!made) {
        return detail::FormDocumentAccess::make_window(stxlib::Unique<detail::FormDocumentWindowState>(),
                                                       std::move(check));
    }
    stxlib::OperationResult<stxlib::Unique<detail::FormDocumentWindowState>> state =
        stxlib::Unique<detail::FormDocumentWindowState>::make();
    if (state.failed()) {
        return detail::FormDocumentAccess::make_window(stxlib::Unique<detail::FormDocumentWindowState>(),
                                                       std::move(check));
    }
    stxlib::Unique<detail::FormDocumentWindowState> kept = std::move(state).take_value();
    (*kept).window = std::move(loaded.window);
    (*kept).components = std::move(loaded.components);
    (*kept).title = std::move(loaded.title);
    return detail::FormDocumentAccess::make_window(std::move(kept), std::move(check));
}

} // namespace gui_forms
