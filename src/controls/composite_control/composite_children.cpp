// The composite control's children and loading (cp34, phase 2): the
// children made from the definition's form document under the instance's
// stable ID, their bindings to the composite's properties, the rules on
// their interactions, and load_composite_definition.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "composite_forms.hpp"
#include "composite_internal.hpp"

#include "../../core/control_language/control_model/control_model.hpp"
#include "../../core/form_language/form_loader/form_loader.hpp"
#include "../../core/form_language/form_model/form_model.hpp"
#include "../../core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "gui_forms/controls/button_base.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms {

namespace {

using control_language::CompositeChildBinding;
using control_language::CompositeChildExpression;
using control_language::CompositeRuleDefinition;
using control_language::CompositeTrigger;

// Sets a flag for a scope and clears it on every exit.
class FlagScope final {
public:
    explicit FlagScope(bool& flag) noexcept : flag_(flag) { flag_ = true; }
    ~FlagScope() { flag_ = false; }
    FlagScope(const FlagScope&) = delete;
    FlagScope& operator=(const FlagScope&) = delete;

private:
    bool& flag_;
};

// A child's property change, for a two-way binding and for a
// `*-changed(@child)` rule. The subscriptions are held by the composite
// (child_subscriptions_), which they name as their owner, so neither
// outlives it.
struct CompositeChildChanged final {
    CompositeControl* control;
    std::size_t binding;
    void operator()() const { CompositeInstanceAccess::child_changed(*control, binding); }
};

struct CompositeChildRule final {
    CompositeControl* control;
    std::size_t rule;
    void operator()() const { CompositeInstanceAccess::child_property_fired(*control, rule); }
};

// An expression's value as a property write takes it; none for no value.
std::optional<BindingValue> binding_value_of(const CompositeValue& value) {
    switch (value.kind) {
    case CompositeValueKind::boolean: return BindingValue{value.boolean};
    case CompositeValueKind::number: return BindingValue{value.number};
    case CompositeValueKind::text:
    case CompositeValueKind::word: return BindingValue{value.text};
    case CompositeValueKind::none:
    default: return std::nullopt;
    }
}

// The stable ID a definition's child takes in an instance: the
// definition's prefix (`composite`) replaced by the instance's own.
std::string instance_stable_id(const std::string& written, std::string_view own) {
    const std::string_view prefix = control_language::composite_child_prefix;
    std::string made(own);
    if (written == prefix) return made;
    made.append(std::string_view(written).substr(prefix.size()));
    return made;
}

} // namespace

// -- children ----------------------------------------------------------------------

Control::Ptr CompositeControl::child(const std::string_view local_id) const {
    for (const MadeChild& made : children_) {
        if (made.local_id == local_id) return made.control;
    }
    return nullptr;
}

Control::Ptr CompositeControl::slot() const {
    if ((*definition_).slot.empty()) return nullptr;
    return child((*definition_).slot);
}

void CompositeControl::make_children() {
    const CompositeDefinition& definition = *definition_;
    if (definition.children.objects.empty()) return;
    // The definition's model under this instance's stable IDs: object 0 is
    // the wrapper (`composite`), the rest `composite.<local>`.
    form_language::FormModel model = definition.children;
    const std::string_view own = stable_id().value();
    for (form_language::FormObject& object : model.objects) {
        object.stable_id = instance_stable_id(object.stable_id, own);
    }
    theme_language::ThemeSource source{};
    source.file = definition.children_file;
    source.text = definition.children_document;
    const form_language::FormSources sources{};
    form_language::LoadedForm loaded{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const form_language::FormLoadOutcome outcome =
        form_language::load_form(source, definition.schema, model, sources, loaded, diagnostics);
    // The definition compiled this document against this schema, so a
    // refusal is a defect of the run time, not of the document.
    if (outcome != form_language::FormLoadOutcome::loaded || loaded.root == nullptr) {
        throw std::logic_error("CompositeControl: the definition's children could not be made");
    }
    children_.reserve(model.objects.size());
    for (std::size_t index = 1U; index < model.objects.size() && index < loaded.objects.size(); ++index) {
        const std::string& local = definition.local_ids[index];
        // A component child (one that is not a control) has no place in
        // the tree; phase 2 makes controls only.
        Control::Ptr control = std::dynamic_pointer_cast<Control>(loaded.objects[index]);
        if (local.empty() || control == nullptr) continue;
        MadeChild made{};
        made.local_id = local;
        made.control = std::move(control);
        made.schema_type = model.objects[index].schema_type;
        children_.push_back(std::move(made));
    }
    // The wrapper's top-level children move into the composite, in order.
    std::vector<Control::Ptr> top{};
    top.reserve((*loaded.root).child_count());
    for (const Control::Ptr& held : (*loaded.root).children()) top.push_back(held);
    for (const Control::Ptr& moved : top) {
        const Control::Ptr released = (*loaded.root).remove_child((*moved).runtime_id());
        if (released) add_child(released);
    }
    // Two-way bindings write back; command bindings wait for bind_command.
    for (std::size_t index = 0U; index < definition.child_bindings.size(); ++index) {
        const CompositeChildBinding& binding = definition.child_bindings[index];
        if (!binding.two_way || definition.properties[binding.source].command) continue;
        const Control::Ptr target = child(binding.child);
        if (!target) continue;
        child_subscriptions_.push_back(
            (*target).subscribe_property_changed(binding.property, *this, CompositeChildChanged{this, index}));
    }
    for (std::size_t index = 0U; index < definition.rules.size(); ++index) {
        const CompositeRuleDefinition& rule = definition.rules[index];
        if (rule.trigger != CompositeTrigger::child_property_changed) continue;
        const Control::Ptr target = child(rule.child);
        if (!target) continue;
        child_subscriptions_.push_back(
            (*target).subscribe_property_changed(rule.child_property, *this, CompositeChildRule{this, index}));
    }
    // A child's activation fires the `activated(@child)` rules; one
    // subscription per child that has any. A button's activation is its
    // clicked event, raised by a click, the keyboard, a mnemonic, a
    // semantic press and perform_click alike (its click event follows the
    // mouse only); another control's is its click.
    for (std::size_t index = 0U; index < children_.size(); ++index) {
        bool activates = false;
        for (const CompositeRuleDefinition& rule : definition.rules) {
            if (rule.trigger == CompositeTrigger::activated && rule.child == children_[index].local_id) activates = true;
        }
        if (!activates) continue;
        ButtonBase* const button = dynamic_cast<ButtonBase*>(children_[index].control.get());
        if (button != nullptr) {
            on((*button).clicked(), *this, &CompositeControl::child_activated, index);
        } else {
            on((*children_[index].control).click(), *this, &CompositeControl::child_activated, index);
        }
    }
}

void CompositeControl::write_bound_children(const std::optional<std::size_t> changed) {
    if (writing_children_) return;
    const FlagScope writing(writing_children_);
    const CompositeDefinition& definition = *definition_;
    for (const CompositeChildBinding& binding : definition.child_bindings) {
        if (changed && binding.source != *changed) continue;
        if (definition.properties[binding.source].command) continue;
        const Control::Ptr target = child(binding.child);
        if (!target) continue;
        // The child's own conversion decides; a refusal (its kind does not
        // take the value) changes nothing and is counted.
        try {
            (*target).set_property_value(binding.property, read_property(binding.source));
        } catch (const std::logic_error&) {
            // std::invalid_argument (no conversion) or a read-only property.
            ++statistics_.refused_writes;
        }
        if (!is_alive()) return;
    }
    // Expressions over self are evaluated again on every change: which
    // properties they read is not recorded.
    for (const CompositeChildExpression& expression : definition.child_expressions) {
        const Control::Ptr target = child(expression.child);
        if (!target) continue;
        const CompositeEvaluator evaluator(*this, CompositeFiring{});
        const std::optional<BindingValue> value = binding_value_of(evaluator.first(expression.expression));
        if (!value) continue;
        try {
            (*target).set_property_value(expression.property, *value);
        } catch (const std::logic_error&) {
            // std::invalid_argument (no conversion) or a read-only property.
            ++statistics_.refused_writes;
        }
        if (!is_alive()) return;
    }
}

void CompositeControl::child_changed(const std::size_t binding) {
    // A change the composite wrote does not flow back.
    if (writing_children_) return;
    const CompositeChildBinding& declared = (*definition_).child_bindings[binding];
    const Control::Ptr target = child(declared.child);
    if (!target) return;
    const std::optional<BindingValue> value = (*target).property_value(declared.property);
    if (!value) return;
    const FlagScope writing(writing_children_);
    static_cast<void>(write_property(declared.source, *value));
}

void CompositeControl::child_activated(const std::size_t child_index) {
    if (child_index >= children_.size()) return;
    const std::string local = children_[child_index].local_id;
    CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::activated, local, std::nullopt, 0.0);
}

void CompositeControl::child_property_fired(const std::size_t rule) {
    fire(rule, 0.0);
}

void CompositeInstanceAccess::child_changed(CompositeControl& control, const std::size_t binding) {
    control.child_changed(binding);
}

void CompositeInstanceAccess::child_property_fired(CompositeControl& control, const std::size_t rule) {
    control.child_property_fired(rule);
}

// -- loading -----------------------------------------------------------------------

std::string_view composite_type_name(const CompositeDefinition& definition) noexcept {
    return definition.registered;
}

CompositeLoadResult load_composite_definition(const std::string_view file, const std::string_view document,
                                              const std::span<const CompositeChildType> child_types) {
    // The schema: the base, then each child type described from a
    // prototype it makes and releases.
    form_language::FormSchema schema{};
    schema.types.reserve(child_types.size() + 1U);
    schema.types.push_back(form_language::form_control_type<UserControl>("user_control"));
    for (const CompositeChildType& type : child_types) {
        if (type.name.empty() || type.create == nullptr) {
            throw std::invalid_argument("load_composite_definition: a child type without a name or a creator");
        }
        const Component::Ptr prototype = (*type.create)(StableId(std::string("prototype")));
        if (prototype == nullptr) throw std::invalid_argument("load_composite_definition: a creator made nothing");
        form_language::FormSchemaType described = form_language::describe_form_type(std::string(type.name), *prototype);
        described.create = type.create;
        // A composite child (a generated class) takes the definition's
        // children into its slot, as a form's composite does.
        const CompositeControl* const nested = dynamic_cast<const CompositeControl*>(&*prototype);
        if (nested != nullptr) {
            const bool slotted = (*nested).slot() != nullptr;
            described.child_placement =
                slotted ? form_language::FormChildPlacement::slot : form_language::FormChildPlacement::refused;
            described.find_slot = &find_composite_slot;
        }
        if (type.bind_command != nullptr) {
            described.bind_command = type.bind_command;
            form_language::add_form_command_slot(described);
        }
        schema.types.push_back(std::move(described));
    }
    theme_language::ThemeSource source{};
    source.file = file;
    source.text = document;
    control_language::ControlSyntax syntax{};
    control_language::ControlModel model{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const control_language::ControlModelOutcome read =
        control_language::read_control_model(source, syntax, schema, model, diagnostics);
    CompositeLoadResult result{};
    if (read == control_language::ControlModelOutcome::model_built) {
        result.definition =
            control_language::compile_composite_definition(source, syntax, model, std::move(schema), diagnostics);
    }
    // One line per diagnostic, as the compilers print them.
    result.diagnostics.reserve(diagnostics.records().size());
    for (const theme_language::ThemeDiagnostic& record : diagnostics.records()) {
        const bool error =
            theme_language::diagnostic_severity(record.code) == theme_language::DiagnosticSeverity::error;
        std::string line = record.file;
        line += ':';
        line += std::to_string(record.position.line.value);
        line += ':';
        line += std::to_string(record.position.column.value);
        line += error ? ": error: " : ": warning: ";
        line += theme_language::diagnostic_code_label(record.code);
        line += ' ';
        line += theme_language::diagnostic_code_name(record.code);
        line += ": ";
        line += record.rule;
        if (!record.property.empty()) {
            line += ' ';
            line += record.property;
        }
        line += ": ";
        line += record.detail;
        result.diagnostics.push_back(std::move(line));
    }
    return result;
}

} // namespace gui_forms
