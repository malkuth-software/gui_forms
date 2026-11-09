# Rules

A rule says "when this happens, and these things hold, do this": one
**trigger**, zero or more **conditions**, and ordered **actions**. A window
keeps its rules in `window.rules()` and owns them.

```cpp
void search_now(SearchPage& page, const gf::RuleFiring& firing);   // a named action

gf::Rule rule{};
rule.trigger = gf::trigger::key_pressed(gf::PhysicalKey::q);
rule.conditions = {gf::condition::hovering(*window.page_ok), gf::condition::not_focused(*window.page_search)};
rule.actions = {gf::action(page, &search_now), gf::builtin::toggle(page.live_search)};
rule.consume = true;
const gf::RuleAddResult added = window.rules().add(rule);   // added.token, or added.refusal
```

- **A closed vocabulary.** Triggers: keys (`key_pressed`, `key_released`,
  `key_repeat`), the mouse events, `click`, `double_click`,
  `context_menu_requested`, focus, `interaction(c, event)`,
  `command_executed`, `value_changed`. Conditions: `key_down`, `modifiers`,
  `hovering`, `pressed`, `focused`, `focus_within`, `enabled`, `visible`,
  `window_active`, `is_true`, `equals`, `dragging`, `drop_candidate`, each
  with a `not_` form.
- **Actions** are named functions `void f(Context&, const gf::RuleFiring&)`
  or built-ins: `invoke(command)`, `set(value, x)`, `toggle(flag)`,
  `focus(c)`, `begin_keyboard_drag(c)`. No lambdas.
- **Order.** Rules on one trigger are evaluated in the order they were
  added; every matching rule fires. `consume` stops later rules and the
  default route. Key rules run before the focused control.
- **Lifetime.** A rule naming a component is disarmed when it is disposed;
  its token then reads `stale`. `token.revoke()` ends a rule early.
- **One trigger, no conditions, one action** in one call:
  `gf::on(window.rules(), gf::trigger::click(*ok), page, &search_now);`.
  For events outside the vocabulary, `gf::on(event, owner, &Owner::member)`.

Form documents hold no rules in 0.5. The C ABI has `GF_TABLE_RULES`.

Full page: [Rules](https://gui-forms.readthedocs.io/user/rules.html).
