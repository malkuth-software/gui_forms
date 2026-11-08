# Data binding

[Values, commands and binding](values-commands-and-binding) connects a
control to one scalar. Data binding connects control properties to the
fields of a list of records, with a current position, edit transactions,
formatting and validation, in the manner of Windows Forms. The types are
[`BindingSource`](../api/generated/gui_forms.BindingSource.rst),
[`Binding`](../api/generated/gui_forms.Binding.rst) and
[`BindingContext`](../api/generated/gui_forms.BindingContext.rst); the
[Data binding reference](../api/library.data-binding.rst) lists the rest.

```css
/* forms/person.form */
form person.window {
  text: "Person";
  client-size: 320 120;

  panel person.root {
    text-box person.name { bounds: 12 12 296 28; }
    check-box person.active { text: "Active"; bounds: 12 52 296 28; }
  }
}
```

```cmake
gui_forms_add_form(people forms/person.form CLASS PersonWindow NAMESPACE people DERIVED)
```

```cpp
#include "person_form.hpp"   // generated

#include <gui_forms/binding.hpp>

#include <memory>
#include <string>

namespace gf = gui_forms;

int main() {
    people::PersonWindow window{};   // binds nothing, so it takes no model

    // A record source belongs to one window, so it is made over the window.
    std::shared_ptr<gf::BindingSource> source = std::make_shared<gf::BindingSource>(window);
    (*source).set_records({
        {"person.ada", {{"name", std::string("Ada")}, {"active", true}}},
        {"person.alan", {{"name", std::string("Alan")}, {"active", false}}},
    });

    gf::BindingOptions immediate{};
    immediate.data_source_update_mode = gf::DataSourceUpdateMode::on_property_changed;
    static_cast<void>((*window.person_name).data_bindings().add("text", source, "Name", immediate));
    static_cast<void>((*window.person_active).data_bindings().add("checked", source, "Active"));

    (*window.person_name).text = "Ada L.";   // written to the current record at once
    (*source).move_next();                   // both controls now show Alan's record
    const bool shows_alan = (*window.person_name).text.read() == "Alan" &&
                            !(*window.person_active).checked.read();
    return shows_alan ? 0 : 1;
}
```

A form binds a property to a scalar `Value` or a `Command` of its model in
the property's own slot (`checked: bind(autosave);`), when the control is
created ([Forms](forms#bindings)). The record bindings on this page are the
layer under that: they take a field of a `BindingSource`'s current record.

## Records and currency

A `BindingSource` belongs to one `Window`, and its operations are checked
against that window's UI thread; using it after the window shuts down throws.
`set_records` takes `BindingRecord`s, each with a nonempty stable ID unique in
the list, a map of field names to `BindingValue`s (null, Boolean, integers,
numbers, text, geometry, colour, font, image, enumeration, object or
collection), an `editable` flag and per-field or record-wide error texts.
Field names are case-insensitive. A property has one registered name,
stored in snake_case (`text`, `auto_size_mode`); its CSS spelling
(`auto-size-mode`) and display spelling (`AutoSizeMode`, as PropertyGrid
shows it) also find it. Replacing the records keeps the
current record when its stable ID is still present.

`position()` and `current()` give the current record; `set_position`,
`move_first`, `move_last`, `move_next` and `move_previous` change it, and the
`move_*` calls return false at the ends. A move raises `position_changed`,
then `current_changed`, then `current_item_changed`, after the bound controls
show the new record. `add`, `insert`, `remove_at`, `remove_current`, `clear`
and `find(field, value)` work on the list; with `allow_new` or `allow_remove`
off, adding or removing throws `std::logic_error`.

`set_current_field` writes one field of the current record. It returns false
and changes nothing when there is no current record, the value is equal, or
`allow_edit` or the record's `editable` flag forbids it. The first write
starts an edit: `cancel_edit` restores the record exactly, and `end_edit`, or
moving to another record, commits it. `suspend_binding` keeps changes from
reaching controls until `resume_binding` refreshes them once.
`currency_manager()` exposes the same position, edit and suspension
operations through the `BindingManagerBase` interface.

## Bindings

`control.data_bindings().add(property, source, field, options)` creates a
`Binding`, starts it and returns it; unless `control_update_mode` is `never`,
the control shows the current record at once. Every control can bind `Name`,
`Visible`, `Enabled`, `AutoSize` and `CausesValidation`, and stock controls
add their own, such as `Text` on `TextBox` and `Label`, `Checked` on
`CheckBox` and `Value` on `RangeControl`; `bindable_property_names()` lists
them. An unknown property and a second
binding for the same property throw `std::invalid_argument`.

`BindingOptions` decides when values move:

| Option | Values | Default |
|---|---|---|
| `data_source_update_mode` | `on_validation`, `on_property_changed`, `never` | `on_validation` |
| `control_update_mode` | `on_property_changed`, `never` | `on_property_changed` |
| `formatting_enabled`, `format_string` | a standard numeric format in the invariant culture (`"F2"`, `"N0"`, `"P1"`, `"C"`, `"E3"`, `"G"`; `gui_forms/binding/number_format`); typed text goes back as the number it shows | off |
| `null_value`, `data_source_null_value` | what a null field shows as, and what that shown value is stored back as | null |

With `on_validation` the control's value is written when the control
validates, that is when focus leaves it (see
[Input and focus](input-and-focus)), or when you call `validate()`.
`read_value()` and `write_value()` transfer explicitly in any mode, including
`never`.

Conversion between property and field is strict. A value that does not
convert leaves the record unchanged and raises the source's `data_error` with
the reason; under `on_validation` it also cancels the validation, so focus
stays on the control. With `formatting_enabled`, the binding's `format` and
`parse` events let you supply a conversion and mark it handled, and
`binding_complete`, on the binding and on the source, reports each
transfer's outcome.

## Lifetime, errors and contexts

Disposing a control disposes its bindings; disposing a source deactivates
every binding to it. `Binding::snapshot()` and `BindingSource::snapshot()`
return transfer and mutation counters.

[`ErrorProvider`](../api/generated/gui_forms.ErrorProvider.rst)'s
`bind_to_data_and_errors(source)` shows the current record's error texts next
to the bound controls, together with any conversion failure, and replaces
them when the position moves.

A `BindingContext` is a per-window registry of sources: `manager(source)`
adds a source of the same window and returns its `CurrencyManager`, and
`collection_changed` reports sources entering and leaving.
