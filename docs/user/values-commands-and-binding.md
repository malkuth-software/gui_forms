# Values, commands and binding

The state your application cares about belongs to your objects, not to the
controls. Three tools connect the two: owned handlers that carry a little
context, [`Value<T>`](../api/generated/gui_forms.Value.rst) for shared scalar
state and [`Command`](../api/generated/gui_forms.Command.rst) for actions.
Controls bind to them by reference, and propagation is synchronous on the UI
thread. Record-and-field binding is described in [Data binding](data-binding).

A form document binds the same sources in a property's own slot when it
creates the control (`value: bind(volume);`); see [Forms](forms.md#bindings).
The `bind` calls below are the C++ spelling, for controls a program makes
itself.

```cpp
#include <gui_forms/collection_controls.hpp>
#include <gui_forms/range_controls.hpp>

#include <memory>

namespace gf = gui_forms;

// The model owns the state and its handler subscriptions.
class Mixer final : public gf::Component {
public:
    Mixer() { gf::on(mute.invoked(), *this, &Mixer::on_mute); }

    gf::Value<double> volume{25.0};
    gf::Command mute{"mixer.mute", "Mute"};

private:
    void on_mute() { volume.set(0.0); }
};

int main() {
    Mixer mixer{};
    std::shared_ptr<gf::TrackBar> left =
        gf::make_control<gf::TrackBar>(gf::StableId("volume.left"));
    std::shared_ptr<gf::TrackBar> right =
        gf::make_control<gf::TrackBar>(gf::StableId("volume.right"));
    std::shared_ptr<gf::CommandBar> bar =
        gf::make_control<gf::CommandBar>(gf::StableId("commands"));
    (*left).bind(mixer.volume);  // the model's 25 wins at bind time
    (*right).bind(mixer.volume);
    (*bar).set_items({{.id = 1, .label = "Mute", .command = &mixer.mute}});

    (*left).set_value(60.0);        // right and mixer.volume follow
    (*bar).item(0).perform_click(); // runs mixer.mute, then bar.invoked()
    return (*right).value() == 0.0 ? 0 : 1;
}
```

## Handlers that carry values

`on(event, owner, &Owner::method, values...)` registers an owned handler; see
[Retained tree](retained-tree) for how the owner revokes it. Up to two values
bound at registration are passed before the event's arguments, so
`gf::on((*tile).clicked(), *this, &Grid::choose, row, column)` calls
`choose(row, column)`, or `choose(row, column, sender)` if it takes the
sender. Bound values must be trivially copyable and total at most 16 bytes:
pass an index into your own data rather than a string, and treat a bound
pointer as a borrow. Violations are compile errors with an explanatory
message; an overloaded method name needs an explicit member-pointer cast.
Registering on a disposed owner does nothing, a null member throws
`std::invalid_argument`, and emitting to member handlers does not allocate.

## Shared scalar state: `Value<T>`

`Value<T>` holds one arithmetic or enum value and is itself a `Component`.
Its contract is `get()`, `set()`, `changed()` and `validating()`:

- `set` with an equal value does nothing.
- `validating` receives the candidate first; a handler that throws rejects
  the write. Otherwise the value is stored and `changed` is raised. An
  exception from a `changed` handler propagates after the commit.
- A floating-point `Value` accepts only finite numbers and throws
  `std::invalid_argument` otherwise, including at construction.
- Writing a different value from inside one of its own notifications throws
  `std::logic_error`, so mutually correcting observers cannot loop. Make the
  follow-up change after the notification returns. Writing to a disposed
  `Value` also throws.

## Binding controls to a value

| Control | Connects with | Model |
|---|---|---|
| [`RangeControl`](../api/generated/gui_forms.RangeControl.rst), [`TrackBar`](../api/generated/gui_forms.TrackBar.rst) | `bind(model)` | `Value<double>` |
| [`CheckBox`](../api/generated/gui_forms.CheckBox.rst) | `bind(model)` | `Value<bool>` |
| [`ChoiceGroup`](../api/generated/gui_forms.ChoiceGroup.rst) | `bind(model)`; `-1` selects nothing | `Value<int>` |
| [`ExpandableSections`](../api/generated/gui_forms.ExpandableSections.rst) | `SectionItem::expanded` | `Value<bool>*` per section |

A binding is two-way, and the model's value wins when you bind. A model value
the control cannot represent (outside its range, past its last choice) throws
at that point, so configure ranges and items first. Any number of controls can
share one model: a change in one updates the model, the model updates the
others, and the originating control's setter is not called again. Every bound
control's constraints are checked before anything changes, so a value one of
them rejects leaves all of them, and the model, as they were.

Controls borrow the model. Disposing or destroying it ends its bindings, and
each control keeps its last state; destroying a control ends its own. Binding
again replaces the model, `unbind()` ends it, and on a `CheckBox` a `Value`
binding and a `Command` binding replace each other. Steady-state propagation
does not allocate. Programmatic changes raise state events such as
`value_changed` but never input events such as `scroll`; see
[Retained tree](retained-tree) for that split.

## Commands

A default-constructed `Command` is anonymous and suits a model member;
`Command(stable_id, text)` names one for registries. `state()` returns a
`CommandState` with text, description, `enabled`, `visible`, `checked` and
further metadata, and each setter raises `state_changed()` only on a real
change. `set_checked` makes the command checkable and `clear_checked` removes
that again, as `CommandState::checkable` reports. `execute(source_id)` raises
`invoked()` with a `CommandInvocation`; it returns false and invokes nothing
when the command is disabled or disposed.

`button.bind(command)` connects any `ButtonBase`, and the command's state wins
at bind time. The button's enabled state follows the command; a checkable
command shows as a `CheckBox` check or a `Button` selected state; toggling a
bound check box updates the command; one click executes it once, with the
button's stable ID as the source. `unbind_command()` ends only that
connection, and a disposed or destroyed command is never invoked through a
surviving button. `command.bind_shortcut(window, gesture)` adds a keyboard
accelerator with the same enabled check. Keep the returned `AcceleratorToken`:
destroying it, or the command, revokes the shortcut.
[Input and focus](input-and-focus) shows where accelerators sit in key routing.

## Item-reporting collection controls

[`CommandBar`](../api/generated/gui_forms.CommandBar.rst), `ChoiceGroup` and
`ExpandableSections` build retained item controls from plain records passed
to `set_items` and report through group events:

| Control | Item record | Group events |
|---|---|---|
| `CommandBar` | `CommandItem`: `id`, `label`, `glyph`, `accessible_name`, `shortcut`, borrowed `Command*` | `invoked(const CommandItem&)` |
| `ChoiceGroup` | `ChoiceItem`: `id`, `label`, `glyph`, `image`, `accessible_name` | `changed(int)` on every selection change, `activated(int)` on input |
| `ExpandableSections` | `SectionItem`: `id`, `label`, `content`, borrowed `Value<bool>*` | `toggled(int index, bool expanded)` |

Duplicate IDs or invalid UTF-8 make `set_items` throw before anything is
published. A new list reuses the control of every item whose ID survives, and
handlers on the group events keep working across replacements. Replacing the
items from a group handler is supported; recursive replacement throws.

- `CommandBar` runs an item's command before raising `invoked`. The
  `CommandItem&` stays valid for the whole dispatch even if a handler replaces
  the items; do not keep it afterwards. A `shortcut` requires a `command` and
  is active while the bar is attached to a window. The bar has toolbar
  semantics and one tab stop; arrow keys move among enabled, visible items.
- `ChoiceGroup` keeps its selection in a local `Value<int>` (`selection()`)
  that `bind` connects to yours; `set_selected_index` changes it. Arrow keys
  select and focus the next available choice, shrinking the list clears a
  selection past its end to `-1`, and the group has radio-group semantics.
- `ExpandableSections` stacks a 28-unit disclosure header over each content
  control, which keeps its requested height. Contents must be distinct and
  have no other parent. `single_open = true` closes the others when one
  opens.

Item controls are named parts for styling: `part("item", index)` on
`CommandBar` and `ChoiceGroup` returns the retained `Button` (as does
`item(index)`); `ExpandableSections` has `"header"` and `"body"`. An unknown
name returns `nullptr`, and item controls get stable IDs such as
`commands.item.1`. Change a choice through the group's selection, not through
its button's selected flag.
