// The composite control (cp34, phase 2): properties, geometry, painting,
// input, rules, commands, drags and semantics over a compiled definition.
// The children and loading are in composite_children.cpp, the
// expressions in composite_evaluator.cpp.

#include "gui_forms/composites/composite_control/composite_control.hpp"

#include "composite_internal.hpp"

#include "gui_forms/controls/scroll_bar_metrics.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms {

namespace {

using control_language::CompositeActionDefinition;
using control_language::CompositeDragDefinition;
using control_language::CompositeExpression;
using control_language::CompositePartDefinition;
using control_language::CompositePropertyDefinition;
using control_language::CompositeRuleDefinition;
using control_language::CompositeTrigger;
using control_language::ControlActionKind;
using control_language::ControlDragMode;
using control_language::ControlKeyPhase;
using control_language::ControlPartHit;
using control_language::ControlPartShape;
using control_language::ControlPress;
using control_language::ControlPropertyKind;
using control_language::ExpressionNode;
using control_language::ExpressionNodeIndex;
using control_language::ExpressionNodeKind;

// Named apart from Control::validated() (the event), which a call inside a
// member would find first.
std::shared_ptr<const CompositeDefinition> required_definition(std::shared_ptr<const CompositeDefinition> definition) {
    if (definition == nullptr) throw std::invalid_argument("CompositeControl: a null definition");
    return definition;
}

bool pressing(const CompositePartDefinition& part, const ControlPress press) noexcept {
    const bool set = (part.press & static_cast<std::uint8_t>(press)) != 0U;
    return set;
}

PartShape shape_of(const CompositePartDefinition& part) noexcept {
    PartShape shape{};
    switch (part.shape) {
    case ControlPartShape::pill: shape.kind = PartShapeKind::pill; break;
    case ControlPartShape::circle: shape.kind = PartShapeKind::circle; break;
    case ControlPartShape::cap_start: shape.kind = PartShapeKind::cap_start; break;
    case ControlPartShape::cap_end: shape.kind = PartShapeKind::cap_end; break;
    case ControlPartShape::rounded_rectangle:
    case ControlPartShape::unspecified:
    default: shape.kind = PartShapeKind::rounded_rectangle; break;
    }
    if (!part.corner_radius.empty()) shape.corner_radius = part.corner_radius[0];
    return shape;
}

// The radius a part is painted with: its declared radius, or the shape's
// for a pill or a circle (the recipe has no shape kinds of its own yet).
stxlib::Optional<double> paint_radius(const CompositePartDefinition& part, const Rect box) noexcept {
    if (!part.corner_radius.empty()) return stxlib::Optional<double>::of(part.corner_radius[0]);
    const bool round = part.shape == ControlPartShape::pill || part.shape == ControlPartShape::circle ||
                       part.shape == ControlPartShape::cap_start || part.shape == ControlPartShape::cap_end;
    if (!round) return stxlib::Optional<double>::none();
    const stxlib::Optional<double> radius = stxlib::Optional<double>::of(std::min(box.width, box.height) * 0.5);
    return radius;
}

bool text_role(const PartRole role) noexcept {
    const bool shows = role == PartRole::label || role == PartRole::caption || role == PartRole::content;
    return shows;
}

// Restores a counter on every exit from a scope.
// Lends the states being decided to nested reads for a scope, and takes them
// back on every exit.
class DecidingStatesScope final {
public:
    DecidingStatesScope(AnatomyStateSet*& deciding, AnatomyStateSet& states) noexcept : deciding_(deciding) {
        deciding_ = &states;
    }
    ~DecidingStatesScope() { deciding_ = nullptr; }
    DecidingStatesScope(const DecidingStatesScope&) = delete;
    DecidingStatesScope& operator=(const DecidingStatesScope&) = delete;

private:
    AnatomyStateSet*& deciding_;
};

class DepthScope final {
public:
    explicit DepthScope(std::size_t& depth) noexcept : depth_(depth) { ++depth_; }
    ~DepthScope() { --depth_; }
    DepthScope(const DepthScope&) = delete;
    DepthScope& operator=(const DepthScope&) = delete;

private:
    std::size_t& depth_;
};

// The registration's operations: named functions over the control and a
// property's index (PropertyRegistration's callables).
struct CompositePropertyGetter final {
    const CompositeControl* control;
    std::size_t index;
    BindingValue operator()() const { return CompositeInstanceAccess::read(*control, index); }
};

struct CompositePropertySetter final {
    CompositeControl* control;
    std::size_t index;
    void operator()(const BindingValue& value) const { CompositeInstanceAccess::write(*control, index, value); }
};

// An alias's change notification is its child property's.
struct AliasChangeConnector final {
    const CompositeControl* control;
    std::string_view child;
    std::string_view property;
    SubscriptionToken operator()(Component& owner, std::function<void()> changed) const {
        const Control::Ptr target = (*control).child(child);
        if (!target) return SubscriptionToken{};
        SubscriptionToken token = (*target).subscribe_property_changed(property, owner, std::move(changed));
        return token;
    }
};

// The repetition of a held part (the scroll bar's timing), observing the
// control so a destroyed one stops it.
struct CompositeRepeatTick final {
    std::weak_ptr<CompositeControl> control;
    void operator()(FrameTime now) const {
        static_cast<void>(now);
        const std::shared_ptr<CompositeControl> live = control.lock();
        if (live) CompositeInstanceAccess::repeat(*live);
    }
};

} // namespace

// -- construction and properties ---------------------------------------------------

CompositeControl::CompositeControl(StableId stable_id, std::shared_ptr<const CompositeDefinition> definition)
    : UserControl(std::move(stable_id)), definition_(required_definition(std::move(definition))) {
    const std::size_t count = (*definition_).properties.size();
    values_.reserve(count);
    changed_events_.reserve(count);
    for (const CompositePropertyDefinition& property : (*definition_).properties) {
        values_.push_back(property.initial);
        std::unique_ptr<Event<>> changed{};
        if (!property.presentational && !property.command && property.alias_child.empty()) {
            changed = std::make_unique<Event<>>();
        }
        changed_events_.push_back(std::move(changed));
    }
    commands_.assign(count, nullptr);
    // The definition compiled its look once, for every instance.
    statistics_.look_compiles = (*definition_).look_compiles;
    register_properties();
    if ((*definition_).focus.target == control_language::ControlFocusTarget::self) focusable = true;
    if (!(*definition_).drags.empty()) {
        drag_ = std::make_unique<DragTracker>(*this);
        (*drag_).callbacks.plan = Delegate<DragPlan&>::bind<CompositeControl, &CompositeControl::plan_drag>(*this);
        (*drag_).callbacks.preview =
            Delegate<const DragTrackerFrame&>::bind<CompositeControl, &CompositeControl::preview_drag>(*this);
        (*drag_).callbacks.finished =
            Delegate<const DragTrackerFrame&>::bind<CompositeControl, &CompositeControl::finish_drag>(*this);
    }
}

CompositeControl::~CompositeControl() = default;

void CompositeControl::initialize_control_tree() {
    make_children();
    write_bound_children(stxlib::Optional<std::size_t>::none());
    apply_look();
}

// The definition's look on the composite and on the controls its
// definition made, walked over an explicit stack (the depth rule). Each
// holds it through an alias of the definition, which the look's anatomies
// borrow. A nested composite that set its own look keeps it, with its
// subtree; slot content is added later and takes none.
void CompositeControl::apply_look() {
    detail::CompositeLook* const compiled = (*definition_).compiled_look.get();
    if (compiled == nullptr) return;
    const std::shared_ptr<detail::CompositeLook> look(definition_, compiled);
    set_composite_look(look);
    std::vector<Control*> pending{};
    for (const Control::Ptr& child : children()) pending.push_back(child.get());
    while (!pending.empty()) {
        Control* const current = pending.back();
        pending.pop_back();
        if (current == nullptr || (*current).has_composite_look()) continue;
        (*current).set_composite_look(look);
        for (const Control::Ptr& child : (*current).children()) pending.push_back(child.get());
    }
}

const CompositeStatistics& CompositeControl::statistics() const noexcept {
    // The look's counts are its definition's, read when asked.
    const detail::CompositeLook* const look = (*definition_).compiled_look.get();
    if (look != nullptr) {
        statistics_.look_theme_compiles = (*look).theme_compiles();
        statistics_.look_theme_refusals = (*look).theme_refusals();
        statistics_.look_theme_reuses = (*look).theme_reuses();
        statistics_.look_missing = (*look).missing();
    }
    return statistics_;
}

std::string_view CompositeControl::type_name() const noexcept {
    return (*definition_).registered;
}

void CompositeControl::register_properties() {
    // Presentational and command properties are not registered: they are
    // not values a binding, a form or the property grid reaches.
    for (std::size_t index = 0U; index < (*definition_).properties.size(); ++index) {
        const CompositePropertyDefinition& property = (*definition_).properties[index];
        if (property.presentational || property.command) continue;
        PropertyRegistration registration{};
        registration.descriptor = property.descriptor;
        registration.get = CompositePropertyGetter{this, index};
        registration.set = CompositePropertySetter{this, index};
        if (!property.alias_child.empty()) {
            registration.connect_changed = AliasChangeConnector{this, property.alias_child, property.alias_property};
        } else {
            registration.connect_changed = detail::EventChangeConnector<>(*changed_events_[index]);
        }
        define_bindable_property(std::move(registration));
    }
}

BindingValue CompositeControl::read_property(const std::size_t index) const {
    const CompositePropertyDefinition& property = (*definition_).properties[index];
    if (!property.alias_child.empty()) {
        const Control::Ptr target = child(property.alias_child);
        if (!target) return property.descriptor.default_value.value_or(BindingValue{});
        const std::optional<BindingValue> read = (*target).property_value(property.alias_property);
        return read.value_or(BindingValue{});
    }
    return values_[index];
}

CompositeControl::WriteOutcome CompositeControl::write_property(const std::size_t index, const BindingValue& value) {
    const CompositePropertyDefinition& property = (*definition_).properties[index];
    if (property.command) {
        ++statistics_.refused_writes;
        return WriteOutcome::refused;
    }
    if (!property.alias_child.empty()) {
        const Control::Ptr target = child(property.alias_child);
        if (!target) {
            ++statistics_.refused_writes;
            return WriteOutcome::refused;
        }
        (*target).set_property_value(property.alias_property, value);
        return WriteOutcome::written;
    }
    BindingValue stored{};
    if (property.presentational && std::holds_alternative<std::monostate>(value)) {
        stored = value;
    } else {
        const std::optional<BindingValue> converted = convert_property_value(value, property.descriptor);
        if (!converted) {
            ++statistics_.refused_writes;
            return WriteOutcome::refused;
        }
        stored = *converted;
    }
    // A number with a range is clamped to it (design note 3.6).
    if (!property.range.empty() && property.range.parsed.terms.size() == 2U) {
        if (double* number = std::get_if<double>(&stored)) {
            const CompositeEvaluator evaluator(*this, CompositeFiring{});
            const double low = evaluator.evaluate(property.range, property.range.parsed.terms[0]).number;
            const double high = evaluator.evaluate(property.range, property.range.parsed.terms[1]).number;
            if (low <= high) *number = std::clamp(*number, low, high);
        }
    }
    if (stored == values_[index]) return WriteOutcome::unchanged;
    values_[index] = std::move(stored);
    property_changed(index);
    return WriteOutcome::written;
}

void CompositeControl::property_changed(const std::size_t index) {
    const CompositePropertyDefinition& property = (*definition_).properties[index];
    invalidate_geometry();
    invalidate(property.affects | Dirty::paint | Dirty::hit_test);
    if (property.presentational) return;
    publish_change(*changed_events_[index]);
    if (!is_alive()) return;
    if (property.changed_event) {
        const control_language::CompositeEventDefinition& event = (*definition_).events[*property.changed_event];
        raise(*property.changed_event, event.payload ? values_[index] : BindingValue{});
        if (!is_alive()) return;
    }
    CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::changed, {}, index, 0.0);
    if (!is_alive()) return;
    write_bound_children(stxlib::Optional<std::size_t>::of(index));
}

void CompositeControl::raise(const std::size_t event, BindingValue payload) {
    CompositeEvent raised{};
    raised.name = (*definition_).events[event].name;
    raised.payload = std::move(payload);
    raised_.emit(raised);
}

BindingValue CompositeControl::presentational_value(const std::string_view name) const {
    for (std::size_t index = 0U; index < (*definition_).properties.size(); ++index) {
        const CompositePropertyDefinition& property = (*definition_).properties[index];
        if (property.name == name && property.presentational) return values_[index];
    }
    return BindingValue{};
}

void CompositeControl::bind_command(const std::string_view name, Command* command) {
    for (std::size_t index = 0U; index < (*definition_).properties.size(); ++index) {
        const CompositePropertyDefinition& property = (*definition_).properties[index];
        if (property.name != name) continue;
        if (!property.command) throw std::invalid_argument("CompositeControl::bind_command: not a command property");
        commands_[index] = command;
        if (command == nullptr) return;
        // The children that bind it take the command (a button's
        // bind(Command&), through their schema type's binder).
        for (const control_language::CompositeChildBinding& binding : (*definition_).child_bindings) {
            if (binding.source != index) continue;
            for (const MadeChild& made : children_) {
                if (made.local_id != binding.child) continue;
                const form_language::FormSchemaType& type = (*definition_).schema.types[made.schema_type];
                if (type.bind_command != nullptr) (*type.bind_command)(*made.control, *command);
            }
        }
        return;
    }
    throw std::invalid_argument("CompositeControl::bind_command: no such property");
}

// -- commands and rules ------------------------------------------------------------

bool CompositeControl::command_enabled_at(const std::size_t command) const {
    if (!effectively_enabled()) return false;
    const CompositeEvaluator evaluator(*this, CompositeFiring{});
    return evaluator.holds((*definition_).commands[command].enabled);
}

bool CompositeControl::command_enabled(const std::string_view name) const {
    for (std::size_t index = 0U; index < (*definition_).commands.size(); ++index) {
        if ((*definition_).commands[index].name == name) return command_enabled_at(index);
    }
    return false;
}

bool CompositeControl::run_command(const std::size_t command) {
    if (!command_enabled_at(command)) {
        ++statistics_.commands_refused;
        return false;
    }
    ++statistics_.commands_run;
    CompositeInstanceAccess::run_calls(*this, (*definition_).commands[command].run, CompositeFiring{});
    return true;
}

bool CompositeControl::perform(const std::string_view name) {
    for (std::size_t index = 0U; index < (*definition_).commands.size(); ++index) {
        if ((*definition_).commands[index].name == name) return run_command(index);
    }
    return false;
}

void CompositeControl::fire(const std::size_t rule, const double wheel_notches) {
    const CompositeRuleDefinition& declared = (*definition_).rules[rule];
    if (firing_depth_ >= maximum_composite_rule_depth) {
        ++statistics_.refused_firings;
        return;
    }
    const DepthScope depth(firing_depth_);
    CompositeFiring firing{};
    firing.wheel_notches = wheel_notches;
    const CompositeEvaluator evaluator(*this, firing);
    if (!evaluator.holds(declared.when)) return;
    ++statistics_.rule_firings;
    CompositeInstanceAccess::run_actions(*this, declared.first_action, declared.action_count, firing);
}

void CompositeInstanceAccess::fire_trigger(CompositeControl& control, const CompositeTrigger trigger,
                                           const std::string_view child, const std::optional<std::size_t> property,
                                           const double wheel_notches) {
    const CompositeDefinition& definition = *control.definition_;
    for (std::size_t index = 0U; index < definition.rules.size(); ++index) {
        const CompositeRuleDefinition& rule = definition.rules[index];
        if (rule.trigger != trigger || rule.child != child) continue;
        if (trigger == CompositeTrigger::changed && rule.property != property) continue;
        control.fire(index, wheel_notches);
        if (!control.is_alive()) return;
    }
}

std::optional<BindingValue> CompositeInstanceAccess::property_value_of(const CompositeControl& control,
                                                                       const std::size_t index,
                                                                       const CompositeValue& value) {
    const CompositePropertyDefinition& property = (*control.definition_).properties[index];
    switch (property.kind) {
    case ControlPropertyKind::boolean: return BindingValue{CompositeEvaluator::truthy(value)};
    case ControlPropertyKind::number:
    case ControlPropertyKind::signed_integer:
    case ControlPropertyKind::unsigned_integer: {
        if (value.kind != CompositeValueKind::number && value.kind != CompositeValueKind::boolean) {
            return std::nullopt;
        }
        const double number = value.kind == CompositeValueKind::boolean ? (value.boolean ? 1.0 : 0.0) : value.number;
        return convert_property_value(BindingValue{number}, property.descriptor);
    }
    case ControlPropertyKind::text:
        if (value.kind == CompositeValueKind::text || value.kind == CompositeValueKind::word) {
            return BindingValue{value.text};
        }
        return std::nullopt;
    case ControlPropertyKind::enumeration: {
        if (value.kind != CompositeValueKind::word && value.kind != CompositeValueKind::text) return std::nullopt;
        if (!property.descriptor.enumeration) return std::nullopt;
        for (const PropertyEnumChoice& choice : (*property.descriptor.enumeration).choices) {
            if (choice.name != value.text) continue;
            PropertyEnumValue chosen{};
            chosen.type_name = (*property.descriptor.enumeration).type_name;
            chosen.name = choice.name;
            chosen.value = choice.value;
            return BindingValue{chosen};
        }
        return std::nullopt;
    }
    // The other kinds hold no binding value; only a presentational number passes through.
    default:
        if (property.presentational && value.kind == CompositeValueKind::number) return BindingValue{value.number};
        return std::nullopt;
    }
}

void CompositeInstanceAccess::run_actions(CompositeControl& control, const std::size_t first, const std::size_t count,
                                          const CompositeFiring firing) {
    const CompositeDefinition& definition = *control.definition_;
    for (std::size_t offset = 0U; offset < count; ++offset) {
        // Disposed inside a firing: the remaining actions are dropped
        // (ADR-003 rule 5).
        if (!control.is_alive()) return;
        const CompositeActionDefinition& action = definition.actions[first + offset];
        const CompositeEvaluator evaluator(control, firing);
        switch (action.kind) {
        case ControlActionKind::set: {
            const std::optional<BindingValue> value =
                property_value_of(control, action.target, evaluator.first(action.value));
            if (value) static_cast<void>(control.write_property(action.target, *value));
            break;
        }
        case ControlActionKind::toggle: {
            const BindingValue current = control.read_property(action.target);
            const bool* truth = std::get_if<bool>(&current);
            if (truth != nullptr) static_cast<void>(control.write_property(action.target, BindingValue{!*truth}));
            break;
        }
        case ControlActionKind::step: {
            const CompositeValue current = CompositeEvaluator::value_of(control.read_property(action.target));
            CompositeValue stepped{};
            stepped.kind = CompositeValueKind::number;
            stepped.number = current.number + evaluator.number(action.value);
            const std::optional<BindingValue> value = property_value_of(control, action.target, stepped);
            if (value) static_cast<void>(control.write_property(action.target, *value));
            break;
        }
        case ControlActionKind::run: static_cast<void>(control.run_command(action.target)); break;
        case ControlActionKind::raise: control.raise(action.target, BindingValue{}); break;
        case ControlActionKind::focus: {
            const BorrowedPointer<SurfaceRoot> root = control.surface_root();
            if (root == nullptr) break;
            const Control::Ptr target = action.child.empty() ? control.shared_from_this() : control.child(action.child);
            if (target) static_cast<void>((*root).request_focus(target));
            break;
        }
        case ControlActionKind::invoke: {
            Command* const command = control.commands_[action.target];
            if (command != nullptr) static_cast<void>((*command).execute(control.stable_id().value()));
            break;
        }
        case ControlActionKind::open:
        case ControlActionKind::close:
        default:
            // Refused by the definition compiler.
            break;
        }
    }
}

void CompositeInstanceAccess::run_calls(CompositeControl& control, const CompositeExpression& calls,
                                        const CompositeFiring firing) {
    const CompositeDefinition& definition = *control.definition_;
    for (const ExpressionNodeIndex term : calls.parsed.terms) {
        if (!control.is_alive()) return;
        const ExpressionNode& node = calls.parsed.nodes[term.value];
        if (node.kind != ExpressionNodeKind::call || node.argument_count == 0U) continue;
        const ExpressionNode& named = calls.parsed.nodes[calls.parsed.arguments[node.first_argument].value];
        const std::string_view target = named.kind == ExpressionNodeKind::self_reference ? std::string_view(named.member)
                                                                                         : std::string_view(named.text);
        const CompositeEvaluator evaluator(control, firing);
        std::optional<std::size_t> property{};
        for (std::size_t index = 0U; index < definition.properties.size(); ++index) {
            if (definition.properties[index].name == target) property = index;
        }
        const ExpressionNodeIndex value_node =
            node.argument_count >= 2U ? calls.parsed.arguments[node.first_argument + 1U] : ExpressionNodeIndex{};
        if (node.text == "set" && property && node.argument_count >= 2U) {
            const std::optional<BindingValue> value =
                property_value_of(control, *property, evaluator.evaluate(calls, value_node));
            if (value) static_cast<void>(control.write_property(*property, *value));
        } else if (node.text == "toggle" && property) {
            const BindingValue current = control.read_property(*property);
            const bool* truth = std::get_if<bool>(&current);
            if (truth != nullptr) static_cast<void>(control.write_property(*property, BindingValue{!*truth}));
        } else if (node.text == "step" && property && node.argument_count >= 2U) {
            const CompositeValue current = CompositeEvaluator::value_of(control.read_property(*property));
            CompositeValue stepped{};
            stepped.kind = CompositeValueKind::number;
            stepped.number = current.number + evaluator.evaluate(calls, value_node).number;
            const std::optional<BindingValue> value = property_value_of(control, *property, stepped);
            if (value) static_cast<void>(control.write_property(*property, *value));
        } else if (node.text == "run") {
            for (std::size_t index = 0U; index < definition.commands.size(); ++index) {
                if (definition.commands[index].name == target) static_cast<void>(control.run_command(index));
            }
        } else if (node.text == "raise") {
            for (std::size_t index = 0U; index < definition.events.size(); ++index) {
                if (definition.events[index].name == target) control.raise(index, BindingValue{});
            }
        } else if (node.text == "focus") {
            const BorrowedPointer<SurfaceRoot> root = control.surface_root();
            const std::string_view local = target.starts_with("@") ? target.substr(1U) : target;
            const Control::Ptr focused = local == "self" ? control.shared_from_this() : control.child(local);
            if (root != nullptr && focused) static_cast<void>((*root).request_focus(focused));
        }
    }
}

BindingValue CompositeInstanceAccess::read(const CompositeControl& control, const std::size_t index) {
    BindingValue value = control.read_property(index);
    return value;
}

void CompositeInstanceAccess::write(CompositeControl& control, const std::size_t index, const BindingValue& value) {
    if (control.write_property(index, value) == CompositeControl::WriteOutcome::refused) {
        throw std::invalid_argument("the composite's property refuses the value");
    }
}

// -- geometry and painting ---------------------------------------------------------

void CompositeControl::invalidate_geometry() noexcept {
    geometry_.valid = false;
}

void CompositeControl::ensure_geometry() const {
    if (geometry_.valid) return;
    const CompositeEvaluator evaluator(*this, CompositeFiring{});
    evaluator.evaluate_geometry(client_rectangle(), geometry_);
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    const Rect client = client_rectangle();
    for (std::size_t index = 0U; index < geometry_.count; ++index) {
        Rect published = geometry_.logical[index];
        if (mirrored) published.x = client.x + client.right() - published.right();
        geometry_.published[index] = published;
    }
    geometry_.valid = true;
    ++statistics_.geometry_passes;
}

Size CompositeControl::measure(const Size available) {
    const Size base = UserControl::measure(available);
    // The parts' own extent: their boxes evaluated over an empty client,
    // where only fixed widths, heights and text extents take room.
    Geometry probe{};
    const CompositeEvaluator evaluator(*this, CompositeFiring{});
    evaluator.evaluate_geometry(Rect{}, probe);
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    for (std::size_t index = 2U; index < probe.count; ++index) {
        if (!probe.present[index]) continue;
        const Rect box = probe.logical[index];
        left = std::min(left, box.x);
        top = std::min(top, box.y);
        right = std::max(right, box.right());
        bottom = std::max(bottom, box.bottom());
    }
    const Size parts{right - left, bottom - top};
    const Size measured{std::max(base.width, parts.width), std::max(base.height, parts.height)};
    return measured;
}

void CompositeControl::arrange(const Rect final_bounds) {
    UserControl::arrange(final_bounds);
    invalidate_geometry();
}

const ControlAnatomy& CompositeControl::anatomy() const noexcept {
    return (*definition_).anatomy;
}

// A nested call while the declared states are evaluated answers from the
// states decided so far (deciding_states_), so a `when` reading the
// composite's own states does not recurse (A-gf-controls-02-F1).
AnatomyStateSet CompositeControl::anatomy_states() const {
    if (deciding_states_ != nullptr) return *deciding_states_;
    AnatomyStateSet states = base_anatomy_states();
    if (pressed_part_.present() && pressed_inside_) states.insert(AnatomyState::pressed);
    if (drag_ != nullptr && (*drag_).running()) states.insert(AnatomyState::captured);
    const DecidingStatesScope deciding(deciding_states_, states);
    const CompositeEvaluator evaluator(*this, CompositeFiring{});
    for (const control_language::CompositeStateDefinition& state : (*definition_).states) {
        if (!evaluator.holds(state.when)) continue;
        if (state.standard) states.insert(*state.standard);
        if (state.custom_index) states.insert(*state.custom_index);
    }
    return states;
}

AnatomyStateSet CompositeControl::part_states(const std::size_t part) const {
    AnatomyStateSet states{};
    const CompositePartDefinition& declared = (*definition_).parts[part];
    const bool captured = drag_ != nullptr && (*drag_).running() && drag_state_.part == stxlib::Optional<std::size_t>::of(part);
    for (const AnatomyState state : declared.states) {
        const bool active = (state == AnatomyState::hot && hot_part_ == stxlib::Optional<std::size_t>::of(part)) ||
                            (state == AnatomyState::pressed && pressed_part_ == stxlib::Optional<std::size_t>::of(part) && pressed_inside_) ||
                            (state == AnatomyState::captured && captured) ||
                            (state == AnatomyState::disabled && geometry_.disabled[part]);
        if (active) states.insert(state);
    }
    return states;
}

void CompositeControl::publish_part_geometry(const std::span<PartGeometry> geometry) const {
    ensure_geometry();
    const std::size_t count = std::min(geometry.size(), geometry_.count);
    for (std::size_t index = 0U; index < count; ++index) {
        PartGeometry record{};
        record.bounds = geometry_.present[index] ? geometry_.published[index] : Rect{};
        record.present = geometry_.present[index] && !record.bounds.empty();
        record.states = part_states(index);
        geometry[index] = record;
    }
}

Rect CompositeControl::part_bounds(const std::string_view part) const {
    ensure_geometry();
    for (std::size_t index = 0U; index < geometry_.count; ++index) {
        const bool named = (*definition_).parts[index].name == part;
        if (named && geometry_.present[index]) return geometry_.published[index];
    }
    return Rect{};
}

void CompositeControl::on_paint(Painter& painter, const Rect local_damage) {
    UserControl::on_paint(painter, local_damage);
    ensure_geometry();
    const AnatomyStateSet control_states = anatomy_states();
    const FontSpec font = text_font(FontSpec{});
    for (std::size_t index = 2U; index < geometry_.count; ++index) {
        if (!geometry_.present[index]) continue;
        const CompositePartDefinition& part = (*definition_).parts[index];
        // The part's own hot and pressed replace the control's for a part
        // that lists them (a stepper's arrows are hot one at a time).
        AnatomyStateSet states = control_states;
        const AnatomyStateSet own = part_states(index);
        for (const AnatomyState state : part.states) {
            states.erase(state);
            if (own.contains(state)) states.insert(state);
        }
        if (geometry_.disabled[index]) {
            states.erase(AnatomyState::enabled);
            states.insert(AnatomyState::disabled);
        }
        if (part.paint_hook) {
            std::string hooked{};
            if (part.text_property) {
                const BindingValue value = read_property(*part.text_property);
                const std::string* text = std::get_if<std::string>(&value);
                if (text != nullptr) hooked = *text;
            }
            CompositePaintHook hook{};
            hook.part = part.name;
            hook.bounds = geometry_.published[index];
            hook.states = states;
            hook.text = hooked;
            paint_hook(painter, hook);
            continue;
        }
        PartPaint paint{.role = part.role, .bounds = geometry_.published[index], .states = states,
                        .family = (*definition_).family};
        paint.mark = part.mark;
        paint.corner_radius = paint_radius(part, paint.bounds);
        paint.font = font;
        paint.key.set(PartKey{part.registered, {}, {}});
        std::string shown{};
        if (part.text_property && text_role(part.role)) {
            const BindingValue value = read_property(*part.text_property);
            const std::string* text = std::get_if<std::string>(&value);
            if (text != nullptr) shown = *text;
            paint.text = lend_paint_text(shown);
        }
        paint_part(painter, *this, paint);
    }
    // The focus cue, where the document names one.
    const bool cue = (*definition_).focus.visible_cue.has_value() && control_states.contains(AnatomyState::focus_visible);
    if (cue && *(*definition_).focus.visible_cue < geometry_.count) {
        const std::size_t part = *(*definition_).focus.visible_cue;
        PartPaint focus{.role = PartRole::focus, .bounds = geometry_.published[part], .states = control_states,
                        .family = (*definition_).family};
        focus.key.set(PartKey{"focus", {}, {}});
        paint_part(painter, *this, focus);
    }
}

void CompositeControl::paint_hook(Painter&, const CompositePaintHook&) {}

stxlib::Optional<std::size_t> CompositeControl::part_at(const Point local_point) const {
    ensure_geometry();
    const Rect client = client_rectangle();
    for (std::size_t offset = geometry_.count; offset > 2U; --offset) {
        const std::size_t index = offset - 1U;
        const CompositePartDefinition& part = (*definition_).parts[index];
        if (part.hit == ControlPartHit::none || !geometry_.present[index]) continue;
        const Rect box = geometry_.published[index];
        bool hit = false;
        if (part.hit == ControlPartHit::box) {
            hit = box.contains(local_point);
        } else if (part.hit == ControlPartHit::expand) {
            const Rect grown{box.x - part.hit_expand, box.y - part.hit_expand, box.width + part.hit_expand * 2.0,
                             box.height + part.hit_expand * 2.0};
            hit = grown.contains(local_point) && client.contains(local_point);
        } else {
            PartHitRegion region{};
            region.bounds = box;
            region.shape = shape_of(part);
            hit = part_hit(region, local_point);
        }
        if (hit) return stxlib::Optional<std::size_t>::of(index);
    }
    return stxlib::Optional<std::size_t>::none();
}

// -- input -------------------------------------------------------------------------

void CompositeControl::activate_part(const std::size_t part) {
    const std::optional<std::size_t> command = (*definition_).parts[part].activates;
    if (!command || geometry_.disabled[part]) return;
    static_cast<void>(run_command(*command));
}

void CompositeControl::on_mouse_down(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary) {
        released_part_.reset();
        const stxlib::Optional<std::size_t> part = part_at(arguments.location);
        if (part.present()) {
            const CompositePartDefinition& declared = (*definition_).parts[(*part.value())];
            pressed_part_ = part;
            pressed_inside_ = true;
            invalidate(Dirty::paint);
            // A press on a part that presses is the composite's.
            if (declared.press != 0U) arguments.handled = true;
            if (pressing(declared, ControlPress::drag) && drag_ != nullptr && declared.drag) {
                const Rect own = absolute_bounds();
                const Rect box = geometry_.published[(*part.value())];
                DragStart start{};
                start.key = static_cast<std::uint64_t>((*part.value()));
                start.source.set(Rect{own.x + box.x, own.y + box.y, box.width, box.height});
                start.threshold = (*definition_).drags[*declared.drag].threshold;
                drag_state_ = DragState{};
                drag_state_.part = part;
                const DragStartResult result = (*drag_).press(pointer_event_of(arguments), start);
                if (result == DragStartResult::armed || result == DragStartResult::started) arguments.handled = true;
            }
            if (pressing(declared, ControlPress::down)) activate_part((*part.value()));
            if (pressing(declared, ControlPress::repeat)) {
                activate_part((*part.value()));
                begin_repeat((*part.value()));
            }
            if (!is_alive()) return;
        }
        CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::mouse_down, {}, std::nullopt, 0.0);
        if (!is_alive()) return;
    }
    UserControl::on_mouse_down(arguments);
}

void CompositeControl::on_mouse_move(MouseEventArgs& arguments) {
    if (drag_ != nullptr && (*drag_).running()) {
        const bool taken = (*drag_).pointer(pointer_event_of(arguments));
        if (taken) arguments.handled = true;
    }
    const stxlib::Optional<std::size_t> part = part_at(arguments.location);
    const bool inside = pressed_part_.present() && part == pressed_part_;
    if (part != hot_part_ || inside != pressed_inside_) {
        hot_part_ = part;
        pressed_inside_ = inside;
        invalidate(Dirty::paint);
    }
    UserControl::on_mouse_move(arguments);
}

void CompositeControl::on_mouse_up(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary) {
        if (drag_ != nullptr && (*drag_).running()) {
            const bool planned = drag_state_.planned;
            const bool taken = (*drag_).pointer(pointer_event_of(arguments));
            if (taken) arguments.handled = true;
            // The release of a drag is no click.
            if (planned) arguments.suppress_click = true;
        }
        const stxlib::Optional<std::size_t> part = part_at(arguments.location);
        if (pressed_part_.present() && part == pressed_part_ && !arguments.suppress_click) released_part_ = pressed_part_;
        pressed_part_.reset();
        pressed_inside_ = false;
        repeat_frames_.disconnect();
        invalidate(Dirty::paint);
        CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::mouse_up, {}, std::nullopt, 0.0);
        if (!is_alive()) return;
    }
    UserControl::on_mouse_up(arguments);
}

void CompositeControl::on_mouse_click(MouseEventArgs& arguments) {
    const stxlib::Optional<std::size_t> part = released_part_;
    released_part_.reset();
    if (part.present() && pressing((*definition_).parts[(*part.value())], ControlPress::click)) {
        activate_part((*part.value()));
        if (!is_alive()) return;
    }
    CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::click, {}, std::nullopt, 0.0);
    if (!is_alive()) return;
    UserControl::on_mouse_click(arguments);
}

void CompositeControl::on_mouse_leave() {
    if (hot_part_.present()) {
        hot_part_.reset();
        invalidate(Dirty::paint);
    }
    UserControl::on_mouse_leave();
}

void CompositeControl::on_mouse_wheel(MouseEventArgs& arguments) {
    // Lines are notches; a pixel delta counts one notch per event.
    double notches = arguments.wheel_delta.y;
    if (arguments.wheel_unit == WheelDeltaUnit::pixels && notches != 0.0) notches = notches > 0.0 ? 1.0 : -1.0;
    const std::uint64_t before = statistics_.rule_firings;
    CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::mouse_wheel, {}, std::nullopt, notches);
    if (!is_alive()) return;
    if (statistics_.rule_firings != before) arguments.handled = true;
    UserControl::on_mouse_wheel(arguments);
}

bool CompositeControl::claims_key(const std::uint32_t physical_key, const Modifier modifiers) const noexcept {
    for (const CompositeRuleDefinition& rule : (*definition_).rules) {
        if (rule.trigger == CompositeTrigger::key && rule.key == physical_key && rule.modifiers == modifiers) {
            return true;
        }
    }
    for (const CompositeDragDefinition& drag : (*definition_).drags) {
        if (drag.keyboard_key != 0U && drag.keyboard_key == physical_key && drag.keyboard_modifiers == modifiers) {
            return true;
        }
    }
    return false;
}

void CompositeControl::on_preview_key_down(PreviewKeyDownEventArgs& arguments) {
    // A handled key is claimed as an input key, so dialog keys do not take
    // it first. Tab is never declared (the checker refused it).
    if (claims_key(arguments.physical_key, arguments.modifiers)) arguments.is_input_key = true;
    UserControl::on_preview_key_down(arguments);
}

bool CompositeControl::handle_key(const KeyEventArgs& arguments, const bool key_up) {
    const ControlKeyPhase phase = key_up ? ControlKeyPhase::key_up : ControlKeyPhase::key_down;
    bool handled = false;
    for (std::size_t index = 0U; index < (*definition_).rules.size(); ++index) {
        const CompositeRuleDefinition& rule = (*definition_).rules[index];
        if (rule.trigger != CompositeTrigger::key || rule.key != arguments.physical_key ||
            rule.modifiers != arguments.modifiers) {
            continue;
        }
        // The other phase of a claimed key is still handled, so it goes
        // nowhere else.
        handled = true;
        if (rule.phase != phase) continue;
        fire(index, 0.0);
        if (!is_alive()) return true;
    }
    return handled;
}

void CompositeControl::on_key_down(KeyEventArgs& arguments) {
    if (!arguments.handled && handle_key(arguments, false)) arguments.handled = true;
    if (!is_alive()) return;
    if (!arguments.handled && drag_ != nullptr) {
        for (const CompositeDragDefinition& drag : (*definition_).drags) {
            if (drag.keyboard_key != 0U && drag.keyboard_key == arguments.physical_key &&
                drag.keyboard_modifiers == arguments.modifiers) {
                arguments.handled = begin_keyboard_drag();
                break;
            }
        }
    }
    UserControl::on_key_down(arguments);
}

void CompositeControl::on_key_up(KeyEventArgs& arguments) {
    if (!arguments.handled && handle_key(arguments, true)) arguments.handled = true;
    if (!is_alive()) return;
    UserControl::on_key_up(arguments);
}

void CompositeControl::on_got_focus() {
    CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::focus_gained, {}, std::nullopt, 0.0);
    if (!is_alive()) return;
    UserControl::on_got_focus();
}

void CompositeControl::on_lost_focus() {
    CompositeInstanceAccess::fire_trigger(*this, CompositeTrigger::focus_lost, {}, std::nullopt, 0.0);
    if (!is_alive()) return;
    UserControl::on_lost_focus();
}

void CompositeControl::on_detaching_from_window(SurfaceRoot& former_root) noexcept {
    // The tracker ends its session without a callback (subject_gone).
    if (drag_ != nullptr) (*drag_).owner_detaching(former_root);
    repeat_frames_.disconnect();
    drag_state_ = DragState{};
    pressed_part_.reset();
    hot_part_.reset();
    UserControl::on_detaching_from_window(former_root);
}

Control::Ptr CompositeControl::focus_target() const {
    if ((*definition_).focus.target == control_language::ControlFocusTarget::child) {
        const Control::Ptr target = child((*definition_).focus.child);
        if (target) return target;
    }
    return UserControl::focus_target();
}

// -- press repetition and drags ----------------------------------------------------

void CompositeControl::begin_repeat(const std::size_t part) {
    repeat_frames_.disconnect();
    // The part is read again at each step (pressed_part_).
    static_cast<void>(part);
    const BorrowedPointer<SurfaceRoot> root = surface_root();
    if (root == nullptr) return;
    const FrameTime now = FrameClock::now();
    const std::shared_ptr<CompositeControl> self = std::static_pointer_cast<CompositeControl>(shared_from_this());
    repeat_frames_ = (*root).schedule_ui_timer(*this, scroll_repeat_interval_default, now + scroll_repeat_delay_default, CompositeRepeatTick{self});
}

void CompositeControl::repeat_step() {
    // Repeats while the pressed part is held under the pointer; a part
    // that became disabled (its command no longer enabled) stops it.
    if (pressed_part_.absent() || !has_pointer_capture()) {
        repeat_frames_.disconnect();
        return;
    }
    if (!pressed_inside_) return;
    const std::optional<std::size_t> command = (*definition_).parts[(*pressed_part_.value())].activates;
    if (!command || !run_command(*command)) repeat_frames_.disconnect();
}

void CompositeInstanceAccess::repeat(CompositeControl& control) {
    control.repeat_step();
}

double CompositeControl::drag_fraction(const double value) const noexcept {
    const double extent = drag_state_.range_end - drag_state_.range_start;
    if (extent <= 0.0) return 0.0;
    const double fraction = std::clamp((value - drag_state_.range_start) / extent, 0.0, 1.0);
    return fraction;
}

void CompositeControl::plan_drag(DragPlan& plan) {
    if (drag_state_.part.absent()) {
        plan.refused = true;
        return;
    }
    const CompositePartDefinition& part = (*definition_).parts[(*drag_state_.part.value())];
    const CompositeDragDefinition& drag = (*definition_).drags[*part.drag];
    ensure_geometry();
    const Rect box = geometry_.logical[(*drag_state_.part.value())];
    const bool block = drag.axis == control_language::ControlDragAxis::block_axis;
    const CompositeEvaluator evaluator(*this, CompositeFiring{});
    double start = 0.0;
    double end = 0.0;
    if (drag.range.parsed.terms.size() == 2U) {
        start = evaluator.evaluate(drag.range, drag.range.parsed.terms[0]).number;
        end = evaluator.evaluate(drag.range, drag.range.parsed.terms[1]).number;
    }
    // The dragged point is the part's centre, which travels between the
    // range's ends less half the part.
    const double half = (block ? box.height : box.width) * 0.5;
    drag_state_.range_start = std::min(start, end) + half;
    drag_state_.range_end = std::max(start, end) - half;
    drag_state_.pressed_value = read_property(drag.maps_to);
    drag_state_.planned = true;
    const bool mirrored = !block && effective_layout_direction() == LayoutDirection::right_to_left;
    plan.kind = DragKind::value;
    plan.value.axis = block ? DragAxis::y : DragAxis::x;
    plan.value.origin_value = block ? box.y + half : box.x + half;
    plan.value.scale = mirrored ? -1.0 : 1.0;
    plan.value.minimum = drag_state_.range_start;
    plan.value.maximum = std::max(drag_state_.range_end, drag_state_.range_start);
    plan.value.keyboard_step = std::max((plan.value.maximum - plan.value.minimum) / 10.0, 1.0);
    plan.settles = drag.settles;
}

void CompositeControl::preview_drag(const DragTrackerFrame& frame) {
    if (drag_state_.part.absent()) return;
    const CompositePartDefinition& part = (*definition_).parts[(*drag_state_.part.value())];
    const CompositeDragDefinition& drag = (*definition_).drags[*part.drag];
    drag_state_.frame_value = frame.value;
    CompositeFiring firing{};
    firing.frame_value = frame.value;
    firing.frame_fraction = drag_fraction(frame.value);
    if (drag.mode == ControlDragMode::commit) {
        CompositeInstanceAccess::run_calls(*this, drag.preview, firing);
        return;
    }
    // Live: the property follows every frame.
    CompositeValue value{};
    value.kind = CompositeValueKind::number;
    value.number = firing.frame_fraction;
    const CompositePropertyDefinition& target = (*definition_).properties[drag.maps_to];
    if (target.kind == ControlPropertyKind::boolean) {
        static_cast<void>(write_property(drag.maps_to, BindingValue{firing.frame_fraction >= 0.5}));
        return;
    }
    if (!target.range.empty() && target.range.parsed.terms.size() == 2U) {
        const CompositeEvaluator evaluator(*this, firing);
        const double low = evaluator.evaluate(target.range, target.range.parsed.terms[0]).number;
        const double high = evaluator.evaluate(target.range, target.range.parsed.terms[1]).number;
        value.number = low + (high - low) * firing.frame_fraction;
    }
    const std::optional<BindingValue> written = CompositeInstanceAccess::property_value_of(*this, drag.maps_to, value);
    if (written) static_cast<void>(write_property(drag.maps_to, *written));
}

void CompositeControl::finish_drag(const DragTrackerFrame& frame) {
    if (drag_state_.part.absent()) return;
    const CompositePartDefinition& part = (*definition_).parts[(*drag_state_.part.value())];
    const CompositeDragDefinition& drag = (*definition_).drags[*part.drag];
    const bool commits = drag_end_commits(frame.end);
    const DragState ended = drag_state_;
    drag_state_ = DragState{};
    pressed_part_.reset();
    pressed_inside_ = false;
    if (drag.mode == ControlDragMode::commit) {
        // The in-progress picture goes; the property is written once.
        for (std::size_t index = 0U; index < (*definition_).properties.size(); ++index) {
            if ((*definition_).properties[index].presentational) {
                static_cast<void>(write_property(index, BindingValue{}));
            }
        }
        if (commits) {
            const double fraction = (ended.range_end - ended.range_start) > 0.0
                ? std::clamp((frame.value - ended.range_start) / (ended.range_end - ended.range_start), 0.0, 1.0)
                : 0.0;
            const CompositePropertyDefinition& target = (*definition_).properties[drag.maps_to];
            if (target.kind == ControlPropertyKind::boolean) {
                static_cast<void>(write_property(drag.maps_to, BindingValue{fraction >= 0.5}));
            } else {
                CompositeValue value{};
                value.kind = CompositeValueKind::number;
                value.number = frame.value;
                const std::optional<BindingValue> written =
                    CompositeInstanceAccess::property_value_of(*this, drag.maps_to, value);
                if (written) static_cast<void>(write_property(drag.maps_to, *written));
            }
        }
    } else if (!commits) {
        // Live: Escape means as it was.
        static_cast<void>(write_property(drag.maps_to, ended.pressed_value));
    }
    if (commits) {
        ++statistics_.drags_committed;
    } else {
        ++statistics_.drags_restored;
    }
    // Geometry transitions are the theme's (phase 3): nothing settles yet.
    if (frame.animate && drag_ != nullptr) (*drag_).finish_settling();
    invalidate(Dirty::paint);
}

bool CompositeControl::begin_keyboard_drag() {
    if (drag_ == nullptr) return UserControl::begin_keyboard_drag();
    for (std::size_t index = 2U; index < (*definition_).parts.size(); ++index) {
        if (!(*definition_).parts[index].drag) continue;
        drag_state_ = DragState{};
        drag_state_.part.set(index);
        DragStart start{};
        start.key = static_cast<std::uint64_t>(index);
        const DragStartResult result = (*drag_).begin_keyboard(start);
        return result == DragStartResult::started;
    }
    return false;
}

// -- semantics ---------------------------------------------------------------------

SemanticDescriptor CompositeControl::semantic_descriptor() const {
    SemanticDescriptor descriptor = UserControl::semantic_descriptor();
    if (!(*definition_).semantics) return descriptor;
    const control_language::CompositeSemanticsDefinition& semantics = *(*definition_).semantics;
    if (!semantics.from_child.empty()) {
        // The focus target's node stands for the composite: the composite
        // publishes none, and its children's nodes take its place, the
        // focus target's (with its text, selection and actions, reached
        // by its own runtime ID) among them.
        descriptor.exposed = false;
        descriptor.include_descendants = true;
        return descriptor;
    }
    const CompositeEvaluator evaluator(*this, CompositeFiring{});
    descriptor.exposed = true;
    descriptor.role = semantics.role;
    if (!semantics.name.empty()) {
        // `a or b` reads the first that is not empty.
        const CompositeValue name = evaluator.first(semantics.name);
        if (name.kind == CompositeValueKind::text || name.kind == CompositeValueKind::word) {
            descriptor.name = name.text;
        }
        if (name.kind == CompositeValueKind::boolean) {
            // An `or` of texts evaluates to a truth; read its operands.
            const ExpressionNode& term = semantics.name.parsed.nodes[semantics.name.parsed.terms[0].value];
            for (std::uint32_t operand = 0U; operand < term.argument_count && descriptor.name.empty(); ++operand) {
                const CompositeValue read =
                    evaluator.evaluate(semantics.name, semantics.name.parsed.arguments[term.first_argument + operand]);
                if (read.kind == CompositeValueKind::text || read.kind == CompositeValueKind::word) {
                    descriptor.name = read.text;
                }
            }
        }
    }
    if (!semantics.value.empty()) {
        const CompositeValue value = evaluator.first(semantics.value);
        descriptor.value = evaluator.text(semantics.value);
        if (value.kind == CompositeValueKind::number) descriptor.numeric_value.set(value.number);
    }
    const std::vector<ExpressionNodeIndex>& range = semantics.range.parsed.terms;
    if (range.size() >= 1U) descriptor.minimum_value.set(evaluator.evaluate(semantics.range, range[0]).number);
    if (range.size() >= 2U) descriptor.maximum_value.set(evaluator.evaluate(semantics.range, range[1]).number);
    if (range.size() >= 3U) descriptor.small_step.set(evaluator.evaluate(semantics.range, range[2]).number);
    for (const control_language::CompositeSemanticState& state : semantics.states) {
        if (CompositeEvaluator::truthy(evaluator.evaluate(semantics.states_expression, state.condition))) {
            descriptor.states = descriptor.states | state.state;
        }
    }
    for (const control_language::CompositeSemanticAction& action : semantics.actions) {
        const bool listed =
            std::find(descriptor.actions.begin(), descriptor.actions.end(), action.action) != descriptor.actions.end();
        if (!listed) descriptor.actions.push_back(action.action);
    }
    descriptor.include_descendants = semantics.children_exposed;
    return descriptor;
}

bool CompositeControl::on_semantic_action(const SemanticAction action, const std::string_view value) {
    if ((*definition_).semantics) {
        for (const control_language::CompositeSemanticAction& mapped : (*(*definition_).semantics).actions) {
            if (mapped.action != action) continue;
            if (mapped.command) return run_command(*mapped.command);
            if (mapped.property) {
                const WriteOutcome outcome = write_property(*mapped.property, BindingValue{std::string(value)});
                return outcome != WriteOutcome::refused;
            }
        }
    }
    // A command declared with this semantic action (design note 3.7).
    for (std::size_t index = 0U; index < (*definition_).commands.size(); ++index) {
        if ((*definition_).commands[index].semantic_action == action) return run_command(index);
    }
    return UserControl::on_semantic_action(action, value);
}

void CompositeControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    UserControl::append_interactions(interactions);
    for (std::size_t index = 0U; index < (*definition_).events.size(); ++index) {
        InteractionDeclaration declaration{};
        declaration.name = (*definition_).interaction_names[index];
        declaration.payload = (*definition_).events[index].payload
            ? control_language::control_payload_type_name(*(*definition_).events[index].payload)
            : std::string_view{};
        interactions.push_back(declaration);
    }
}

} // namespace gui_forms
