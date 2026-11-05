#pragma once

// A form document's model (stage FD, phase FD2): the objects, properties
// and handler references a parsed `.form` document describes, checked
// against a form schema before anything is built. Internal until stage D
// (owner decision FD-6).
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
// - font (owner decision FD6-3): `<role> <size> <weight> [italic]
//   [spacing <n>]`, the role control, content or monospace, a positive
//   size, a weight from 1 to 1000, a letter spacing in logical pixels
//   (`font: control 12 700 italic;`, `font: control 12 700 spacing 0.24;`);
// - collection (owner decision FD6-4): one or more strings, for a property
//   whose default is a text collection (`items: "AM" "FM";`);
// - image, object and other collections: not yet expressible (E999).
// A handler reference, `on-<interaction>: <handler>;`, names a member of
// the owner class (owner decision FD-3): one C++ identifier.
//
// Spans (study 10.9): directly inside an object whose type holds spans
// (FormSchemaType::holds_spans), `span [name] { anchor: <row> <column>;
// rows: <n>; columns: <n>; }` declares one region of its grid. The reserved
// type `span` is no object: its name is the span's, in the container's
// namespace and not a stable ID, and it may be omitted (an anonymous span,
// placed into by its anchor). `rows` and `columns` default to 1; `anchor`
// is required. The children write `area: "<name>"` or `cell: <row>
// <column>`, the child properties such a type provides.

#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../../theme_language/tokenizer/theme_tokenizer.hpp"

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/layout/grid_span/grid_span.hpp"

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

// Defines a span on an object of a type that holds spans, for the loader:
// a named function per type, made from the template below.
using FormSpanDefiner = GridSpanOutcome (*)(Component& owner, const GridSpan& span);

// One type a document may name, as the run-time registry describes it.
struct FormSchemaType final {
    // The registered name: `check_box`.
    std::string name{};
    // Whether objects of the type may hold objects: controls do, other
    // components do not.
    bool holds_children{};
    std::vector<PropertyDescriptor> properties{};
    // The properties the type provides to its direct children, which each
    // child writes in its own block (attached properties: a table cell, a
    // flow break). No name here is also a property of every control.
    std::vector<PropertyDescriptor> child_properties{};
    // Whether objects of the type hold `span` blocks (HoldsGridSpans), and
    // how the loader defines one.
    bool holds_spans{};
    FormSpanDefiner define_span{};
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

// The span definer of a type that holds spans.
template <typename ControlType>
[[nodiscard]] GridSpanOutcome define_form_span(Component& owner, const GridSpan& span) {
    ControlType& control = static_cast<ControlType&>(owner);
    const GridSpanOutcome outcome = control.define_span(span);
    return outcome;
}

// A control or component type described from a fresh prototype, with its
// creator (and its span definer, for a type that holds spans).
template <typename ControlType>
[[nodiscard]] FormSchemaType form_control_type(std::string name) {
    const std::shared_ptr<ControlType> prototype = make_control<ControlType>(StableId("prototype"));
    FormSchemaType type = describe_form_type(std::move(name), *prototype);
    type.create = &create_form_control<ControlType>;
    if constexpr (HoldsGridSpans<ControlType>::value) {
        type.holds_spans = true;
        type.define_span = &define_form_span<ControlType>;
    }
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

// The reserved type of a span block, admitted only directly inside an
// object whose type holds spans.
inline constexpr std::string_view form_span_type_name = "span";

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
};

// Which type declares a written property.
enum class FormPropertyProvider : std::uint8_t {
    // The object's own type: FormSchemaType::properties.
    object_type,
    // The type of the object's parent, which provides it to its children:
    // FormSchemaType::child_properties.
    parent_type,
};

struct FormProperty final {
    ObjectIndex object{};
    FormPropertyProvider provider{FormPropertyProvider::object_type};
    // The property's position in its provider's list (see provider).
    std::size_t schema_property{};
    theme_language::NodeIndex node{};
    // Of the property's kind, normalized by its descriptor.
    BindingValue value{};
};

// One span block: its container, its value and its node.
struct FormSpan final {
    ObjectIndex owner{};
    GridSpan span{};
    theme_language::NodeIndex node{};
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
// children; spans, properties and handlers are in document order.
struct FormModel final {
    std::vector<FormObject> objects{};
    std::vector<FormSpan> spans{};
    std::vector<FormProperty> properties{};
    std::vector<FormHandler> handlers{};
};

enum class FormModelOutcome : std::uint8_t {
    model_built,
    // At least one E8xx (or E999) error: every defect found is reported,
    // in document order, and `model` is unchanged.
    refused,
};

// The descriptor of a property `model` records, from its provider's list
// in `schema`, the schema the model was built against. The reference is
// valid while `schema` is unchanged.
[[nodiscard]] const PropertyDescriptor& form_property_descriptor(const FormSchema& schema, const FormModel& model,
                                                                 const FormProperty& property);

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
