// A form document's run-time loader (stage FD, phase FD3). The run-time
// types' refusals arrive as exceptions; this is the subsystem boundary that
// turns each into a located diagnostic, and the scratch result is released
// when any arrived.

#include "form_loader.hpp"

#include "gui_forms/binding/form_binding/form_binding.hpp"
#include "gui_forms/binding/property_table/property_table.hpp"

#include <exception>
#include <functional>
#include <span>
#include <stdexcept>
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

// The schema's table entry for an object-type property (audit S7), when it
// lies in the made control's own table; otherwise null and the property is
// written by name. The containment check uses std::less, which orders any
// two pointers.
const PropertyEntry* schema_table_entry(const FormSchema& schema, const FormModel& model,
                                        const FormProperty& property, const Control& control) noexcept {
    if (property.provider != FormPropertyProvider::object_type) return nullptr;
    const FormSchemaType& type = schema.types[model.objects[property.object.value].schema_type];
    if (property.schema_property >= type.property_entries.size()) return nullptr;
    const PropertyEntry* const entry = type.property_entries[property.schema_property];
    if (entry == nullptr) return nullptr;
    const std::span<const PropertyEntry> table = control.property_table();
    if (table.empty()) return nullptr;
    const std::less<const PropertyEntry*> before{};
    const PropertyEntry* const first = table.data();
    const PropertyEntry* const last = first + table.size();
    if (before(entry, first) || !before(entry, last)) return nullptr;
    return entry;
}

class FormLoader final {
public:
    FormLoader(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
               const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
               const FormSchema& schema, const FormModel& model, const FormSources& sources,
               theme_language::ThemeDiagnosticList& diagnostics) noexcept;

    // Fills `scratch`; returns whether nothing was refused.
    [[nodiscard]] bool run(LoadedForm& scratch);

private:
    void make_object(std::size_t index, LoadedForm& scratch);
    void set_property(const FormProperty& property, LoadedForm& scratch);
    // Binds a bound property of `control` (the object's own property: the
    // model binds no other) to its source.
    void bind_property(const FormProperty& property, const PropertyDescriptor& descriptor, Control& control);
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
    const FormSources& sources_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    SourcePositionCursor cursor_;
    // Whether the root is the reserved form type.
    bool form_root_{};
    bool refused_{};
};

FormLoader::FormLoader(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
                       const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
                       const FormSchema& schema, const FormModel& model, const FormSources& sources,
                       theme_language::ThemeDiagnosticList& diagnostics) noexcept
    : source_(source), tokens_(tokens), tree_(tree), schema_(schema), model_(model), sources_(sources),
      diagnostics_(diagnostics), cursor_(lines, source.text) {}

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
    if (property.binding) {
        bind_property(property, descriptor, *control);
        return;
    }
    const Control::Ptr parent = property.provider == FormPropertyProvider::parent_type
        ? std::dynamic_pointer_cast<Control>(scratch.objects[(*object.parent).value])
        : Control::Ptr{};
    // The model resolved the property against the schema; an own property
    // with a table entry is written through it (audit S7).
    const PropertyEntry* const entry = parent ? nullptr : schema_table_entry(schema_, model_, property, *control);
    try {
        if (parent) {
            (*parent).set_child_property_value(*control, descriptor.name, property.value);
        } else if (entry != nullptr) {
            write_property_entry(*control, *entry, property.value);
        } else {
            (*control).set_property_value(descriptor.name, property.value);
        }
    } catch (const std::exception& refusal) {
        report(DiagnosticCode::form_load_refused, property.node, object.stable_id, descriptor.name, refusal.what());
    }
}

void FormLoader::bind_property(const FormProperty& property, const PropertyDescriptor& descriptor,
                               Control& control) {
    const FormObject& object = model_.objects[property.object.value];
    const FormBinding& binding = *property.binding;
    const FormSourceReference* found = sources_.find(binding.source);
    if (found == nullptr) {
        report(DiagnosticCode::form_binding_source_refused, property.node, object.stable_id, descriptor.name,
               "no source named " + binding.source);
        return;
    }
    const FormSchemaType& type = schema_.types[object.schema_type];
    const bool command_slot = type.bind_command != nullptr && descriptor.name == form_command_slot_name;
    // The source's alternative the binding needs, by its form.
    bool matched = false;
    try {
        if (command_slot) {
            Command* const* command = std::get_if<Command*>(found);
            matched = command != nullptr;
            if (matched) (*type.bind_command)(control, **command);
        } else if (!binding.field.empty()) {
            const std::shared_ptr<BindingSource>* records = std::get_if<std::shared_ptr<BindingSource>>(found);
            matched = records != nullptr;
            if (matched) {
                bind_form_field(control, descriptor.name, *records, binding.field, binding.direction,
                                binding.trigger);
            }
        } else if (descriptor.kind == BindingValueKind::boolean) {
            Value<bool>* const* scalar = std::get_if<Value<bool>*>(found);
            matched = scalar != nullptr;
            if (matched) bind_form_value(control, descriptor.name, **scalar, binding.direction);
        } else if (descriptor.kind == BindingValueKind::signed_integer) {
            Value<int>* const* scalar = std::get_if<Value<int>*>(found);
            matched = scalar != nullptr;
            if (matched) bind_form_value(control, descriptor.name, **scalar, binding.direction);
        } else {
            // The model admits a Value source only for the three scalar
            // kinds; this is the number's.
            Value<double>* const* scalar = std::get_if<Value<double>*>(found);
            matched = scalar != nullptr;
            if (matched) bind_form_value(control, descriptor.name, **scalar, binding.direction);
        }
    } catch (const std::exception& refusal) {
        report(DiagnosticCode::form_binding_source_refused, property.node, object.stable_id, descriptor.name,
               refusal.what());
        return;
    }
    if (!matched) {
        report(DiagnosticCode::form_binding_source_refused, property.node, object.stable_id, descriptor.name,
               "the source " + binding.source + " is not of the kind the property binds");
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

void FormSources::add(std::string name, Value<bool>& source) {
    add_entry(std::move(name), FormSourceReference{&source});
}

void FormSources::add(std::string name, Value<int>& source) {
    add_entry(std::move(name), FormSourceReference{&source});
}

void FormSources::add(std::string name, Value<double>& source) {
    add_entry(std::move(name), FormSourceReference{&source});
}

void FormSources::add(std::string name, Command& source) {
    add_entry(std::move(name), FormSourceReference{&source});
}

void FormSources::add(std::string name, std::shared_ptr<BindingSource> source) {
    if (!source) throw std::invalid_argument("FormSources: a null BindingSource");
    add_entry(std::move(name), FormSourceReference{std::move(source)});
}

const FormSourceReference* FormSources::find(std::string_view name) const noexcept {
    for (const NamedFormSource& entry : entries_) {
        if (entry.name == name) return &entry.source;
    }
    return nullptr;
}

void FormSources::add_entry(std::string name, FormSourceReference source) {
    bool identifier = !name.empty();
    for (std::size_t index = 0U; identifier && index < name.size(); ++index) {
        const char character = name[index];
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                            character == '_';
        const bool digit = character >= '0' && character <= '9';
        identifier = letter || (digit && index != 0U);
    }
    if (!identifier) throw std::invalid_argument("FormSources: a source's name is one C++ identifier");
    if (find(name) != nullptr) throw std::invalid_argument("FormSources: a source's name is added twice");
    NamedFormSource entry{};
    entry.name = std::move(name);
    entry.source = std::move(source);
    entries_.push_back(std::move(entry));
}

FormLoadOutcome load_form(const theme_language::ThemeSource& source, const theme_language::ThemeTokenBuffer& tokens,
                          const theme_language::LineStartTable& lines, const theme_language::ThemeSyntaxTree& tree,
                          const FormSchema& schema, const FormModel& model, const FormSources& sources,
                          LoadedForm& loaded, theme_language::ThemeDiagnosticList& diagnostics) {
    // A built model has its root object; an empty one (a refused or never
    // built model) is the caller's error, as generate_form_cpp says (audit
    // P16).
    if (model.objects.empty()) throw std::invalid_argument("load_form: the model has no objects");
    LoadedForm scratch{};
    FormLoader loader(source, tokens, lines, tree, schema, model, sources, diagnostics);
    if (!loader.run(scratch)) return FormLoadOutcome::refused;
    loaded = std::move(scratch);
    return FormLoadOutcome::loaded;
}

} // namespace gui_forms::form_language
