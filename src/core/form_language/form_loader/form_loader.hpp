#pragma once

// A form document's run-time loader (stage FD, phase FD3; owner decision
// FD-5): the objects a FormModel describes, made and wired. Internal until
// stage D. Generated C++ (FD5) and the loader build the same tree.
//
// Order: objects are made in document order, each control added to its
// parent control as it is made, so child order is document order (owner
// decision FD-2: later on top); then each property is set in document
// order. Handler references are not connected: their payloads differ by
// interaction, so the owner class's generated code (FD5) or the caller
// connects them from LoadedForm::handlers.

#include "../form_model/form_model.hpp"

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms::form_language {

// The window a form root describes, for the caller to apply: its title,
// client size, and the stable IDs of its accept and cancel buttons.
struct LoadedWindow final {
    std::string title{};
    std::optional<Size> client_size{};
    std::string accept_button{};
    std::string cancel_button{};
};

// What a load made. Caller-owned: replaced whole by a load that succeeds,
// left as it was by one that refuses.
struct LoadedForm final {
    // The root object's control; for a form root, the form's control.
    Control::Ptr root{};
    // A form root's window; none for a control root.
    std::optional<LoadedWindow> window{};
    // Every object, at its FormModel::objects index (null for a form
    // root). Controls other than the root are also owned by their parents;
    // components are owned here only.
    std::vector<Component::Ptr> objects{};
    // The document's handler references, for the caller to connect.
    std::vector<FormHandler> handlers{};
};

enum class FormLoadOutcome : std::uint8_t {
    loaded,
    // A creator is missing, or a run-time type refused an object, a child
    // or a value (E811, every refusal reported): everything made is
    // released and `loaded` is unchanged.
    refused,
};

// Loads `model`, which build_form_model built from `tree` against
// `schema`; the source, tokens and lines are those of that build and
// locate the diagnostics. Everything is borrowed for the call. UI thread.
[[nodiscard]] FormLoadOutcome load_form(const theme_language::ThemeSource& source,
                                        const theme_language::ThemeTokenBuffer& tokens,
                                        const theme_language::LineStartTable& lines,
                                        const theme_language::ThemeSyntaxTree& tree, const FormSchema& schema,
                                        const FormModel& model, LoadedForm& loaded,
                                        theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
