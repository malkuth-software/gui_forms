// The rule vocabulary's records (rule.hpp): the trigger, condition and
// built-in action makers and the kinds' names.

#include "gui_forms/rules/rule/rule.hpp"

namespace gui_forms {
namespace {

RuleTrigger key_trigger(RuleTriggerKind kind, std::uint32_t physical_key) {
    RuleTrigger made{};
    made.kind = kind;
    made.physical_key = physical_key;
    return made;
}

RuleTrigger control_trigger(RuleTriggerKind kind, Control& control) {
    RuleTrigger made{};
    made.kind = kind;
    made.control = control.weak_from_this();
    Component& anchor = control;
    made.anchor = &anchor;
    return made;
}

RuleCondition control_condition(RuleConditionKind kind, Control& control, bool negated) {
    RuleCondition made{};
    made.kind = kind;
    made.negated = negated;
    made.control = control.weak_from_this();
    return made;
}

RuleCondition key_condition(std::uint32_t physical_key, bool negated) {
    RuleCondition made{};
    made.kind = RuleConditionKind::key_down;
    made.negated = negated;
    made.physical_key = physical_key;
    return made;
}

RuleCondition active_condition(bool negated) {
    RuleCondition made{};
    made.kind = RuleConditionKind::window_active;
    made.negated = negated;
    return made;
}

RuleCondition flag_condition(Value<bool>& flag, bool negated) {
    RuleCondition made{};
    made.kind = RuleConditionKind::is_true;
    made.negated = negated;
    made.flag = &flag;
    Component& anchor = flag;
    made.anchor = &anchor;
    return made;
}

RuleCondition number_condition(Value<int>& number, int expected, bool negated) {
    RuleCondition made{};
    made.kind = RuleConditionKind::equals;
    made.negated = negated;
    made.number = &number;
    made.expected = expected;
    Component& anchor = number;
    made.anchor = &anchor;
    return made;
}

} // namespace

std::string_view rule_trigger_kind_name(RuleTriggerKind kind) noexcept {
    switch (kind) {
    case RuleTriggerKind::key_pressed: return "key_pressed";
    case RuleTriggerKind::key_released: return "key_released";
    case RuleTriggerKind::key_repeat: return "key_repeat";
    case RuleTriggerKind::mouse_down: return "mouse_down";
    case RuleTriggerKind::mouse_up: return "mouse_up";
    case RuleTriggerKind::click: return "click";
    case RuleTriggerKind::mouse_click: return "mouse_click";
    case RuleTriggerKind::double_click: return "double_click";
    case RuleTriggerKind::mouse_double_click: return "mouse_double_click";
    case RuleTriggerKind::context_menu_requested: return "context_menu_requested";
    case RuleTriggerKind::mouse_enter: return "mouse_enter";
    case RuleTriggerKind::mouse_hover: return "mouse_hover";
    case RuleTriggerKind::mouse_leave: return "mouse_leave";
    case RuleTriggerKind::mouse_capture_changed: return "mouse_capture_changed";
    case RuleTriggerKind::focus_gained: return "focus_gained";
    case RuleTriggerKind::focus_lost: return "focus_lost";
    case RuleTriggerKind::interaction: return "interaction";
    case RuleTriggerKind::command_executed: return "command_executed";
    case RuleTriggerKind::value_changed: return "value_changed";
    case RuleTriggerKind::none:
    default:
        break;
    }
    return "none";
}

std::string_view rule_condition_kind_name(RuleConditionKind kind) noexcept {
    switch (kind) {
    case RuleConditionKind::key_down: return "key_down";
    case RuleConditionKind::modifiers: return "modifiers";
    case RuleConditionKind::hovering: return "hovering";
    case RuleConditionKind::pressed: return "pressed";
    case RuleConditionKind::focused: return "focused";
    case RuleConditionKind::focus_within: return "focus_within";
    case RuleConditionKind::enabled: return "enabled";
    case RuleConditionKind::visible: return "visible";
    case RuleConditionKind::window_active: return "window_active";
    case RuleConditionKind::is_true: return "is_true";
    case RuleConditionKind::equals: return "equals";
    case RuleConditionKind::none:
    default:
        break;
    }
    return "none";
}

std::string_view rule_action_kind_name(RuleActionKind kind) noexcept {
    switch (kind) {
    case RuleActionKind::program: return "program";
    case RuleActionKind::invoke: return "invoke";
    case RuleActionKind::set: return "set";
    case RuleActionKind::toggle: return "toggle";
    case RuleActionKind::focus: return "focus";
    case RuleActionKind::none:
    default:
        break;
    }
    return "none";
}

namespace detail {

void toggle_rule_value(void* value, const RuleActionArgument&) {
    Value<bool>& target = *static_cast<Value<bool>*>(value);
    const bool toggled = !target.get();
    target.set(toggled);
}

} // namespace detail

namespace trigger {

RuleTrigger key_pressed(std::uint32_t physical_key) {
    return key_trigger(RuleTriggerKind::key_pressed, physical_key);
}
RuleTrigger key_released(std::uint32_t physical_key) {
    return key_trigger(RuleTriggerKind::key_released, physical_key);
}
RuleTrigger key_repeat(std::uint32_t physical_key) {
    return key_trigger(RuleTriggerKind::key_repeat, physical_key);
}
RuleTrigger mouse_down(Control& control) { return control_trigger(RuleTriggerKind::mouse_down, control); }
RuleTrigger mouse_up(Control& control) { return control_trigger(RuleTriggerKind::mouse_up, control); }
RuleTrigger click(Control& control) { return control_trigger(RuleTriggerKind::click, control); }
RuleTrigger mouse_click(Control& control) { return control_trigger(RuleTriggerKind::mouse_click, control); }
RuleTrigger double_click(Control& control) { return control_trigger(RuleTriggerKind::double_click, control); }
RuleTrigger mouse_double_click(Control& control) {
    return control_trigger(RuleTriggerKind::mouse_double_click, control);
}
RuleTrigger context_menu_requested(Control& control) {
    return control_trigger(RuleTriggerKind::context_menu_requested, control);
}
RuleTrigger mouse_enter(Control& control) { return control_trigger(RuleTriggerKind::mouse_enter, control); }
RuleTrigger mouse_hover(Control& control) { return control_trigger(RuleTriggerKind::mouse_hover, control); }
RuleTrigger mouse_leave(Control& control) { return control_trigger(RuleTriggerKind::mouse_leave, control); }
RuleTrigger mouse_capture_changed(Control& control) {
    return control_trigger(RuleTriggerKind::mouse_capture_changed, control);
}
RuleTrigger focus_gained(Control& control) { return control_trigger(RuleTriggerKind::focus_gained, control); }
RuleTrigger focus_lost(Control& control) { return control_trigger(RuleTriggerKind::focus_lost, control); }

RuleTrigger command_executed(Command& command) {
    RuleTrigger made{};
    made.kind = RuleTriggerKind::command_executed;
    made.command = &command;
    Component& anchor = command;
    made.anchor = &anchor;
    return made;
}

} // namespace trigger

namespace condition {

RuleCondition key_down(std::uint32_t physical_key) { return key_condition(physical_key, false); }
RuleCondition not_key_down(std::uint32_t physical_key) { return key_condition(physical_key, true); }

RuleCondition modifiers(Modifier exact) {
    RuleCondition made{};
    made.kind = RuleConditionKind::modifiers;
    made.modifiers = exact;
    return made;
}

RuleCondition hovering(Control& control) { return control_condition(RuleConditionKind::hovering, control, false); }
RuleCondition not_hovering(Control& control) {
    return control_condition(RuleConditionKind::hovering, control, true);
}
RuleCondition pressed(Control& control) { return control_condition(RuleConditionKind::pressed, control, false); }
RuleCondition not_pressed(Control& control) { return control_condition(RuleConditionKind::pressed, control, true); }
RuleCondition focused(Control& control) { return control_condition(RuleConditionKind::focused, control, false); }
RuleCondition not_focused(Control& control) { return control_condition(RuleConditionKind::focused, control, true); }
RuleCondition focus_within(Control& control) {
    return control_condition(RuleConditionKind::focus_within, control, false);
}
RuleCondition not_focus_within(Control& control) {
    return control_condition(RuleConditionKind::focus_within, control, true);
}
RuleCondition enabled(Control& control) { return control_condition(RuleConditionKind::enabled, control, false); }
RuleCondition not_enabled(Control& control) { return control_condition(RuleConditionKind::enabled, control, true); }
RuleCondition visible(Control& control) { return control_condition(RuleConditionKind::visible, control, false); }
RuleCondition not_visible(Control& control) { return control_condition(RuleConditionKind::visible, control, true); }
RuleCondition window_active() { return active_condition(false); }
RuleCondition not_window_active() { return active_condition(true); }
RuleCondition is_true(Value<bool>& flag) { return flag_condition(flag, false); }
RuleCondition not_true(Value<bool>& flag) { return flag_condition(flag, true); }
RuleCondition equals(Value<int>& number, int expected) { return number_condition(number, expected, false); }
RuleCondition not_equals(Value<int>& number, int expected) { return number_condition(number, expected, true); }

} // namespace condition

namespace builtin {

RuleAction invoke(Command& command) {
    RuleAction made{};
    made.kind = RuleActionKind::invoke;
    made.command = &command;
    Component& anchor = command;
    made.anchor = &anchor;
    return made;
}

RuleAction toggle(Value<bool>& value) {
    RuleAction made{};
    made.kind = RuleActionKind::toggle;
    made.value = static_cast<void*>(&value);
    made.write = &detail::toggle_rule_value;
    Component& anchor = value;
    made.anchor = &anchor;
    return made;
}

RuleAction focus(Control& control) {
    RuleAction made{};
    made.kind = RuleActionKind::focus;
    made.control = control.weak_from_this();
    return made;
}

} // namespace builtin

} // namespace gui_forms
