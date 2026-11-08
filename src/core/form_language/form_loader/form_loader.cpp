// A form document's run-time loader (stage FD, phase FD3). The run-time
// types' refusals arrive as exceptions; this is the subsystem boundary that
// turns each into a located diagnostic, and the scratch result is released
// when any arrived.

#include "form_loader.hpp"

#include <exception>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms::form_language {

namespace {

using theme_language::ByteOffset;
using theme_language::DiagnosticCode;
using theme_language::NodeIndex;
using theme_language::SourcePositionCursor;
using theme_language::ThemeDiagnostic;

class FormLoader final {
public:
    FormLoader(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
               const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
               const FormSchema& schema, const FormModel& model,
               theme_language::ThemeDiagnosticList& diagnostics) noexcept;

    // Fills `scratch`; returns whether nothing was refused.
    [[nodiscard]] bool run(LoadedForm& scratch);

private:
    void make_object(std::size_t index, LoadedForm& scratch);
    void set_property(const FormProperty& property, LoadedForm& scratch);
    void define_span(const FormSpan& span, LoadedForm& scratch);
    // A form root's property, which the window takes.
    static void set_window_property(const std::string& name, const BindingValue& value, LoadedWindow& window);
    void report(DiagnosticCode code, NodeIndex node, const std::string& rule, const std::string& property,
                std::string detail);

    const theme_language::ThemeSource& source_;
    const theme_language::ThemeTokenBuffer& tokens_;
    const theme_language::ThemeSyntaxTree& tree_;
    const FormSchema& schema_;
    const FormModel& model_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    // Whether the root is the reserved form type.
    bool form_root_{};
    bool refused_{};
};

FormLoader::FormLoader(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
                       const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
                       const FormSchema& schema, const FormModel& model,
                       theme_language::ThemeDiagnosticList& diagnostics) noexcept
    : source_(source), tokens_(tokens), tree_(tree), schema_(schema), model_(model), diagnostics_(diagnostics),
      cursor_(lines, source.text) {}

bool FormLoader::run(LoadedForm& scratch) {
    scratch.objects.assign(model_.objects.size(), Component::Ptr{});
    form_root_ = schema_.types[model_.objects[0].schema_type].name == form_root_type_name;
    if (form_root_) scratch.window = LoadedWindow{};
    // Parents come before their children in the model, so each parent is
    // made by the time its children are added.
    for (std::size_t index = 0U; index < model_.objects.size(); ++index) make_object(index, scratch);
    if (refused_) return false;
    // Spans before any placement, so that `area` and `cell` find them.
    for (const FormSpan& span : model_.spans) define_span(span, scratch);
    if (refused_) return false;
    for (const FormProperty& property : model_.properties) set_property(property, scratch);
    if (refused_) return false;
    if (form_root_) {
        // The model admits exactly one control among the form's children.
        for (const ObjectIndex child : model_.objects[0].children) {
            const Control::Ptr control = std::dynamic_pointer_cast<Control>(scratch.objects[child.value]);
            if (control) scratch.root = control;
        }
    } else {
        scratch.root = std::dynamic_pointer_cast<Control>(scratch.objects[0]);
    }
    if (!scratch.root) {
        report(DiagnosticCode::form_load_refused, model_.objects[0].node, model_.objects[0].stable_id, {},
               "the root is not a control");
        return false;
    }
    scratch.handlers = model_.handlers;
    return true;
}

void FormLoader::make_object(std::size_t index, LoadedForm& scratch) {
    const FormObject& object = model_.objects[index];
    const FormSchemaType& type = schema_.types[object.schema_type];
    // A form root is the window, which the caller owns.
    if (index == 0U && form_root_) return;
    if (type.create == nullptr) {
        report(DiagnosticCode::form_load_refused, object.node, object.stable_id, {}, type.name + " has no creator");
        return;
    }
    try {
        scratch.objects[index] = (*type.create)(StableId(object.stable_id));
    } catch (const std::exception& refusal) {
        report(DiagnosticCode::form_load_refused, object.node, object.stable_id, {}, refusal.what());
        return;
    }
    if (!object.parent) return;
    const Control::Ptr parent = std::dynamic_pointer_cast<Control>(scratch.objects[(*object.parent).value]);
    const Control::Ptr child = std::dynamic_pointer_cast<Control>(scratch.objects[index]);
    // A component is the form's, not its parent's: it stays in `objects`.
    // A parent that was refused has already been reported.
    if (!parent || !child) return;
    try {
        (*parent).add_child(child);
    } catch (const std::exception& refusal) {
        report(DiagnosticCode::form_load_refused, object.node, object.stable_id, {}, refusal.what());
    }
}

void FormLoader::define_span(const FormSpan& span, LoadedForm& scratch) {
    const FormObject& owner = model_.objects[span.owner.value];
    const FormSchemaType& type = schema_.types[owner.schema_type];
    const Component::Ptr& made = scratch.objects[span.owner.value];
    if (type.define_span == nullptr || !made) {
        report(DiagnosticCode::form_load_refused, span.node, owner.stable_id, span.span.name,
               type.name + " has no span definer");
        return;
    }
    GridSpanOutcome outcome = GridSpanOutcome::applied;
    try {
        outcome = (*type.define_span)(*made, span.span);
    } catch (const std::exception& refusal) {
        report(DiagnosticCode::form_load_refused, span.node, owner.stable_id, span.span.name, refusal.what());
        return;
    }
    if (outcome != GridSpanOutcome::applied) {
        report(DiagnosticCode::form_load_refused, span.node, owner.stable_id, span.span.name,
               "the container refuses the span");
    }
}

void FormLoader::set_property(const FormProperty& property, LoadedForm& scratch) {
    const FormObject& object = model_.objects[property.object.value];
    const PropertyDescriptor& descriptor = form_property_descriptor(schema_, model_, property);
    if (property.object.value == 0U && form_root_) {
        set_window_property(descriptor.name, property.value, *scratch.window);
        return;
    }
    const Control::Ptr control = std::dynamic_pointer_cast<Control>(scratch.objects[property.object.value]);
    if (!control) {
        report(DiagnosticCode::form_load_refused, property.node, object.stable_id, descriptor.name,
               "properties are set on controls only");
        return;
    }
    // A property the parent's type provides is the parent's to store; the
    // child was added to it when it was made.
    const Control::Ptr parent = property.provider == FormPropertyProvider::parent_type
        ? std::dynamic_pointer_cast<Control>(scratch.objects[(*object.parent).value])
        : Control::Ptr{};
    try {
        if (parent) {
            (*parent).set_child_property_value(*control, descriptor.name, property.value);
        } else {
            (*control).set_property_value(descriptor.name, property.value);
        }
    } catch (const std::exception& refusal) {
        report(DiagnosticCode::form_load_refused, property.node, object.stable_id, descriptor.name, refusal.what());
    }
}

void FormLoader::set_window_property(const std::string& name, const BindingValue& value, LoadedWindow& window) {
    if (name == "text") {
        window.title = std::get<std::string>(value);
    } else if (name == "client_size") {
        window.client_size = std::get<Size>(value);
    } else if (name == "accept_button") {
        window.accept_button = std::get<std::string>(value);
    } else {
        // The reserved type's last property: cancel_button.
        window.cancel_button = std::get<std::string>(value);
    }
}

void FormLoader::report(DiagnosticCode code, NodeIndex node, const std::string& rule, const std::string& property,
                        std::string detail) {
    const std::uint32_t token = tree_.nodes[node.value].tokens.first.value;
    const std::uint32_t offset = tokens_.tokens[token].offset.value;
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = cursor_.locate(ByteOffset{offset});
    diagnostic.rule = rule;
    diagnostic.property = property;
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    refused_ = true;
}

} // namespace

FormLoadOutcome load_form(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
                          const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
                          const FormSchema& schema, const FormModel& model, LoadedForm& loaded,
                          theme_language::ThemeDiagnosticList& diagnostics) {
    LoadedForm scratch{};
    FormLoader loader(source, tokens, lines, tree, schema, model, diagnostics);
    if (!loader.run(scratch)) return FormLoadOutcome::refused;
    loaded = std::move(scratch);
    return FormLoadOutcome::loaded;
}

} // namespace gui_forms::form_language
