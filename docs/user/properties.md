# Properties

A control's property is a member you assign to and read from. The value is
stored in exactly one place, the property's **cell**. Assigning to the cell is
the only way to write the value, and every write behaves the same way: it
compares the new value with the old one, stores it if it differs, marks the
control dirty and raises the property's change event.

Controls are held by `std::shared_ptr`, so a property is reached through the
pointer: `(*ok).text = "OK";`.

```cpp
#include <gui_forms/basic_controls.hpp>
#include <gui_forms/input_controls.hpp>

#include <memory>

namespace gf = gui_forms;

void hide_when_empty(const std::shared_ptr<gf::ListBox>& list, const std::shared_ptr<gf::Button>& clear) {
    const bool has_items = !(*list).items.read().empty();
    (*clear).visible = has_items;                 // the write
    if (!(*clear).visible.read()) {               // the read
        (*clear).tab_stop = false;
    }
    (*clear).text = "Clear";
    (*clear).dock = gf::DockStyle::bottom;        // an enum value, written the same way
}
```

## Writing and reading

- **A write is an assignment.** `(*button).enabled = false;` stores the value
  and touches the control: the layout, paint or hit-test work the property
  needs is marked, and nothing is drawn until the host paints its next frame
  ([Update scopes and dirtiness](update-scopes-and-dirtiness)). Writing the
  value the property already holds does nothing at all. It raises no event
  and marks nothing.
- **A read is `.read()`.** It is the only read. A cell has no conversion
  operator and no `==`, so a forgotten `.read()` is a compile error rather
  than a silent copy. A cell's `.read()` returns a reference to the stored
  value.
- **No implicit conversion.** The cell takes its own type only. Write
  `(*label).padding = gf::Insets{4.0, 2.0, 4.0, 2.0};`, not a bare number.
  Typed code converts with ordinary C++ after `.read()`.
- **A cell cannot be copied or moved.** It is part of its control. Copy the
  value with `.read()`.
- **Validation comes first.** A control can refuse a value before storing it.
  The refusal throws, and the property keeps its old value.
- **The UI thread only.** A write to an attached control from another thread
  throws, as every control mutation does ([Threading](threading)).

Each property raises a typed change event after the value is stored, such as
`visible_changed()` or `text_changed()`. Subscribe with an owning component
as usual. Writes made between `begin_init()` and `end_init()` keep only each
property's latest value, and their events are raised once, when the
outermost `end_init()` runs.

## Values owned elsewhere

Some values belong to another owner. For example, a text box's text belongs
to its editing engine. Such a property is a **view**. It has the same
spelling as a cell (`=` to write, `.read()` to read), but the read asks the
owner and returns the value by copy:

```cpp
(*name_box).text = "Ada";                          // a TextBox: the editor stores it
const std::string typed = (*name_box).text.read(); // a copy of the editor's text
```

## Collections

A list's items are a **collection cell**. Assign the whole sequence, or edit
it in place; each edit is checked, compared and marked like any other write:

```cpp
(*list).items = {"Red", "Green", "Blue"};   // the whole sequence
(*list).items.append("Violet");             // Windows Forms' Items.Add
(*list).items.replace(0U, "Crimson");
const std::size_t count = (*list).items.read().size();
```

`insert`, `erase` and `clear` complete the set. `items.read()` returns a
`std::span` of the elements, valid until the next edit.

## Properties a container gives its children

A container can give each direct child a property of its own, such as a grid
area or a flow break. Index the container's property with the child:

```cpp
(*grid).area[*navigation] = "navigation";       // the write
const std::string where = (*grid).area[*navigation].read();
(*flow).flow_break[*button] = true;
```

The container stores one value for each child. Writing or reading for a
control that is not a live direct child throws. A child that was never
written reads the property's default. A child property can be written only in
the expression that indexes it: you cannot keep the indexed result. A grid's
`area` and `cell` are two views of one placement
([Layout](layout.md#grid-spans-and-placement)).

## Types and conversions

`.read()` gives the property's declared type exactly. Two conversions sit
beside it, for code that needs another type:

```cpp
const std::string shown = (*clear).visible.as_type(gf::types::string);    // the value as text
const std::uint64_t order = (*clear).tab_index.as_type(gf::types::uint64); // a widening, never a narrowing
const gf::PropertyType described = (*clear).visible.type();              // the runtime description
const std::optional<gf::BindingValue> converted = (*clear).visible.as_type(described);
```

- `as_type(gf::types::<tag>)` converts at compile time, through a tag from
  `gui_forms::types` (`boolean`, `int32`, `int64`, `uint32`, `uint64`,
  `float32`, `float64`, `string`, `point`, `size`, `rect`, `insets`,
  `color`, `font_spec`, `image_id`, `milliseconds`, `binding_value`, and
  enumeration tags beside their enumerations). Only total conversions are
  offered: the identity, widening integers, integers to `float64`, anything
  to `string` and to `binding_value`, an enumeration to `int64` (its value)
  and to `string` (its name). A pair where the conversion could fail does not
  compile.
- `type()` returns the property's runtime description, a `PropertyType`
  (its kind, whether it is nullable, and its enumeration). It describes the
  property; it is not a read.
- `as_type(property_type)` converts to a type known only at run time. It
  returns `std::optional<BindingValue>`, empty when the value has no form of
  that type. Both forms use one conversion table, so they agree.

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

## What stays a function

What is a property is a cell, a view or a collection cell. The members that
remain functions are not properties: operations (`perform_click()`), the
tree's structure (`add_child`, `remove_child`), records with identities of
their own (`set_items` of the menu, strip and ribbon families), geometry with
flags (`set_bounds(rect, specified)`), a write that returns a result
(`MaskedTextBox::set_text` returns a `MaskEditResult`) and settings of
objects that are not controls (`Command::set_enabled`,
`Application::set_theme`). Each control's [API page](../api/index) lists its
members.
