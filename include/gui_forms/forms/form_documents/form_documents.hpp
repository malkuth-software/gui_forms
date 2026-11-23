#pragma once

// Form documents for tools: the schema a document may name, a document
// checked with the form compiler's diagnostics, and a document loaded into
// a window to look at. A converter, a designer or an editor built against
// the SDK reads documents through these, with the same parser, model and
// checks the form compiler and the run-time loader use.
//
//     const gui_forms::FormDocumentSchema& schema = gui_forms::FormDocumentSchema::registry();
//     const gui_forms::FormDocumentCheck check =
//         gui_forms::check_form_document(schema, "main.form", 9U, text, text_bytes);
//     for (stxlib::usize index = 0; index < check.count(); ++index) {
//         const gui_forms::FormDocumentDiagnostic found = check.diagnostic(index);
//         // found.line, found.column, found.error, found.code_label, found.detail ...
//     }
//
// The registry schema holds every stock type a document may name (the form
// compiler's schema without application or composite types). Texts are
// UTF-8 with their byte counts; a text that is not UTF-8 is a diagnostic,
// not a crash. What a result returns is borrowed from it: keep the result
// while reading. UI thread: the schema describes each type from a fresh
// prototype, and a window is made on the application's thread.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/value/binding_value.hpp"

#include "stxlib/types.hpp"
#include "stxlib/unique.hpp"
#include "stxlib/view.hpp"

namespace gui_forms {

class Window;
class FormDocumentCheck;

namespace detail {
struct FormDocumentSchemaState;
struct FormDocumentCheckState;
struct FormDocumentWindowState;
// The implementation's access to the classes below.
struct FormDocumentAccess;
} // namespace detail

// One type a document may name, borrowed from its schema.
class GUI_FORMS_API FormDocumentType final {
public:
    // The registered name: `check_box` (a document writes `check-box`).
    [[nodiscard]] stxlib::View<char> name() const noexcept;
    // Whether objects of the type may hold objects: controls do, other
    // components do not.
    [[nodiscard]] bool holds_children() const noexcept;
    // The properties a document may set on the type.
    [[nodiscard]] stxlib::View<PropertyDescriptor> properties() const noexcept;
    // The properties the type provides to its direct children.
    [[nodiscard]] stxlib::View<PropertyDescriptor> child_properties() const noexcept;
    // The interactions' registered names: `checked_changed`.
    [[nodiscard]] stxlib::usize interaction_count() const noexcept;
    [[nodiscard]] stxlib::View<char> interaction(stxlib::usize index) const noexcept;

private:
    friend class FormDocumentSchema;
    FormDocumentType(const detail::FormDocumentSchemaState* state, stxlib::usize index) noexcept
        : state_(state), index_(index) {}

    const detail::FormDocumentSchemaState* state_{};
    stxlib::usize index_{};
};

// The types a document may name.
class GUI_FORMS_API FormDocumentSchema final {
public:
    FormDocumentSchema(const FormDocumentSchema&) = delete;
    FormDocumentSchema& operator=(const FormDocumentSchema&) = delete;

    // The registry's: every stock type that can be made alone, in registry
    // order, then the form root. Made on the first call (UI thread) and
    // never destroyed.
    [[nodiscard]] static const FormDocumentSchema& registry();

    [[nodiscard]] stxlib::usize type_count() const noexcept;
    // The type at `index`, which must be below type_count().
    [[nodiscard]] FormDocumentType type(stxlib::usize index) const noexcept;
    // The index of the type registered as `name`, or type_count() for none.
    [[nodiscard]] stxlib::usize find(const char* name, stxlib::usize name_bytes) const noexcept;

private:
    friend struct detail::FormDocumentAccess;
    explicit FormDocumentSchema(const detail::FormDocumentSchemaState* state) noexcept : state_(state) {}

    const detail::FormDocumentSchemaState* state_{};
};

// One diagnostic, in the form compiler's terms
// (`main.form:4:7: error: E812 form_generation_refused: ...`). The texts
// are borrowed from the check, the code's label and name from static
// storage.
struct GUI_FORMS_API FormDocumentDiagnostic final {
    bool error{};
    // One-based; zero where the diagnostic names no place.
    stxlib::usize line{};
    stxlib::usize column{};
    // `E812`, and the code's name: `form_generation_refused`.
    stxlib::View<char> code_label{};
    stxlib::View<char> code_name{};
    stxlib::View<char> file{};
    // The element or rule it concerns, the property (may be empty), and
    // what is wrong.
    stxlib::View<char> rule{};
    stxlib::View<char> property{};
    stxlib::View<char> detail{};
};

// A document's check: whether it compiles, and the diagnostics in the
// order found. Move-only; an empty check (moved from, or refused for want
// of memory) does not compile and holds none.
class GUI_FORMS_API FormDocumentCheck final {
public:
    FormDocumentCheck() noexcept;
    FormDocumentCheck(FormDocumentCheck&& other) noexcept;
    FormDocumentCheck& operator=(FormDocumentCheck&& other) noexcept;
    FormDocumentCheck(const FormDocumentCheck&) = delete;
    FormDocumentCheck& operator=(const FormDocumentCheck&) = delete;
    ~FormDocumentCheck();

    // The form compiler would accept the document (warnings allowed).
    [[nodiscard]] bool compiled() const noexcept;
    [[nodiscard]] stxlib::usize count() const noexcept;
    [[nodiscard]] stxlib::usize error_count() const noexcept;
    // The diagnostic at `index` (below count()), borrowed from this check.
    [[nodiscard]] FormDocumentDiagnostic diagnostic(stxlib::usize index) const& noexcept;
    FormDocumentDiagnostic diagnostic(stxlib::usize index) const&& = delete;

private:
    friend struct detail::FormDocumentAccess;
    explicit FormDocumentCheck(stxlib::Unique<detail::FormDocumentCheckState> state) noexcept;

    stxlib::Unique<detail::FormDocumentCheckState> state_{};
};

// `document` (named `file` in the diagnostics) checked against `schema` as
// the form compiler checks it: parsed, its model built, every name, value
// and binding checked. Nothing is made.
[[nodiscard]] GUI_FORMS_API FormDocumentCheck check_form_document(const FormDocumentSchema& schema, const char* file,
                                                                  stxlib::usize file_bytes, const char* document,
                                                                  stxlib::usize document_bytes);

// A document loaded into a window, to look at: the window, the document's
// title, and the load's check. The window lives as long as this value;
// the program shows or captures it (OffscreenAttachment) but does not
// keep it past this value. Move-only.
class GUI_FORMS_API FormDocumentWindow final {
public:
    FormDocumentWindow() noexcept;
    FormDocumentWindow(FormDocumentWindow&& other) noexcept;
    FormDocumentWindow& operator=(FormDocumentWindow&& other) noexcept;
    FormDocumentWindow(const FormDocumentWindow&) = delete;
    FormDocumentWindow& operator=(const FormDocumentWindow&) = delete;
    ~FormDocumentWindow();

    // The window, or null when the load was refused (the check says why).
    [[nodiscard]] Window* window() const& noexcept;
    Window* window() const&& = delete;
    // The document's title (a Window holds none), for the host.
    [[nodiscard]] stxlib::View<char> title() const& noexcept;
    stxlib::View<char> title() const&& = delete;
    [[nodiscard]] const FormDocumentCheck& check() const& noexcept { return check_; }
    const FormDocumentCheck& check() const&& = delete;

private:
    friend struct detail::FormDocumentAccess;
    FormDocumentWindow(stxlib::Unique<detail::FormDocumentWindowState> state, FormDocumentCheck check) noexcept;

    stxlib::Unique<detail::FormDocumentWindowState> state_{};
    FormDocumentCheck check_{};
};

// `document` loaded against `schema` into a window of the current
// application (or, with none, a window with its own resources), with no
// model: a document that binds (`<bind>`) is refused, so a preview blanks
// its bindings first. A document whose root is a control, not a form, is
// refused too. UI thread.
[[nodiscard]] GUI_FORMS_API FormDocumentWindow load_form_document_window(const FormDocumentSchema& schema,
                                                                         const char* file, stxlib::usize file_bytes,
                                                                         const char* document,
                                                                         stxlib::usize document_bytes);

} // namespace gui_forms
