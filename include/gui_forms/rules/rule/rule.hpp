#pragma once
// A rule (notes:planning APP_WINDOW_RULES_DESIGN Part C): one trigger, zero
// or more conditions that must all hold, and one or more actions run in
// order, kept by a window's RuleSet (rule_set.hpp). A rule is data over a
// closed vocabulary, as a theme rule is: the trigger, condition and action
// kinds below are the whole of it, and adding a word is a library change.
//
// Triggers are edges: a key's press, repeat or release, and the events a
// control raises (the WinForms mouse events, focus, a typed interaction), a
// command's execution and a Value's change. Conditions are levels read when
// the trigger occurs: held keys and modifiers, the window's hover, press and
// focus registries, a control's availability, the window's activation and a
// bound model. Actions are named functions with an explicit context, or one
// of four built-ins (invoke a command, set or toggle a Value, focus a
// control). There are no lambdas and no callable framework: a program
// action is a plain function `void (Context&, const RuleFiring&)` named
// beside its context.
//
// The records borrow what they name. A rule naming a component (a control,
// a command, a Value, a context that is a Component) is disarmed when that
// component is disposed or destroyed, and its token then reads stale. A
// context that is not a Component must outlive its rules.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/events/drag_events/drag_events.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/value/value.hpp"
#include "gui_forms/window/interaction/interaction_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gui_forms {

class SurfaceRoot;
struct RuleFiring;

// The trigger kinds. The numbers are the C ABI's GF_RULE_TRIGGER_* values.
enum class RuleTriggerKind : std::uint8_t {
    none = 0,
    // A key's press (not the platform's auto-repeat), its auto-repeat (the
    // platform's own; GUI.Forms times no holds), and its release.
    key_pressed = 1,
    key_released = 2,
    key_repeat = 3,
    // The control's WinForms mouse events (Control::mouse_down and the rest).
    mouse_down = 4,
    mouse_up = 5,
    click = 6,
    mouse_click = 7,
    double_click = 8,
    mouse_double_click = 9,
    context_menu_requested = 10,
    mouse_enter = 11,
    mouse_hover = 12,
    mouse_leave = 13,
    mouse_capture_changed = 14,
    // The control gained or lost the keyboard focus (Control::focus_observed).
    focus_gained = 15,
    focus_lost = 16,
    // Any interaction the schema lists for the control's type, named by its
    // typed event in C++ and by its schema name in the C ABI.
    interaction = 17,
    // A Command's execution (Command::invoked).
    command_executed = 18,
    // A Value<T>'s change (Value::changed). C++ only: the C ABI has no Value.
    value_changed = 19,
};

// The condition kinds. The numbers through window_active are the C ABI's
// GF_RULE_CONDITION_* values; is_true and equals are C++ only. dragging and
// drop_candidate (the drag engine's, roadmap 2.1) are the C ABI's 12 and 13
// since 1.2.
enum class RuleConditionKind : std::uint8_t {
    none = 0,
    // The physical key is held (the window's input state).
    key_down = 1,
    // The held modifiers are exactly these.
    modifiers = 2,
    // The pointer is over the control or one of its descendants.
    hovering = 3,
    // The control is the window's pressed control.
    pressed = 4,
    // The control has the keyboard focus.
    focused = 5,
    // The control or one of its descendants has the keyboard focus.
    focus_within = 6,
    // The control is effectively enabled, effectively visible.
    enabled = 7,
    visible = 8,
    // The window is the active window.
    window_active = 9,
    // A bound model: a Value<bool> holds true; a Value<int> holds a number.
    is_true = 10,
    equals = 11,
    // A drag session of the window's drag engine is running (planning,
    // dragging or settling) on the control or one of its descendants.
    dragging = 12,
    // The control is the container of the running drag's selected
    // candidate (the group container its client named).
    drop_candidate = 13,
};

// The action kinds. The numbers through focus are the C ABI's
// GF_RULE_ACTION_* values (program is a gf_callback there);
// begin_keyboard_drag is the C ABI's 6 since 1.2.
enum class RuleActionKind : std::uint8_t {
    none = 0,
    program = 1,
    invoke = 2,
    set = 3,
    toggle = 4,
    focus = 5,
    // Enters the control's keyboard drag mode (Control::begin_keyboard_drag).
    begin_keyboard_drag = 6,
};

// The most conditions and actions one rule holds, and the most rules one
// window holds. add() refuses a rule past any of them.
inline constexpr std::size_t maximum_rule_conditions = 8U;
inline constexpr std::size_t maximum_rule_actions = 8U;
inline constexpr std::size_t maximum_rules = 1024U;

namespace detail {

class RuleTap;

// The connection of a trigger whose event type varies: a control's typed
// interaction, a Value<T>'s change, or (in the C ABI) an interaction named
// by its schema name. Type erasure is admitted here because the payload
// types vary at run time (house style section 7). connect() subscribes to
// the event so that each emission reaches `tap` (fire_rule_tap); the source
// is kept, with its subscription, by the window's rule set while a rule
// uses it. identity() and name() say which event it is: two sources with
// the same identity and name are one trigger (the duplicate check, and one
// subscription shared by every rule on it). identity() is compared, never
// dereferenced, after connect().
class GUI_FORMS_API RuleSource {
public:
    virtual ~RuleSource() = default;
    [[nodiscard]] virtual SubscriptionToken connect(RuleTap& tap) = 0;
    [[nodiscard]] virtual const void* identity() const noexcept = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept { return {}; }
};

// Hands one emission of a source's event to its tap, which fills the
// trigger kind and control and offers it to the window's rules. `firing`
// carries what the source read from the event (a Value's new value).
GUI_FORMS_API void fire_rule_tap(RuleTap& tap, RuleFiring& firing);

} // namespace detail

// What a value_changed trigger's Value held after the change: integers,
// enumerations and bool in `integer` (bool as 0 or 1), floating values in
// `real`. `present` is false for other triggers.
struct GUI_FORMS_API RuleValuePayload final {
    std::int64_t integer{};
    double real{};
    bool present{};
};

// One firing of a rule, passed to each of its program actions. The trigger
// payload pointers are borrowed for the action's call and are null where the
// trigger has none: `key` for the key triggers, `mouse` for mouse_down,
// mouse_up, mouse_click and mouse_double_click, `context_request` for
// context_menu_requested (an action may set its `handled`), `invocation`
// for command_executed, `drag` for an interaction trigger on a typed drag
// interaction (drag_started, drag_target_changed, drag_dropped,
// drag_ended). `control` is the control the trigger names (null for
// the key triggers and command_executed). `state` is the window's input
// state as it was when the trigger occurred (one copy per occurrence that
// fires a rule). `window` is the window whose rules fired.
struct GUI_FORMS_API RuleFiring final {
    RuleTriggerKind trigger{RuleTriggerKind::none};
    Control* control{};
    const KeyEvent* key{};
    const MouseEventArgs* mouse{};
    ContextMenuRequest* context_request{};
    const CommandInvocation* invocation{};
    const DragInteraction* drag{};
    RuleValuePayload value{};
    const InteractionSnapshot* state{};
    SurfaceRoot* window{};
};

// The trigger of a rule; made by the functions in namespace trigger. The
// fields a kind does not read stay at their initial values.
//   physical_key: the key triggers' USB HID usage (PhysicalKey).
//   control: the control the control-event and interaction triggers name.
//   command: command_executed's command.
//   source: interaction's and value_changed's connection.
//   anchor: the component the trigger names (control, command or Value),
//     whose disposal disarms the rule.
struct GUI_FORMS_API RuleTrigger final {
    RuleTriggerKind kind{RuleTriggerKind::none};
    std::uint32_t physical_key{};
    Control::WeakPtr control{};
    Command* command{};
    std::shared_ptr<detail::RuleSource> source{};
    Component* anchor{};
};

// A condition of a rule; made by the functions in namespace condition. A
// negated condition holds when its level does not (the `not_` forms).
//   physical_key: key_down's key; modifiers: the modifiers' exact set.
//   control: the control hovering, pressed, focused, focus_within, enabled,
//     visible, dragging and drop_candidate read.
//   flag: is_true's Value; number and expected: equals' Value and number.
//   anchor: the Value is_true or equals reads, whose disposal disarms the
//     rule (a control condition's control is anchored through `control`).
struct GUI_FORMS_API RuleCondition final {
    RuleConditionKind kind{RuleConditionKind::none};
    bool negated{};
    std::uint32_t physical_key{};
    Modifier modifiers{Modifier::none};
    Control::WeakPtr control{};
    const Value<bool>* flag{};
    const Value<int>* number{};
    int expected{};
    Component* anchor{};
};

// The argument a set action writes: integers, enumerations and bool in
// `integer`, floating values in `real` (the Value's own type decides).
struct GUI_FORMS_API RuleActionArgument final {
    std::int64_t integer{};
    double real{};
};

// Calls a program action's function with its context, both restored to
// their own types (detail::call_rule_program).
using RuleProgramCall = void (*)(void* context, void (*function)(), const RuleFiring& firing);
// Writes a set or toggle action's Value from its argument
// (detail::write_rule_value, detail::toggle_rule_value).
using RuleValueWrite = void (*)(void* value, const RuleActionArgument& argument);

// An action of a rule; made by action() and the functions in namespace
// builtin.
//   program: `call` runs `function` (a `void (Context&, const RuleFiring&)`
//     stored as a plain function pointer) with `context`.
//   retained: a context the rule owns, released with the rule (the C ABI's
//     callback loans); null for a C++ action, whose context is borrowed.
//   invoke: `command`, executed through the window's command route.
//   set, toggle: `value` (a Value<T>) written by `write` from `argument`.
//   focus: `control`, given the keyboard focus.
//   begin_keyboard_drag: `control`, whose keyboard drag mode is entered.
//   anchor: the component the action names (a context that is a Component,
//     the command, the Value), whose disposal disarms the rule.
struct GUI_FORMS_API RuleAction final {
    RuleActionKind kind{RuleActionKind::none};
    void* context{};
    void (*function)(){};
    RuleProgramCall call{};
    std::shared_ptr<const void> retained{};
    Command* command{};
    void* value{};
    RuleValueWrite write{};
    RuleActionArgument argument{};
    Control::WeakPtr control{};
    Component* anchor{};
};

// A rule. Evaluated when its trigger occurs: when every condition holds,
// its actions run in order. With `consume`, a firing stops the rules
// declared after it for that occurrence and the occurrence's default route
// (the focused control and the key commands for a key; the ancestors for a
// context-menu request).
struct GUI_FORMS_API Rule final {
    RuleTrigger trigger{};
    std::vector<RuleCondition> conditions{};
    std::vector<RuleAction> actions{};
    bool consume{};
};

// The vocabulary's names (snake_case, as the C ABI and diagnostics spell
// them); "none" for a value outside it.
[[nodiscard]] GUI_FORMS_API std::string_view rule_trigger_kind_name(RuleTriggerKind kind) noexcept;
[[nodiscard]] GUI_FORMS_API std::string_view rule_condition_kind_name(RuleConditionKind kind) noexcept;
[[nodiscard]] GUI_FORMS_API std::string_view rule_action_kind_name(RuleActionKind kind) noexcept;

namespace detail {

// The payload an interaction's argument gives a firing: a typed drag
// interaction's record (borrowed for the firing) and nothing for any other
// argument.
template <typename Argument>
void note_rule_payload(RuleFiring&, const Argument&) noexcept {}

inline void note_rule_payload(RuleFiring& firing, const DragInteraction& interaction) noexcept {
    firing.drag = &interaction;
}

// interaction's connection to a control's typed event: each emission fires
// the tap; of the event's arguments only a typed drag interaction's record
// is carried (RuleFiring::drag), otherwise the trigger has no payload
// beyond its control.
template <typename... Arguments>
class RuleEventSource final : public RuleSource {
public:
    explicit RuleEventSource(Event<Arguments...>& event) noexcept : event_(&event) {}

    [[nodiscard]] SubscriptionToken connect(RuleTap& tap) override {
        tap_ = &tap;
        const Delegate<Arguments...> forward =
            Delegate<Arguments...>::template bind<RuleEventSource, &RuleEventSource::fire>(*this);
        SubscriptionToken subscription = (*event_).subscribe(forward);
        return subscription;
    }
    [[nodiscard]] const void* identity() const noexcept override { return event_; }

    void fire(Arguments... arguments) {
        RuleFiring firing{};
        (note_rule_payload(firing, arguments), ...);
        fire_rule_tap(*tap_, firing);
    }

private:
    Event<Arguments...>* event_;
    RuleTap* tap_{};
};

// value_changed's connection to a Value<T>: each change fires the tap with
// the new value in the firing's payload.
template <typename T>
class RuleValueSource final : public RuleSource {
public:
    explicit RuleValueSource(Value<T>& value) noexcept : value_(&value) {}

    [[nodiscard]] SubscriptionToken connect(RuleTap& tap) override {
        tap_ = &tap;
        const Delegate<T> forward = Delegate<T>::template bind<RuleValueSource, &RuleValueSource::fire>(*this);
        SubscriptionToken subscription = (*value_).changed().subscribe(forward);
        return subscription;
    }
    [[nodiscard]] const void* identity() const noexcept override { return value_; }

    void fire(T changed) {
        RuleFiring firing{};
        firing.value.present = true;
        if constexpr (std::is_floating_point_v<T>) {
            firing.value.real = static_cast<double>(changed);
        } else {
            firing.value.integer = static_cast<std::int64_t>(changed);
        }
        fire_rule_tap(*tap_, firing);
    }

private:
    Value<T>* value_;
    RuleTap* tap_{};
};

template <typename Context>
void call_rule_program(void* context, void (*function)(), const RuleFiring& firing) {
    using Function = void (*)(Context&, const RuleFiring&);
    // A function pointer converted to another function pointer type and back
    // is the original pointer; action() made the conversion.
    const Function typed = reinterpret_cast<Function>(function);
    Context& target = *static_cast<Context*>(context);
    typed(target, firing);
}

template <typename T>
void write_rule_value(void* value, const RuleActionArgument& argument) {
    Value<T>& target = *static_cast<Value<T>*>(value);
    if constexpr (std::is_floating_point_v<T>) {
        const T written = static_cast<T>(argument.real);
        target.set(written);
    } else {
        const T written = static_cast<T>(argument.integer);
        target.set(written);
    }
}

GUI_FORMS_API void toggle_rule_value(void* value, const RuleActionArgument& argument);

} // namespace detail

// The triggers.
namespace trigger {

[[nodiscard]] GUI_FORMS_API RuleTrigger key_pressed(std::uint32_t physical_key);
[[nodiscard]] GUI_FORMS_API RuleTrigger key_released(std::uint32_t physical_key);
[[nodiscard]] GUI_FORMS_API RuleTrigger key_repeat(std::uint32_t physical_key);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_down(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_up(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger click(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_click(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger double_click(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_double_click(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger context_menu_requested(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_enter(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_hover(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_leave(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger mouse_capture_changed(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger focus_gained(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger focus_lost(Control& control);
[[nodiscard]] GUI_FORMS_API RuleTrigger command_executed(Command& command);

// Any interaction of `control`, named by its typed event, which must be one
// of `control`'s own (`(*slider).value_changed()`, `(*check).checked_changed()`).
template <typename... Arguments>
[[nodiscard]] RuleTrigger interaction(Control& control, Event<Arguments...>& event) {
    RuleTrigger made{};
    made.kind = RuleTriggerKind::interaction;
    made.control = control.weak_from_this();
    made.source = std::make_shared<detail::RuleEventSource<Arguments...>>(event);
    Component& anchor = control;
    made.anchor = &anchor;
    return made;
}

template <typename T>
[[nodiscard]] RuleTrigger value_changed(Value<T>& value) {
    RuleTrigger made{};
    made.kind = RuleTriggerKind::value_changed;
    made.source = std::make_shared<detail::RuleValueSource<T>>(value);
    Component& anchor = value;
    made.anchor = &anchor;
    return made;
}

} // namespace trigger

// The conditions. Negation is through the `not_` forms only; OR is two rules.
namespace condition {

[[nodiscard]] GUI_FORMS_API RuleCondition key_down(std::uint32_t physical_key);
[[nodiscard]] GUI_FORMS_API RuleCondition not_key_down(std::uint32_t physical_key);
[[nodiscard]] GUI_FORMS_API RuleCondition modifiers(Modifier exact);
[[nodiscard]] GUI_FORMS_API RuleCondition hovering(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_hovering(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition pressed(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_pressed(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition focused(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_focused(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition focus_within(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_focus_within(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition enabled(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_enabled(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition visible(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_visible(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition window_active();
[[nodiscard]] GUI_FORMS_API RuleCondition not_window_active();
// The drag engine's levels (GF_RULE_CONDITION_DRAGGING and _DROP_CANDIDATE
// in the C ABI).
[[nodiscard]] GUI_FORMS_API RuleCondition dragging(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_dragging(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition drop_candidate(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition not_drop_candidate(Control& control);
[[nodiscard]] GUI_FORMS_API RuleCondition is_true(Value<bool>& flag);
[[nodiscard]] GUI_FORMS_API RuleCondition not_true(Value<bool>& flag);
[[nodiscard]] GUI_FORMS_API RuleCondition equals(Value<int>& number, int expected);
[[nodiscard]] GUI_FORMS_API RuleCondition not_equals(Value<int>& number, int expected);

} // namespace condition

// A program action: `function` called with `context` each time the rule
// fires. A context that is a Component disarms the rule when it is
// disposed; any other context must outlive the rule.
template <typename Context>
[[nodiscard]] RuleAction action(Context& context, void (*function)(Context&, const RuleFiring&)) {
    RuleAction made{};
    made.kind = RuleActionKind::program;
    made.context = static_cast<void*>(&context);
    made.function = reinterpret_cast<void (*)()>(function);
    made.call = &detail::call_rule_program<Context>;
    if constexpr (std::is_base_of_v<Component, Context>) {
        Component& anchor = context;
        made.anchor = &anchor;
    }
    return made;
}

// The built-in actions, which keep simple rules free of program code.
namespace builtin {

// Executes `command` through the firing window's command route
// (Command::execute_routed), as a menu or shortcut does.
[[nodiscard]] GUI_FORMS_API RuleAction invoke(Command& command);

// Writes `written` to `value` (Value::set: an equal value does nothing, a
// rejected one throws and stops the firing's later actions).
template <typename T>
[[nodiscard]] RuleAction set(Value<T>& value, T written) {
    RuleAction made{};
    made.kind = RuleActionKind::set;
    made.value = static_cast<void*>(&value);
    made.write = &detail::write_rule_value<T>;
    if constexpr (std::is_floating_point_v<T>) {
        made.argument.real = static_cast<double>(written);
    } else {
        made.argument.integer = static_cast<std::int64_t>(written);
    }
    Component& anchor = value;
    made.anchor = &anchor;
    return made;
}

// Writes the opposite of `value`'s current value.
[[nodiscard]] GUI_FORMS_API RuleAction toggle(Value<bool>& value);

// Gives `control` the keyboard focus (SurfaceRoot::request_focus on the
// firing window); a control that cannot take it is left as it is.
[[nodiscard]] GUI_FORMS_API RuleAction focus(Control& control);

// Enters `control`'s keyboard drag mode (Control::begin_keyboard_drag), so
// that an application can bind its own move key; a control that declares
// no drag, or a window already running one, is left as it is.
[[nodiscard]] GUI_FORMS_API RuleAction begin_keyboard_drag(Control& control);

} // namespace builtin

} // namespace gui_forms
