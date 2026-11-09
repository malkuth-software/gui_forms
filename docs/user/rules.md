# Rules

A rule says "when this happens, and these things are true, do this". It has
one **trigger**, zero or more **conditions** that must all hold, and one or
more **actions** that run in order. A window keeps its rules in a
`RuleSet`, which it makes the first
time you call `window.rules()`.

Rules are data over a closed vocabulary, much as theme rules are. The
trigger, condition and action kinds listed below are the whole vocabulary,
and adding a word to it is a library change. Program code enters only
through named actions: plain functions with an explicit context. There are
no lambdas.

```cpp
#include <gui_forms/gui_forms.hpp>

namespace gf = gui_forms;

class SearchPage final : public gf::Component {
public:
    gf::Value<bool> live_search{false};
    int searches{};
};

// Named actions are declared before the rules that use them.
void search_now(SearchPage& page, const gf::RuleFiring& firing) {
    ++page.searches;
}

void arm_rules(gf::Window& window, SearchPage& page, gf::Control& ok, gf::Control& search) {
    gf::Rule rule{};
    rule.trigger = gf::trigger::key_pressed(gf::PhysicalKey::q);
    rule.conditions = {gf::condition::hovering(ok), gf::condition::not_focused(search)};
    rule.actions = {gf::action(page, &search_now), gf::builtin::toggle(page.live_search)};
    rule.consume = true;
    const gf::RuleAddResult added = window.rules().add(rule);
    // added.refusal says why a rule was refused; added.token observes it.
}
```

## Evaluation

For one occurrence of a trigger, the window evaluates its rules on that
trigger in the order they were added. Every rule whose conditions all hold
fires, and its actions run in the order listed.

- **Consume.** A firing rule with `consume = true` stops the rules added
  after it. It also stops the occurrence's default route. A consumed key
  reaches no control and no key command. A consumed context-menu request
  reaches none of the control's ancestors.
- **Rules and input order.** Key rules see a key before the control route,
  where WinForms' `ProcessCmdKey` sits. They run before the preemptive
  accelerators and before the focused control's preview. A rule such as "Q
  while hovering OK" therefore keeps a focused text box from typing q only
  when the rule says `consume`. Rules on MouseDown, MouseUp and the clicks
  run before the control's own subscribers. A consuming one keeps the event
  from them, and a consumed Click also keeps a button from activating. The
  other control-event rules (hover, capture, focus, the context menu) run
  when the control raises the event. See
  [Mouse events and context requests](mouse-events.md#rules-and-the-mouse-events).
- **Adding or revoking during a firing.** A rule added by an action is first
  evaluated by the next occurrence. A rule revoked during a firing is not
  evaluated again, even later in the same occurrence.
- **Failures.** An action that throws stops the later actions of its firing
  and the later rules. The exception then reaches the code that raised the
  trigger, as an event handler's exception does.
- **Duplicates.** A rule with the same trigger and the same set of
  conditions as an armed rule is refused. Put several actions in one rule
  instead.

`RuleFiring` carries what the trigger reported:

| Field | Set for |
| --- | --- |
| `key` | the key triggers |
| `mouse` | `mouse_down`, `mouse_up`, `mouse_click`, `mouse_double_click` |
| `context_request` | `context_menu_requested`; an action may set its `handled` |
| `invocation` | `command_executed` |
| `value` | `value_changed`; the new value |
| `drag` | `interaction` on a typed drag interaction (`drag_started`, `drag_target_changed`, `drag_dropped`, `drag_ended`): the `DragInteraction` record, with the end kind |

The firing also carries `control` (the control the trigger names), `state`
(the window's input state when the trigger occurred) and `window`.

## Vocabulary

### Triggers (edges)

| Trigger | Fires on |
| --- | --- |
| `key_pressed(k)` | a key's press, without the platform's auto-repeat |
| `key_released(k)` | a key's release |
| `key_repeat(k)` | the platform's own auto-repeat (GUI.Forms times no holds of its own) |
| `mouse_down(c)`, `mouse_up(c)` | the control's MouseDown and MouseUp |
| `click(c)`, `mouse_click(c)` | the control's Click and MouseClick |
| `double_click(c)`, `mouse_double_click(c)` | the control's DoubleClick and MouseDoubleClick |
| `context_menu_requested(c)` | a context-menu request on the control, from the pointer or the keyboard |
| `mouse_enter(c)`, `mouse_hover(c)`, `mouse_leave(c)` | the pointer entering, resting on and leaving the control |
| `mouse_capture_changed(c)` | the control losing the pointer capture |
| `focus_gained(c)`, `focus_lost(c)` | the control gaining or losing the keyboard focus |
| `interaction(c, event)` | any interaction of the control, named by its typed event, for example `(*slider).value_changed()` |
| `command_executed(command)` | the command's execution |
| `value_changed(value)` | a `Value<T>`'s change |

Keys are physical keys (`PhysicalKey`, USB HID usages). The mouse events
follow WinForms' order and use the host's click count (see
{doc}`input-and-focus`).

### Conditions (levels)

| Condition | Holds when |
| --- | --- |
| `key_down(k)` | the key is held |
| `modifiers(m)` | the held modifiers are exactly `m` |
| `hovering(c)` | the pointer is over the control or one of its descendants |
| `pressed(c)` | the control is the window's pressed control |
| `focused(c)` | the control has the keyboard focus |
| `focus_within(c)` | the control or one of its descendants has the focus |
| `enabled(c)`, `visible(c)` | the control is effectively enabled, effectively visible |
| `window_active()` | the window is the active window |
| `is_true(flag)` | a `Value<bool>` holds true |
| `equals(number, n)` | a `Value<int>` holds `n` |
| `dragging(c)` | a drag is running on the control or one of its descendants |
| `drop_candidate(c)` | the control holds the running drag's selected drop candidate |

Every condition has a `not_` form, such as `not_focused(c)` or
`not_window_active()`. Negation is spelled only through these forms. To say
"or", write two rules.

### Actions

- `action(context, &function)` calls a named function
  `void function(Context&, const RuleFiring&)` with its context.
- `builtin::invoke(command)` executes a command through the window's command
  route, as a menu or shortcut does.
- `builtin::set(value, x)` writes a `Value<T>`.
- `builtin::toggle(flag)` writes the opposite of a `Value<bool>`.
- `builtin::focus(c)` gives a control the keyboard focus.
- `builtin::begin_keyboard_drag(c)` starts the control's keyboard move mode,
  for a control that can be dragged, so an application can bind its own
  move key.

`dragging`, `drop_candidate` and `begin_keyboard_drag` are C++ only for
now; the C ABI does not offer them yet (their numbers are reserved).

A control the drag engine drives (Splitter, SplitContainer) raises typed
drag interactions: `drag_started` when the drag is planned,
`drag_target_changed` when the selected drop candidate changes,
`drag_dropped` after a committing end and `drag_ended` after every end,
each with a `DragInteraction` (input mode, candidate or value, pointer and
end kind). A rule reaches them as `interaction(c, (*c).drag_ended())`, and
its program action reads the record through `firing.drag`. The C ABI sees
them by their schema names.

The built-in actions let simple rules work without any program code.

## The one-condition case

A rule with one trigger, no conditions and one action can be armed in one
call:

```cpp
gf::on(window.rules(), gf::trigger::click(*ok), page, &search_now);
gf::on(window.rules(), gf::trigger::key_pressed(gf::PhysicalKey::m), gf::builtin::toggle(page.live_search));
```

For events outside the rule vocabulary, such as a worker's completion or a
model's own event, use `gf::on(event, owner, &Owner::member)`
({doc}`values-commands-and-binding`).

## Lifetime

- **Ownership.** The window owns its rules, and they end with the window.
- **Disposed components.** A rule that names a component is disarmed when
  that component is disposed or destroyed. The component can be a control, a
  command, a `Value`, or a context that is a `Component`. The rule's token
  then reads `stale`.
- **Other contexts.** A context that is not a `Component` must outlive its
  rules.
- **Tokens.** A `RuleToken` observes its rule and does not own it. Dropping
  the token leaves the rule armed. `revoke()` ends the rule early.

Rules are not written in form documents in 0.5: a form never names program
code. A document that still writes a handler reference (`on-clicked: …;`)
is refused with E816 `form_handler_removed`; the program arms a
rule on the generated member instead, as the Gallery's `GalleryContext::attach`
and `examples/reference/window_rules.cpp` do. Rules that use only built-in
actions may come to documents after the 0.5 seal.

## The C ABI

`GF_TABLE_RULES` (`gui_forms/c/gf_abi.h`) adds rules to a window object from
C. The ABI has the same evaluation and lifetime rules, with these
differences:

- **Triggers.** The C trigger kinds are those above except `value_changed`.
  An interaction is named by its schema name.
- **Conditions.** The C condition kinds are those above except `is_true` and
  `equals`. A condition record carries `negated`.
- **Actions.** A program action is a `gf_callback`. Instead of writing a
  `Value`, `GF_RULE_ACTION_SET` and `GF_RULE_ACTION_TOGGLE` write a property
  of the target.

`add` gives a token, which `remove` frees, and `state` reports whether the
rule is armed or stale. The conformance kit ({doc}`c-abi-conformance-kit`)
exercises the table.
