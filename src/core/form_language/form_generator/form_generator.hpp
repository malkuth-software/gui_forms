#pragma once

// A form document's C++ generator (stage FD, phase FD5; owner decision
// FD-4): the header that builds what load_form builds, as code. Internal
// until stage D; the form compiler (tools/form_compiler/) is its program,
// shipped in the SDK.
//
// The generated header holds one class template over the owner class
// (owner decision FD-3), whose member functions are the document's
// handlers:
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
//   control to its parent as it is made, then sets each property in
//   document order, then connects each handler in document order: the
//   loader's order (form_loader.hpp). It builds a fresh instance and moves
//   it into this one only once everything succeeded, so a throw leaves the
//   form as it was.
// - Handlers connect through Delegate::bind<Owner, &Owner::name>(owner),
//   so a handler whose signature differs from its event's arguments does
//   not compile. The form holds the connections: Owner must outlive the
//   form, or the form be reinitialized or destroyed first.
// - Properties are set through Control::set_property_value with the
//   model's normalized value, the path the loader takes, so both paths set
//   the same values.
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
