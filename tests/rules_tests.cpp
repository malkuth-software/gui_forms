// A window's rules (notes:planning APP_WINDOW_RULES_DESIGN Part C): every
// trigger and condition, the built-in and program actions, evaluation
// order, consume (later rules and the default route), duplicates,
// refusals, stale and revoked rules, reentrancy (rules added or revoked
// while firing, a throwing action), the one-condition spellings and their
// one action per interaction (FD-6 Q2 and Q3).

#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/component/component/component.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/rules/rule_set/rule_set.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/value/value.hpp"
#include "gui_forms/window/window.hpp"
#include "support/routed_input_probe.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// What the program actions saw, in order.
struct Journal final {
    std::vector<std::string> entries;
    std::vector<RuleTriggerKind> triggers;
    std::uint32_t last_clicks{};
    std::int64_t last_value{};
    bool last_had_state{};
    bool last_had_key{};
    // The control's own subscribers' calls, and how many had run when the
    // last ordering rule fired (mouse_rules_run_before_subscribers).
    std::uint32_t subscriber_calls{};
    std::uint32_t subscriber_calls_at_fire{};
    std::uint32_t activations{};
};

void note(Journal& journal, const char* name, const RuleFiring& firing) {
    journal.entries.push_back(name);
    journal.triggers.push_back(firing.trigger);
    journal.last_had_state = firing.state != nullptr && firing.window != nullptr;
    journal.last_had_key = firing.key != nullptr;
    if (firing.mouse != nullptr) journal.last_clicks = (*firing.mouse).clicks;
    if (firing.value.present) journal.last_value = firing.value.integer;
}

void record_a(Journal& journal, const RuleFiring& firing) { note(journal, "a", firing); }
void record_b(Journal& journal, const RuleFiring& firing) { note(journal, "b", firing); }
void record_c(Journal& journal, const RuleFiring& firing) { note(journal, "c", firing); }

void record_order(Journal& journal, const RuleFiring& firing) {
    journal.subscriber_calls_at_fire = journal.subscriber_calls;
    note(journal, "order", firing);
}

// The control's own subscribers, beside the rules.
struct Subscribers final {
    Journal* journal{};
    void plain() { ++(*journal).subscriber_calls; }
    void mouse(const MouseEventArgs&) { ++(*journal).subscriber_calls; }
    void activated(ButtonBase&) { ++(*journal).activations; }
};

void throw_now(Journal& journal, const RuleFiring& firing) {
    note(journal, "throw", firing);
    throw std::runtime_error("rule action failed");
}

// A control that counts the keys the control route gives it.
class KeyProbe final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    explicit KeyProbe(StableId id) : gui_forms::tests::RoutedInputProbe<Control>(std::move(id)) {}
    void routed_key(KeyEvent& event) override {
        ++keys;
        event.handled = true;
    }
    int keys{};
};

// A page whose rules name its own members: a Component context.
class RulePage final : public Component {
public:
    Journal journal;
};

void page_noted(RulePage& page, const RuleFiring& firing) { note(page.journal, "page", firing); }

// Adds a rule from inside a firing: the new rule waits for the next
// occurrence.
struct Reentry final {
    RuleSet* rules{};
    Journal* journal{};
    RuleTrigger trigger{};
    RuleToken victim{};
    RuleToken added{};
};

void add_and_revoke(Reentry& reentry, const RuleFiring& firing) {
    note(*reentry.journal, "reentry", firing);
    Rule rule{};
    rule.trigger = reentry.trigger;
    rule.conditions.push_back(condition::window_active());
    rule.actions.push_back(action(*reentry.journal, &record_c));
    RuleAddResult made = (*reentry.rules).add(rule);
    reentry.added = made.token;
    reentry.victim.revoke();
}

KeyEvent key(KeyAction action_kind, std::uint32_t physical_key, Modifier modifiers = Modifier::none,
             bool repeat = false) {
    KeyEvent made{};
    made.action = action_kind;
    made.physical_key = physical_key;
    made.modifiers = modifiers;
    made.repeat = repeat;
    return made;
}

// The window, a key probe that takes the focus and two buttons.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<KeyProbe> probe;
    std::shared_ptr<Button> ok;
    std::shared_ptr<Button> cancel;
};

Fixture make_fixture() {
    Fixture value;
    value.root = make_control<Panel>(StableId("root"));
    value.probe = make_control<KeyProbe>(StableId("root.probe"));
    value.ok = make_control<Button>(StableId("root.ok"), "OK");
    value.cancel = make_control<Button>(StableId("root.cancel"), "Cancel");
    (*value.probe).focusable = true;
    (*value.probe).set_requested_bounds({0.0, 0.0, 100.0, 40.0});
    (*value.ok).set_requested_bounds({0.0, 50.0, 100.0, 30.0});
    (*value.cancel).set_requested_bounds({120.0, 50.0, 100.0, 30.0});
    (*value.root).add_child(value.probe);
    (*value.root).add_child(value.ok);
    (*value.root).add_child(value.cancel);
    return value;
}

void click_at(Window& window, Point position, std::uint32_t clicks = 1U) {
    PointerEvent move{PointerAction::move, PointerButton::none, position};
    move.click_count = 0U;
    static_cast<void>(window.dispatch_pointer(move));
    PointerEvent press{PointerAction::down, PointerButton::primary, position};
    press.click_count = clicks;
    PointerEvent release{PointerAction::up, PointerButton::primary, position};
    release.click_count = clicks;
    static_cast<void>(window.dispatch_pointer(press));
    static_cast<void>(window.dispatch_pointer(release));
}

void key_triggers_and_the_control_route() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    require(window.request_focus(fixture.probe), "the probe takes the focus");
    Journal journal;
    RuleSet& rules = window.rules();

    Rule pressed{};
    pressed.trigger = trigger::key_pressed(PhysicalKey::q);
    pressed.actions.push_back(action(journal, &record_a));
    require(rules.add(pressed).refusal == RuleRefusal::none, "a key rule is armed");
    Rule released{};
    released.trigger = trigger::key_released(PhysicalKey::q);
    released.actions.push_back(action(journal, &record_b));
    require(rules.add(released).refusal == RuleRefusal::none, "a release rule is armed");
    Rule repeated{};
    repeated.trigger = trigger::key_repeat(PhysicalKey::q);
    repeated.actions.push_back(action(journal, &record_c));
    require(rules.add(repeated).refusal == RuleRefusal::none, "a repeat rule is armed");

    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::q)));
    require(journal.entries.size() == 1U && journal.entries[0] == "a" && journal.last_had_key &&
                journal.last_had_state,
            "a press fires the press rule with the key and the state");
    require((*fixture.probe).keys == 1, "a rule that does not consume leaves the key to the focused control");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::q, Modifier::none, true)));
    require(journal.entries.size() == 2U && journal.entries[1] == "c", "the platform's repeat fires only the repeat rule");
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::q)));
    require(journal.entries.size() == 3U && journal.entries[2] == "b", "a release fires the release rule");

    Rule consuming{};
    consuming.trigger = trigger::key_pressed(PhysicalKey::w);
    consuming.actions.push_back(action(journal, &record_a));
    consuming.consume = true;
    require(rules.add(consuming).refusal == RuleRefusal::none, "a consuming key rule is armed");
    const int keys_before = (*fixture.probe).keys;
    const bool handled = window.dispatch_key(key(KeyAction::down, PhysicalKey::w));
    require(handled && (*fixture.probe).keys == keys_before,
            "a consuming key rule ends the key before the focused control");
    require(rules.statistics().consumed == 1U, "the consumption is counted");

    // modifiers (exact) and key_down read the window's input state.
    Rule chord{};
    chord.trigger = trigger::key_pressed(PhysicalKey::s);
    chord.conditions.push_back(condition::modifiers(Modifier::control));
    chord.actions.push_back(action(journal, &record_b));
    require(rules.add(chord).refusal == RuleRefusal::none, "a chord rule is armed");
    const std::size_t before_chord = journal.entries.size();
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::s)));
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::s)));
    require(journal.entries.size() == before_chord, "without its modifiers the chord rule does not fire");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::s, Modifier::control | Modifier::shift)));
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::s, Modifier::control | Modifier::shift)));
    require(journal.entries.size() == before_chord, "modifiers are matched exactly");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::s, Modifier::control)));
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::s, Modifier::control)));
    require(journal.entries.size() == before_chord + 1U, "with exactly its modifiers the chord rule fires");

    Rule held{};
    held.trigger = trigger::key_pressed(PhysicalKey::b);
    held.conditions.push_back(condition::key_down(PhysicalKey::a));
    held.actions.push_back(action(journal, &record_c));
    require(rules.add(held).refusal == RuleRefusal::none, "a held-key rule is armed");
    const std::size_t before_held = journal.entries.size();
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::b)));
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::b)));
    require(journal.entries.size() == before_held, "B alone does not fire the A+B rule");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::a)));
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::b)));
    require(journal.entries.size() == before_held + 1U, "B while A is held fires it");
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::b)));
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::a)));
}

void order_consume_and_duplicates() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    Journal journal;
    RuleSet& rules = window.rules();

    Rule first{};
    first.trigger = trigger::click(*fixture.ok);
    first.actions.push_back(action(journal, &record_a));
    first.actions.push_back(action(journal, &record_b));
    Rule second{};
    second.trigger = trigger::click(*fixture.ok);
    second.conditions.push_back(condition::enabled(*fixture.ok));
    second.actions.push_back(action(journal, &record_c));
    second.consume = true;
    Rule third{};
    third.trigger = trigger::click(*fixture.ok);
    third.conditions.push_back(condition::visible(*fixture.ok));
    third.actions.push_back(action(journal, &record_a));
    require(rules.add(first).refusal == RuleRefusal::none && rules.add(second).refusal == RuleRefusal::none &&
                rules.add(third).refusal == RuleRefusal::none,
            "three rules on one trigger are armed");
    click_at(window, {50.0, 65.0});
    require(journal.entries.size() == 3U && journal.entries[0] == "a" && journal.entries[1] == "b" &&
                journal.entries[2] == "c",
            "rules fire in declaration order, actions in listed order, and consume stops the later rule");

    require(rules.add(first).refusal == RuleRefusal::duplicate, "the same trigger and no conditions is a duplicate");
    Rule reordered{};
    reordered.trigger = trigger::click(*fixture.ok);
    reordered.conditions.push_back(condition::visible(*fixture.ok));
    reordered.conditions.push_back(condition::enabled(*fixture.ok));
    reordered.actions.push_back(action(journal, &record_a));
    require(rules.add(reordered).refusal == RuleRefusal::none, "a different condition set is no duplicate");
    Rule swapped{};
    swapped.trigger = trigger::click(*fixture.ok);
    swapped.conditions.push_back(condition::enabled(*fixture.ok));
    swapped.conditions.push_back(condition::visible(*fixture.ok));
    swapped.actions.push_back(action(journal, &record_b));
    require(rules.add(swapped).refusal == RuleRefusal::duplicate, "a condition set is compared as a set");
    Rule other_trigger{};
    other_trigger.trigger = trigger::mouse_click(*fixture.ok);
    other_trigger.actions.push_back(action(journal, &record_a));
    require(rules.add(other_trigger).refusal == RuleRefusal::none, "another trigger is no duplicate");
    require(rules.statistics().refused == 2U, "the refusals are counted");
}

// Owner rulings FD-6 Q2 and Q3 (a): on() takes a control's mouse events and
// its semantic events from the one trigger namespace, and a second on() for
// the same interaction on the same control is refused until the first is
// revoked; the replacement then fires instead of the first.
void one_action_per_interaction() {
    Fixture fixture = make_fixture();
    const std::shared_ptr<CheckBox> check = make_control<CheckBox>(StableId("root.check"));
    (*check).set_requested_bounds({0.0, 90.0, 100.0, 24.0});
    (*fixture.root).add_child(check);
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    Journal journal;
    RuleSet& rules = window.rules();

    RuleAddResult clicked = on(rules, trigger::click(*fixture.ok), journal, &record_a);
    RuleAddResult checked = on(rules, trigger::interaction(*check, (*check).checked_changed()), journal, &record_a);
    require(clicked.refusal == RuleRefusal::none && checked.refusal == RuleRefusal::none,
            "on() takes a mouse event and a semantic event from one namespace");
    const RuleAddResult second_click = on(rules, trigger::click(*fixture.ok), journal, &record_b);
    const RuleAddResult second_check =
        on(rules, trigger::interaction(*check, (*check).checked_changed()), journal, &record_b);
    require(second_click.refusal == RuleRefusal::duplicate && second_check.refusal == RuleRefusal::duplicate &&
                second_click.token.state() == RuleTokenState::empty &&
                second_check.token.state() == RuleTokenState::empty && rules.armed_count() == 2U,
            "a second on() for the same interaction is refused and changes nothing");
    require(on(rules, trigger::click(*fixture.cancel), journal, &record_b).refusal == RuleRefusal::none,
            "the same interaction on another control is no duplicate");
    click_at(window, {50.0, 65.0});
    (*check).checked = true;
    require(journal.entries.size() == 2U && journal.entries[0] == "a" && journal.entries[1] == "a",
            "the first actions alone fire");

    clicked.token.revoke();
    checked.token.revoke();
    RuleAddResult replaced_click = on(rules, trigger::click(*fixture.ok), journal, &record_c);
    RuleAddResult replaced_check =
        on(rules, trigger::interaction(*check, (*check).checked_changed()), journal, &record_c);
    require(replaced_click.refusal == RuleRefusal::none && replaced_check.refusal == RuleRefusal::none,
            "revoking the first frees the interaction for a replacement");
    click_at(window, {50.0, 65.0});
    (*check).checked = false;
    require(journal.entries.size() == 4U && journal.entries[2] == "c" && journal.entries[3] == "c" &&
                journal.triggers[2] == RuleTriggerKind::click && journal.triggers[3] == RuleTriggerKind::interaction,
            "the replacements fire instead of the revoked actions");
}

void refusals_and_limits() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    Journal journal;
    RuleSet& rules = window.rules();

    Rule no_actions{};
    no_actions.trigger = trigger::key_pressed(PhysicalKey::a);
    require(rules.add(no_actions).refusal == RuleRefusal::no_actions, "a rule needs an action");
    Rule no_key{};
    no_key.trigger = trigger::key_pressed(0U);
    no_key.actions.push_back(action(journal, &record_a));
    require(rules.add(no_key).refusal == RuleRefusal::malformed_trigger, "a key trigger names a key");
    Rule no_trigger{};
    no_trigger.actions.push_back(action(journal, &record_a));
    require(rules.add(no_trigger).refusal == RuleRefusal::malformed_trigger, "a rule needs a trigger");
    Rule crowded{};
    crowded.trigger = trigger::key_pressed(PhysicalKey::a);
    for (std::size_t index = 0U; index <= maximum_rule_conditions; ++index) {
        crowded.conditions.push_back(condition::window_active());
    }
    crowded.actions.push_back(action(journal, &record_a));
    require(rules.add(crowded).refusal == RuleRefusal::too_many_conditions, "a rule holds at most eight conditions");
    Rule busy{};
    busy.trigger = trigger::key_pressed(PhysicalKey::a);
    for (std::size_t index = 0U; index <= maximum_rule_actions; ++index) {
        busy.actions.push_back(action(journal, &record_a));
    }
    require(rules.add(busy).refusal == RuleRefusal::too_many_actions, "a rule holds at most eight actions");
    Rule blank_action{};
    blank_action.trigger = trigger::key_pressed(PhysicalKey::a);
    blank_action.actions.push_back(RuleAction{});
    require(rules.add(blank_action).refusal == RuleRefusal::malformed_action, "an action needs a kind");
    Rule blank_condition{};
    blank_condition.trigger = trigger::key_pressed(PhysicalKey::a);
    blank_condition.conditions.push_back(RuleCondition{});
    blank_condition.actions.push_back(action(journal, &record_a));
    require(rules.add(blank_condition).refusal == RuleRefusal::malformed_condition, "a condition needs a kind");

    Value<bool> gone(false);
    gone.dispose();
    Rule disposed{};
    disposed.trigger = trigger::key_pressed(PhysicalKey::a);
    disposed.conditions.push_back(condition::is_true(gone));
    disposed.actions.push_back(action(journal, &record_a));
    require(rules.add(disposed).refusal == RuleRefusal::disposed_reference, "a disposed Value is refused");
    require(rules.armed_count() == 0U, "no refused rule was armed");

    for (std::size_t index = 0U; index < maximum_rules; ++index) {
        Rule many{};
        many.trigger = trigger::key_pressed(static_cast<std::uint32_t>(0x1000U + index));
        many.actions.push_back(action(journal, &record_a));
        require(rules.add(many).refusal == RuleRefusal::none, "rules up to the limit are armed");
    }
    Rule past{};
    past.trigger = trigger::key_pressed(PhysicalKey::z);
    past.actions.push_back(action(journal, &record_a));
    require(rules.add(past).refusal == RuleRefusal::limit_reached, "the window holds at most maximum_rules rules");
}

void stale_revoked_and_dropped_tokens() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    Journal journal;
    RuleSet& rules = window.rules();

    RuleToken dropped_kept{};
    {
        const RuleAddResult dropped = on(rules, trigger::click(*fixture.ok), journal, &record_a);
        dropped_kept = dropped.token;
    }
    require(dropped_kept.state() == RuleTokenState::armed, "a token observes its rule");
    RuleAddResult revocable = on(rules, trigger::click(*fixture.cancel), journal, &record_b);
    revocable.token.revoke();
    require(revocable.token.state() == RuleTokenState::revoked && rules.armed_count() == 1U, "a token revokes early");
    click_at(window, {170.0, 65.0});
    require(journal.entries.empty(), "a revoked rule does not fire");
    revocable.token.revoke();
    require(revocable.token.state() == RuleTokenState::revoked, "revoking twice changes nothing");
    require(RuleToken{}.state() == RuleTokenState::empty, "a default token is empty");

    // A rule naming a destroyed control is disarmed and its token stale.
    Rule naming{};
    naming.trigger = trigger::key_pressed(PhysicalKey::k);
    naming.conditions.push_back(condition::not_focused(*fixture.cancel));
    naming.actions.push_back(action(journal, &record_c));
    const RuleAddResult named = rules.add(naming);
    std::shared_ptr<Button> removed =
        std::static_pointer_cast<Button>((*fixture.root).remove_child((*fixture.cancel).runtime_id()));
    fixture.cancel.reset();
    removed.reset();
    require(named.token.state() == RuleTokenState::stale, "a rule naming a destroyed control is stale");
    require(rules.armed_count() == 1U, "the stale rule is not armed");

    // A Component context disarms its rules when it is disposed.
    RulePage page;
    const RuleAddResult paged = on(rules, trigger::key_pressed(PhysicalKey::p), page, &page_noted);
    page.dispose();
    require(paged.token.state() == RuleTokenState::stale, "a rule whose context is disposed is stale");

    // A window's rules end with it.
    RuleToken outlived{};
    {
        Fixture inner = make_fixture();
        Window short_lived(inner.root, {300.0, 120.0});
        outlived = on(short_lived.rules(), trigger::click(*inner.ok), journal, &record_a).token;
    }
    require(outlived.state() == RuleTokenState::stale, "a rule of a destroyed window reads stale");
}

void builtins_and_model_conditions() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    RuleSet& rules = window.rules();
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    Journal journal;
    const RuleAddResult executed = on(rules, trigger::command_executed(*save), journal, &record_a);
    require(executed.refusal == RuleRefusal::none, "a command rule is armed");
    Value<int> mode(0);
    Value<bool> flag(false);

    require(on(rules, trigger::key_pressed(PhysicalKey::i), builtin::invoke(*save)).refusal == RuleRefusal::none,
            "invoke is armed");
    require(on(rules, trigger::key_pressed(PhysicalKey::m), builtin::set(mode, 3)).refusal == RuleRefusal::none,
            "set is armed");
    require(on(rules, trigger::key_pressed(PhysicalKey::t), builtin::toggle(flag)).refusal == RuleRefusal::none,
            "toggle is armed");
    require(on(rules, trigger::key_pressed(PhysicalKey::f), builtin::focus(*fixture.ok)).refusal == RuleRefusal::none,
            "focus is armed");

    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::i)));
    require(journal.entries.size() == 1U && journal.triggers[0] == RuleTriggerKind::command_executed,
            "invoke executes the command through the route, which fires command_executed");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::m)));
    require(mode.get() == 3, "set writes the Value");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::t)));
    require(flag.get(), "toggle flips the Value");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::t)));
    require(!flag.get(), "toggle flips it back");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::f)));
    require(window.focused_control() == fixture.ok, "focus moves the keyboard focus");

    // is_true and equals read bound models; value_changed carries the value.
    Rule gated{};
    gated.trigger = trigger::key_pressed(PhysicalKey::g);
    gated.conditions.push_back(condition::is_true(flag));
    gated.conditions.push_back(condition::equals(mode, 3));
    gated.actions.push_back(action(journal, &record_b));
    require(rules.add(gated).refusal == RuleRefusal::none, "a model-gated rule is armed");
    const std::size_t before = journal.entries.size();
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::g)));
    require(journal.entries.size() == before, "a false flag holds the rule back");
    flag.set(true);
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::g)));
    require(journal.entries.size() == before + 1U, "a true flag and the expected number let it fire");
    Rule changed{};
    changed.trigger = trigger::value_changed(mode);
    changed.conditions.push_back(condition::not_equals(mode, 0));
    changed.actions.push_back(action(journal, &record_c));
    require(rules.add(changed).refusal == RuleRefusal::none, "a value rule is armed");
    mode.set(7);
    require(journal.entries.back() == "c" && journal.last_value == 7, "value_changed carries the new value");
    mode.set(0);
    require(journal.last_value == 7, "a condition reads the Value after the change");
}

void control_events_and_control_conditions() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    RuleSet& rules = window.rules();
    Journal journal;

    Rule down{};
    down.trigger = trigger::mouse_down(*fixture.ok);
    down.conditions.push_back(condition::pressed(*fixture.ok));
    down.conditions.push_back(condition::hovering(*fixture.ok));
    down.actions.push_back(action(journal, &record_a));
    require(rules.add(down).refusal == RuleRefusal::none, "a mouse_down rule is armed");
    require(on(rules, trigger::mouse_up(*fixture.ok), journal, &record_b).refusal == RuleRefusal::none,
            "a mouse_up rule is armed");
    require(on(rules, trigger::mouse_click(*fixture.ok), journal, &record_c).refusal == RuleRefusal::none,
            "a mouse_click rule is armed");
    click_at(window, {50.0, 65.0});
    require(journal.entries.size() == 3U && journal.triggers[0] == RuleTriggerKind::mouse_down &&
                journal.triggers[1] == RuleTriggerKind::mouse_up && journal.triggers[2] == RuleTriggerKind::mouse_click,
            "the mouse rules fire in the raisers' order (MouseUp at the release's target phase, then MouseClick), "
            "with hover and press conditions holding on the press");
    require(journal.last_clicks == 1U, "the mouse payload carries the host's click count");

    // double_click on a control with standard_double_click (a panel).
    Rule doubled{};
    doubled.trigger = trigger::double_click(*fixture.root);
    doubled.conditions.push_back(condition::not_hovering(*fixture.ok));
    doubled.actions.push_back(action(journal, &record_a));
    require(rules.add(doubled).refusal == RuleRefusal::none, "a double_click rule is armed");
    const std::size_t before_double = journal.entries.size();
    click_at(window, {250.0, 110.0}, 1U);
    click_at(window, {250.0, 110.0}, 2U);
    require(journal.entries.size() == before_double + 1U && journal.triggers.back() == RuleTriggerKind::double_click,
            "the host's second press fires double_click once");

    // focus_gained and focus_lost; focused and focus_within.
    require(on(rules, trigger::focus_gained(*fixture.probe), journal, &record_b).refusal == RuleRefusal::none,
            "a focus_gained rule is armed");
    require(on(rules, trigger::focus_lost(*fixture.probe), journal, &record_c).refusal == RuleRefusal::none,
            "a focus_lost rule is armed");
    Rule within{};
    within.trigger = trigger::key_pressed(PhysicalKey::x);
    within.conditions.push_back(condition::focus_within(*fixture.root));
    within.conditions.push_back(condition::focused(*fixture.probe));
    within.actions.push_back(action(journal, &record_a));
    require(rules.add(within).refusal == RuleRefusal::none, "a focus rule is armed");
    require(window.request_focus(fixture.probe), "focus the probe");
    require(journal.triggers.back() == RuleTriggerKind::focus_gained, "gaining focus fires focus_gained");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::x)));
    require(journal.entries.back() == "a", "focused and focus_within hold for the focused descendant");
    require(window.request_focus(fixture.ok), "focus the button");
    require(journal.triggers.back() == RuleTriggerKind::focus_lost, "losing focus fires focus_lost");

    // context_menu_requested: a consuming rule marks the request handled.
    Rule menu{};
    menu.trigger = trigger::context_menu_requested(*fixture.ok);
    menu.actions.push_back(action(journal, &record_b));
    menu.consume = true;
    require(rules.add(menu).refusal == RuleRefusal::none, "a context menu rule is armed");
    const bool handled = window.dispatch_context_request(ContextRequestOrigin::keyboard, Point{});
    require(handled && journal.triggers.back() == RuleTriggerKind::context_menu_requested,
            "a consuming context menu rule handles the request");

    // window_active and the typed interaction trigger.
    const std::shared_ptr<CheckBox> check = make_control<CheckBox>(StableId("root.check"));
    (*fixture.root).add_child(check);
    Rule interacted{};
    interacted.trigger = trigger::interaction(*check, (*check).checked_changed());
    interacted.conditions.push_back(condition::not_window_active());
    interacted.actions.push_back(action(journal, &record_c));
    require(rules.add(interacted).refusal == RuleRefusal::none, "an interaction rule is armed");
    window.set_active(false);
    (*check).checked = true;
    require(journal.triggers.back() == RuleTriggerKind::interaction, "the interaction fires while the window is inactive");
    window.set_active(true);
    const std::size_t before_active = journal.entries.size();
    (*check).checked = false;
    require(journal.entries.size() == before_active, "not_window_active holds it back while active");
}

void reentrancy_and_failures() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    RuleSet& rules = window.rules();
    Journal journal;

    Reentry reentry{};
    reentry.rules = &rules;
    reentry.journal = &journal;
    reentry.trigger = trigger::key_pressed(PhysicalKey::r);
    RuleAddResult adder = on(rules, trigger::key_pressed(PhysicalKey::r), reentry, &add_and_revoke);
    Rule victim{};
    victim.trigger = trigger::key_pressed(PhysicalKey::r);
    victim.conditions.push_back(condition::visible(*fixture.ok));
    victim.actions.push_back(action(journal, &record_b));
    reentry.victim = rules.add(victim).token;
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::r)));
    require(journal.entries.size() == 1U && journal.entries[0] == "reentry",
            "a rule revoked during a firing is not evaluated, and one added waits for the next occurrence");
    adder.token.revoke();
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::r)));
    require(reentry.added.state() == RuleTokenState::armed, "the rule added while firing is armed");

    Rule failing{};
    failing.trigger = trigger::key_pressed(PhysicalKey::e);
    failing.actions.push_back(action(journal, &throw_now));
    failing.actions.push_back(action(journal, &record_a));
    require(rules.add(failing).refusal == RuleRefusal::none, "a failing rule is armed");
    Rule after{};
    after.trigger = trigger::key_pressed(PhysicalKey::e);
    after.conditions.push_back(condition::visible(*fixture.ok));
    after.actions.push_back(action(journal, &record_b));
    require(rules.add(after).refusal == RuleRefusal::none, "a later rule is armed");
    const std::size_t before = journal.entries.size();
    bool threw = false;
    try {
        static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::e)));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && journal.entries.size() == before + 1U && journal.entries.back() == "throw",
            "a throwing action stops the later actions and rules and reaches the dispatcher");
    static_cast<void>(window.dispatch_key(key(KeyAction::up, PhysicalKey::e)));
    require(on(rules, trigger::key_pressed(PhysicalKey::n), journal, &record_c).refusal == RuleRefusal::none,
            "the rule set takes new rules after a failure");
    static_cast<void>(window.dispatch_key(key(KeyAction::down, PhysicalKey::n)));
    require(journal.entries.back() == "c", "and fires them");
}

// Q-C5: the window's rules see MouseDown, MouseUp and the clicks before the
// control's subscribers, and a consuming rule keeps the event from them (a
// consumed Click also from the button's activation).
void mouse_rules_run_before_subscribers() {
    Fixture fixture = make_fixture();
    Window window(fixture.root, {300.0, 120.0});
    window.perform_layout();
    RuleSet& rules = window.rules();
    Journal journal;
    Subscribers subscribers{};
    subscribers.journal = &journal;
    Button& ok = *fixture.ok;
    const SubscriptionToken down_subscription =
        ok.mouse_down().subscribe(Delegate<const MouseEventArgs&>::bind<Subscribers, &Subscribers::mouse>(subscribers));
    const SubscriptionToken up_subscription =
        ok.mouse_up().subscribe(Delegate<const MouseEventArgs&>::bind<Subscribers, &Subscribers::mouse>(subscribers));
    const SubscriptionToken click_subscription =
        ok.click().subscribe(Delegate<>::bind<Subscribers, &Subscribers::plain>(subscribers));
    const SubscriptionToken activation_subscription =
        ok.clicked().subscribe(Delegate<ButtonBase&>::bind<Subscribers, &Subscribers::activated>(subscribers));

    // A rule added after the subscribers still runs first.
    require(on(rules, trigger::mouse_down(ok), journal, &record_order).refusal == RuleRefusal::none,
            "an ordering mouse_down rule is armed");
    click_at(window, {50.0, 65.0});
    require(journal.entries.size() == 1U && journal.subscriber_calls_at_fire == 0U,
            "the mouse_down rule fires before the control's MouseDown subscribers");
    require(journal.subscriber_calls == 3U && journal.activations == 1U,
            "a rule that does not consume leaves MouseDown, MouseUp, Click and the activation to the control");

    // A consuming Click rule: no Click subscriber and no activation; MouseUp
    // is its own event and still reaches its subscriber.
    Rule swallow{};
    swallow.trigger = trigger::click(ok);
    swallow.actions.push_back(action(journal, &record_a));
    swallow.consume = true;
    require(rules.add(swallow).refusal == RuleRefusal::none, "a consuming click rule is armed");
    click_at(window, {50.0, 65.0});
    require(journal.entries.back() == "a" && journal.triggers.back() == RuleTriggerKind::click,
            "the consuming click rule fires");
    require(journal.subscriber_calls == 5U && journal.activations == 1U,
            "a consumed Click reaches neither its subscribers nor the activation; MouseDown and MouseUp do");

    // A consuming MouseUp rule keeps MouseUp from its subscriber.
    Rule up{};
    up.trigger = trigger::mouse_up(ok);
    up.actions.push_back(action(journal, &record_b));
    up.consume = true;
    require(rules.add(up).refusal == RuleRefusal::none, "a consuming mouse_up rule is armed");
    click_at(window, {50.0, 65.0});
    require(journal.triggers.back() == RuleTriggerKind::mouse_up && journal.subscriber_calls == 6U,
            "a consumed MouseUp reaches no subscriber");
    require(rules.statistics().consumed == 3U, "each consumption is counted (Click twice, MouseUp once)");
}

} // namespace

int main() {
    key_triggers_and_the_control_route();
    order_consume_and_duplicates();
    one_action_per_interaction();
    refusals_and_limits();
    stale_revoked_and_dropped_tokens();
    builtins_and_model_conditions();
    control_events_and_control_conditions();
    mouse_rules_run_before_subscribers();
    reentrancy_and_failures();
    std::cout << "rules tests passed\n";
    return 0;
}
