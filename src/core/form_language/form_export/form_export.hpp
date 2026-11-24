#pragma once

// Form documents of running windows (stage MCP G6; study section 18.2):
// what `forms export`, `forms validate`, `forms apply` and `forms revert`
// do, independent of any transport.
//
// A window takes part once its form document is registered here with the
// sources it was bound to (its program's data and commands): by generated
// code built with GUI_FORMS_FORM_EXPORT (register_form_document), or by
// whoever loaded it. Each operation takes the schema to read against (the
// registry's, from the caller: the library holds no catalog). Then:
//
// - **export** returns the document as the program has it: the applied
//   one after an apply, the registered one otherwise;
// - **validate** reads a candidate against the schema (the theme
//   language's rules: an unknown property or a bad value is an error) and
//   checks that every `<bind>` names a source the program registered;
// - **apply** loads a valid candidate and, only once it has loaded,
//   replaces the window's structure: the children of its root control and
//   the root's local properties. With a target stable ID it replaces that
//   control's children and properties instead (structural patching; the
//   candidate's root is then a control with the same stable ID). A
//   candidate that does not validate or load leaves the window as it was,
//   and so does one the live window refuses while it is put in place (a
//   stable ID that collides with a control elsewhere in the window, a
//   property value the control refuses): the old children and properties
//   are put back and the apply is refused. Should putting them back fail
//   too, the window is marked patched (export no longer describes it);
// - **revert** applies the registered document again.
//
// Rules (section 18.2): only registry vocabulary and no code (the schema
// admits nothing else); bindings only to registered sources; the window's
// root keeps its identity (a candidate whose root has another stable ID is
// refused); a window or part that was replaced is marked so the inspect and
// tag line can say so. The window's title and client size are not changed
// by an apply. Applying is destructive for the tool's marks.
//
// Records hold the window's root weakly: a window that ends is forgotten
// at the next call. UI thread.

#include "../form_loader/form_loader.hpp"
#include "../form_model/form_model.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::form_language {

// The most windows registered at once.
inline constexpr std::size_t maximum_exported_windows = 256U;

// How the window stands against its exported document.
enum class FormExportState : std::uint8_t {
    // As registered (or reverted).
    registered,
    // A whole-window apply replaced it; export returns the applied document.
    applied,
    // A targeted apply (or a refused apply that could not be undone)
    // changed a part of it that the exported document does not carry.
    patched,
};

[[nodiscard]] std::string_view form_export_state_name(FormExportState state) noexcept;

enum class FormExportOutcome : std::uint8_t {
    done,
    // The window's root is not registered (or has ended).
    not_registered,
    // The candidate does not validate or does not load; diagnostics say why
    // and the window is as it was.
    refused,
    // A target stable ID that names no control under the window's root.
    target_not_found,
};

[[nodiscard]] std::string_view form_export_outcome_name(FormExportOutcome outcome) noexcept;

class FormExportRegistry final {
public:
    // Registers `root` (a window's root control) with the document it was
    // built from and the sources it was bound to, replacing a record of the
    // same root. `sources` is borrowed and must outlive the record. False
    // when maximum_exported_windows live records exist.
    bool add(const Control::Ptr& root, std::string file, std::string document, const FormSources& sources);
    void forget(const Control& root) noexcept;

    [[nodiscard]] bool registered(const Control& root);
    // Whether an apply replaced the window (or a part of it) since it was
    // registered or reverted: state() is not registered.
    [[nodiscard]] bool replaced(const Control& root);
    // How the window stands against its document; registered for a root
    // that is not registered.
    [[nodiscard]] FormExportState state(const Control& root);

    // The document as the program has it, into `document`.
    [[nodiscard]] FormExportOutcome export_document(const Control& root, std::string& document);
    // Validates `candidate` against `schema` and the registered sources.
    [[nodiscard]] FormExportOutcome validate(const Control& root, const FormSchema& schema, std::string_view file,
                                             std::string_view candidate,
                                             theme_language::ThemeDiagnosticList& diagnostics);
    // Replaces the window's structure, or `target`'s (empty: the whole).
    [[nodiscard]] FormExportOutcome apply(const Control& root, const FormSchema& schema, std::string_view file,
                                          std::string_view candidate, std::string_view target,
                                          theme_language::ThemeDiagnosticList& diagnostics);
    [[nodiscard]] FormExportOutcome revert(const Control& root, const FormSchema& schema,
                                           theme_language::ThemeDiagnosticList& diagnostics);

private:
    struct Record final {
        std::weak_ptr<Control> root{};
        std::string file{};
        std::string registered_document{};
        std::string current_document{};
        const FormSources* sources{};
        // What the last apply made that only the load owns (components).
        std::vector<Component::Ptr> applied_objects{};
        FormExportState state{FormExportState::registered};
    };

    [[nodiscard]] Record* find(const Control& root);
    void prune() noexcept;

    std::vector<Record> records_{};
};

} // namespace gui_forms::form_language
