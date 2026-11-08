# Properties

A control's property is a member you assign to and read from. The value is
stored in exactly one place, the property's **cell**. Assigning to the cell is
the only way to write the value, and every write behaves the same way: it
compares the new value with the old one, stores it if it differs, marks the
control dirty and raises the property's change event.

```cpp
#include <gui_forms/basic_controls.hpp>

namespace gf = gui_forms;

void hide_when_empty(gf::ListBox& list, gf::Button& clear) {
    clear.visible = !list.items().empty();    // the write
    if (!clear.visible.read()) {              // the read
        clear.tab_stop = false;
    }
    clear.dock = gf::DockStyle::bottom;       // an enum value, written the same way
}
```

## Writing and reading

- **A write is an assignment.** `button.enabled = false` stores the value and
  touches the control: the layout, paint or hit-test work the property needs
  is marked, and nothing is drawn until the host paints its next frame
  ([Update scopes and dirtiness](update-scopes-and-dirtiness)). Writing the
  value the property already holds does nothing at all. It raises no event
  and marks nothing.
- **A read is `.read()`.** A cell has no conversion operator and no `==`, so
  a forgotten `.read()` is a compile error rather than a silent copy.
  `.read()` returns a reference to the stored value.
- **No implicit conversion.** The cell takes its own type only. Write
  `label.padding = gf::Insets{4.0, 2.0, 4.0, 2.0}`, not a bare number.
- **A cell cannot be copied or moved.** It is part of its control. Copy the
  value with `.read()`.
- **Validation comes first.** A control can refuse a value before storing it.
  The refusal throws, and the property keeps its old value.
- **The UI thread only.** A write to an attached control from another thread
  throws, as every control mutation does ([Threading](threading)).

Each property raises a typed change event after the value is stored, such as
`visible_changed()` or `enabled_changed()`. Subscribe with an owning
component as usual. Writes made between `begin_init()` and `end_init()` keep
only each property's latest value, and their events are raised once, when
the outermost `end_init()` runs.

## Values owned elsewhere

Some values belong to another owner. For example, a text editor's text
belongs to its editing engine. Such a property is a **view**. It has the same
spelling as a cell (`=` to write, `.read()` to read), but the read asks the
owner and returns the value by copy.

## Properties a container gives its children

A container can give each direct child a property of its own, such as a grid
area or a flow break. Index the container's property with the child:

```cpp
grid.area[*navigation] = "navigation";          // the write
const std::string area = grid.area[*navigation].read();
flow.flow_break[*button] = true;
```

The container stores one value for each child. Writing or reading for a
control that is not a live direct child throws. A child that was never
written reads the property's default. A child property can be written only in
the expression that indexes it. You cannot keep the indexed result.

## Types and conversions

`cell.type()` returns the property's runtime type, a `PropertyType`. Two
conversions are available beside the read:

- `cell.as_type(gf::types::string)` is a total conversion named by a tag. A
  pair of types where the conversion could fail does not compile.
- `cell.as_type(property_type)` converts to a type known only at run time. It
  returns `std::optional<BindingValue>`, empty when the value has no form of
  that type.

## Properties by name

Tools, form documents, the C ABI and the property grid reach properties by
their schema name:

- `control.property_value("padding")` reads a property and returns
  `std::optional<BindingValue>`.
- `control.set_property_value("padding", value)` writes one through the same
  cell, with the same comparison, dirtiness and event.
- `control.property_descriptor(name)` describes a property. It says whether
  the property is bindable, and in which direction a binding defaults.
- `child_property_value` and `set_child_property_value` do the same for a
  container's child properties.

In program code, write the member. The name-based calls are for code that
learns the property's name at run time.

## Bindings

A bindable property can follow a source:

- a `Value<T>` the program owns;
- a `Command`, for a button's `command` property;
- a field of a `BindingSource` record.

The property's table entry gives its default direction. A property the user
edits, such as a check box's `checked`, a range's `value` or a text box's
`text`, binds **two way**. A property only code changes, such as a label's
`text`, `enabled` or `visible`, binds **one way**, from the source to the
control.

[Forms](forms.md#bindings) shows how a form document binds a property when it
creates the control. [Values, commands and binding](values-commands-and-binding)
and [Data binding](data-binding) describe the sources.

## Properties not yet cells

Most properties are cells. A few are still accessor pairs, such as a label's
`text()` and `set_text()`, while their controls are converted. Each control's
[API page](../api/index) lists its members, and the cell members are the ones
you assign to. Accessor pairs keep the same rules: an equal value does
nothing, and a real change marks the control dirty and raises the change
event.
