#pragma once

// A form document's model (stage FD, phase FD2): the objects, properties,
// handler references and style blocks a parsed `.form` document describes,
// checked against a form schema before anything is built. Internal until
// stage D (owner decision FD-6).
//
// Spelling: a document writes the registry's names in kebab-case
// (`check-box`, `tab-index`, `on-checked-changed`); the schema holds them
// as registered (`check_box`, `tab_index`, `checked_changed`). A written
// name holding '_' names nothing.
//
// Values, by the property's kind:
// - boolean: `true` or `false`;
// - signed and unsigned integers: one integer number;
// - number: one number;
// - text: one string;
// - point and size: two numbers (x y, width height);
// - rectangle: four numbers (x y width height);
// - insets: one, two or four numbers in CSS order (all; vertical and
//   horizontal; top right bottom left);
// - color: one hash of three, four, six or eight hexadecimal digits;
// - enumeration: one choice, or for a flags enumeration one or more
//   choices separated by whitespace;
// - font (owner decision FD6-3): `<role> <size> <weight> [italic]`, the
//   role control, content or monospace, a positive size, a weight from 1
//   to 1000 (`font: control 12 700 italic;`);
// - collection (owner decision FD6-4): one or more strings, for a property
//   whose default is a text collection (`items: "AM" "FM";`);
// - image, object and other collections: not yet expressible (E999).
// A handler reference, `on-<interaction>: <handler>;`, names a member of
// the owner class (owner decision FD-3): one C++ identifier.

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::form_language {

// Makes a fresh object of one type for the loader (load_form): a named
// function per type, made from the templates below.
using FormObjectCreator = Component::Ptr (*)(StableId stable_id);

// One type a document may name, as the run-time registry describes it.
struct FormSchemaType final {
    // The registered name: `check_box`.
    std::string name{};
    // Whether objects of the type may hold objects: controls do, other
    // components do not.
    bool holds_children{};
    std::vector<PropertyDescriptor> properties{};
    // The interactions' registered names: `checked_changed`.
    std::vector<std::string> interactions{};
    // How the loader makes one; none for a type a document may describe
    // but the loader cannot make.
    FormObjectCreator create{};
    // For the generator (FD5): the C++ class, qualified from the global
    // namespace without a leading `::` (`gui_forms::CheckBox`, or an
    // application's `my_app::NoteCard`), and the header that declares it
    // (`gui_forms/controls/button_base/check_box/check_box.hpp`); empty for
    // a type the generator cannot spell.
    std::string cpp_type{};
    std::string header{};
};

// The types a document may name. Built by the caller, borrowed by
// build_form_model for its call.
struct FormSchema final {
    std::vector<FormSchemaType> types{};
};

// A type as `prototype`, a fresh instance of it, describes itself: its
// properties and interactions, and whether it is a control.
[[nodiscard]] FormSchemaType describe_form_type(std::string name, const Component& prototype);

// The creators of a control type, made with the document's stable ID, and
// of a component type, made from nothing (a component carries no stable
// ID; the document names it in the model only).
template <typename ControlType>
[[nodiscard]] Component::Ptr create_form_control(StableId stable_id) {
    const std::shared_ptr<ControlType> control = make_control<ControlType>(std::move(stable_id));
    return control;
}

template <typename ComponentType>
[[nodiscard]] Component::Ptr create_form_component(StableId) {
    const std::shared_ptr<ComponentType> component = std::make_shared<ComponentType>();
    return component;
}

// A control or component type described from a fresh prototype, with its
// creator.
template <typename ControlType>
[[nodiscard]] FormSchemaType form_control_type(std::string name) {
    const std::shared_ptr<ControlType> prototype = make_control<ControlType>(StableId("prototype"));
    FormSchemaType type = describe_form_type(std::move(name), *prototype);
    type.create = &create_form_control<ControlType>;
    return type;
}

template <typename ComponentType>
[[nodiscard]] FormSchemaType form_component_type(std::string name) {
    const std::shared_ptr<ComponentType> prototype = std::make_shared<ComponentType>();
    FormSchemaType type = describe_form_type(std::move(name), *prototype);
    type.create = &create_form_component<ComponentType>;
    return type;
}

// The reserved root type (owner decision of 2026-11-03, FD option 2): a
// form is the window a document describes. It is only ever the root, holds
// exactly one control (the window's root control) besides any components,
// and has the window's properties: `text` (the title), `client_size`,
// `accept_button` and `cancel_button` (each the stable ID of an object of
// the document). It has no interactions until the window's are
// described; the loader makes no object of it (LoadedForm::window).
inline constexpr std::string_view form_root_type_name = "form";

// The reserved form type's schema, for the caller to list with the
// registry's types.
[[nodiscard]] FormSchemaType form_root_type();

// An object's position in FormModel::objects.
struct ObjectIndex final {
    std::uint32_t value{};

    friend constexpr bool operator==(ObjectIndex left, ObjectIndex right) noexcept {
        return left.value == right.value;
    }
};

struct FormObject final {
    // Its type's position in FormSchema::types.
    std::size_t schema_type{};
    std::string stable_id{};
    // None for the root, object 0.
    std::optional<ObjectIndex> parent{};
    // In document order, which is child order (owner decision FD-2).
    std::vector<ObjectIndex> children{};
    theme_language::NodeIndex node{};
    // Its style block, if it has one.
    std::optional<theme_language::NodeIndex> style{};
};

struct FormProperty final {
    ObjectIndex object{};
    // The property's position in its type's FormSchemaType::properties.
    std::size_t schema_property{};
    theme_language::NodeIndex node{};
    // Of the property's kind, normalized by its descriptor.
    BindingValue value{};
};

struct FormHandler final {
    ObjectIndex object{};
    // The registered interaction name and the handler member's name.
    std::string interaction{};
    std::string handler{};
    theme_language::NodeIndex node{};
};

// Caller-owned: replaced whole by a build that succeeds, left as it was by
// one that refuses. Objects are in document order, each before its
// children; properties and handlers are in document order.
struct FormModel final {
    std::vector<FormObject> objects{};
    std::vector<FormProperty> properties{};
    std::vector<FormHandler> handlers{};
};

enum class FormModelOutcome : std::uint8_t {
    model_built,
    // At least one E8xx (or E999) error: every defect found is reported,
    // in document order, and `model` is unchanged.
    refused,
};

// Builds the model of the document `tree` describes. `tree` must be a
// complete parse of `source` as ThemeSheetForm::form_document, from
// `tokens` and `lines`; all of them and `schema` are borrowed for the call.
// Diagnostics are appended to `diagnostics`, with the object's stable ID as
// their rule and the property or handler as their property.
[[nodiscard]] FormModelOutcome build_form_model(const theme_language::ThemeSource& source,
                                                const theme_language::ThemeTokenBuffer& tokens,
                                                const theme_language::LineStartTable& lines,
                                                const theme_language::ThemeSyntaxTree& tree, const FormSchema& schema,
                                                FormModel& model, theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::form_language
