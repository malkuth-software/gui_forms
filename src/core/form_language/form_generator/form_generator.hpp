#pragma once

// A form document's C++ generator (stage FD, phase FD5; owner decision
// FD-4): the header that builds what load_form builds, as code. Internal
// until stage D; the form compiler (tools/form_compiler/) is its program,
// shipped in the SDK.
//
// The generated header holds one class template over the owner class
// (owner decision FD-3), whose members are the sources the document binds
// (owner rulings FD-6 R1 and R2):
//
//   template <typename Owner>
//   class SettingsForm final {
//   public:
//       std::shared_ptr<gui_forms::CheckBox> settings_autosave{};
//       ...
//       void initialize_component(Owner& owner);
//   };
//
// - Members: one per object, in document order, named after its stable ID
//   with '.' and '-' spelled '_' (settings.auto-save: settings_auto_save).
//   A form root has no member; it is the window (see below).
// - initialize_component makes every object in document order, adding each
//   control to its parent as it is made, then sets or binds each property
//   in document order: the loader's order (form_loader.hpp). It builds a
//   fresh instance and moves it into this one only once everything
//   succeeded, so a throw leaves the form as it was.
// - A property on a capability (PropertyDescriptor::assignable_member) is
//   assigned through its cell or view, `(*made.ok).text =
//   gui_forms::assigned_property_value<decltype((*made.ok).text)>(value)`,
//   and a child property through the parent's child view subscripted by
//   the child (CLOSURE P-6). A property not yet migrated is set through
//   Control::set_property_value (set_child_property_value), the loader's
//   path. Both take the model's normalized value and convert it as the
//   table write does, so both paths set the same values.
// - A bound property is bound where a literal would be set, reading the
//   owner's member: `gui_forms::bind_form_value<Kind>(..., owner.<source>,
//   ...)` for a Value (one whose type does not carry the property's kind
//   does not compile), `gui_forms::bind_form_field(...)` for a
//   BindingSource field and `bind(owner.<source>)` for a Command. The
//   controls keep their bindings, so the owner's sources must outlive them:
//   declare the form after them. A form names no program code (FD-6 R6):
//   the owner attaches behaviour itself.
// - A form root adds window_title, window_client_size (when the document
//   sets them) and attach_buttons(window), which gives a window the accept
//   and cancel buttons.
// - Style blocks are not applied by the loader yet, and not generated.
//
// The output is deterministic: the same document, schema and options give
// the same bytes, with LF line ends, written in the house style.

#include "../form_model/form_model.hpp"

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../theme_language/tokenizer/theme_tokenizer.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::form_language {

struct FormGeneratorOptions final {
    // The generated class's name: a C++ identifier.
    std::string class_name{};
    // The namespace it is declared in, `::`-separated identifiers; empty
    // for the global namespace.
    std::string namespace_name{};
    // The document's name as the header's first comment cites it.
    std::string source_name{};
    // Stage B (APP_WINDOW_RULES_DESIGN Part B): the class derives from the
    // root's type instead of being a document over an owner. A form root
    // gives a gui_forms::Window subclass whose constructors take the model
    // (and, in an overload, the Application); a control root gives a
    // subclass of its type made with make(model). A control root whose type
    // is final (FormSchemaType::final_type: the layout panels) is composed
    // instead: a class holding the root as its first member, whose static
    // make(model) returns it by value with every object made and set, and
    // whose root_control() is the control to show. Opt-in until the Gallery
    // migrates.
    bool derived{};
    // The model a derived class binds: a `::`-separated name, required when
    // the form binds (E812 otherwise).
    std::string model_type{};
    // The header that declares the model, included by the generated header
    // when not empty (a path as an #include names it).
    std::string model_header{};
};

enum class FormGenerateOutcome : std::uint8_t {
    generated,
    // At least one E812 (form_generation_refused): every defect found is
    // reported, in document order, and `generated` is unchanged.
    refused,
};

// The member name of the object with `stable_id`: '.' and '-' spelled '_'.
// Empty when that is not a C++ identifier the generator may declare (it
// starts with a digit, or is a keyword).
[[nodiscard]] std::string form_member_name(std::string_view stable_id);

// Whether `name` is an identifier the generator may declare: a letter or
// '_' then letters, digits and '_', not a C++ keyword, and not starting
// with '_' (reserved spellings).
[[nodiscard]] bool form_identifier(std::string_view name) noexcept;

// Whether `name` is `::`-separated form identifiers (`gui_forms::CheckBox`,
// `my_app::forms`), without a leading `::`.
[[nodiscard]] bool form_qualified_name(std::string_view name);

// Generates the header for `model`, which build_form_model built from
// `tree` against `schema`; the source, tokens and lines are those of that
// build and locate the diagnostics. Each type `model` names needs its
// FormSchemaType::cpp_type and header (E812 otherwise). Everything is
// borrowed for the call. Throws std::invalid_argument when `options` names
// no valid class or namespace, before anything is generated.
[[nodiscard]] FormGenerateOutcome generate_form_cpp(const theme_language::ThemeSource& source,
                                                    const theme_language::ThemeTokenBuffer& tokens,
                                                    const theme_language::LineStartTable& lines,
                                                    const theme_language::ThemeSyntaxTree& tree,
                                                    const FormSchema& schema, const FormModel& model,
                                                    const FormGeneratorOptions& options, std::string& generated,
                                                    theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
