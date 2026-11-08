#pragma once

// A form document's run-time loader (stage FD, phase FD3; owner decision
// FD-5): the objects a FormModel describes, made and wired. Internal until
// stage D. Generated C++ (FD5) and the loader build the same tree.
//
// Order: objects are made in document order, each control added to its
// parent control as it is made, so child order is document order (owner
// decision FD-2: later on top); then each property is set or bound in
// document order. A bound property takes its source's value and is
// subscribed in the same step (owner ruling FD-6 R2): the sources are the
// load's input (FormSources), and a missing or mistyped source refuses the
// whole load (E819). A form names no program code (FD-6 R6): behaviour is
// attached by the caller from program logic.

#include "../form_model/form_model.hpp"

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include "gui_forms/binding/binding_source/binding_source.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/value.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
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

// One source a run-time load binds: what generated code reaches as an
// owner member. The pointers are borrowed: each source must outlive the
// controls bound to it, or be disposed first.
using FormSourceReference =
    std::variant<Value<bool>*, Value<int>*, Value<double>*, Command*, std::shared_ptr<BindingSource>>;

// The sources a run-time load binds, by the names the document's bindings
// write. Caller-owned; borrowed by load_form for its call.
class FormSources final {
public:
    // Adds `source` under `name`. A name added before, or one that is not a
    // binding source's spelling (one C++ identifier), is
    // std::invalid_argument, adding nothing.
    void add(std::string name, Value<bool>& source);
    void add(std::string name, Value<int>& source);
    void add(std::string name, Value<double>& source);
    void add(std::string name, Command& source);
    void add(std::string name, std::shared_ptr<BindingSource> source);
    // The source added under `name`, or null.
    [[nodiscard]] const FormSourceReference* find(std::string_view name) const noexcept;

private:
    struct NamedFormSource final {
        std::string name{};
        FormSourceReference source{};
    };
    void add_entry(std::string name, FormSourceReference source);
    std::vector<NamedFormSource> entries_{};
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
};

enum class FormLoadOutcome : std::uint8_t {
    loaded,
    // A creator is missing, or a run-time type refused an object, a child
    // or a value (E811), or a binding's source is missing, of another kind
    // or refused (E819), every refusal reported: everything made is
    // released and `loaded` is unchanged.
    refused,
};

// Loads `model`, which build_form_model built from `tree` against
// `schema`, binding its bound properties to `sources`; the source, tokens
// and lines are those of that build and locate the diagnostics.
// Everything is borrowed for the call. UI thread.
[[nodiscard]] FormLoadOutcome load_form(const theme_language::ThemeSource& source,
                                        const theme_language::ThemeTokenBuffer& tokens,
                                        const theme_language::LineStartTable& lines,
                                        const theme_language::ThemeSyntaxTree& tree, const FormSchema& schema,
                                        const FormModel& model, const FormSources& sources, LoadedForm& loaded,
                                        theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
