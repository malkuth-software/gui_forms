A two-way connection between one property of a control and one field of a `BindingSource`.

A binding keeps a control property, such as a text box's `Text` or a check
box's `Checked`, in step with a field of the source's current record. It
shows the field in the control when the binding starts, whenever the source
changes and when the current record moves, and writes the control's value back
to the field when the control changes or is validated, according to its
`BindingOptions`. Values are converted between the field's kind and the
property's kind, and the `format` and `parse` events let the application
convert them itself. Create bindings with `data_bindings().add` on the
control; to keep a single scalar in step with a control, bind the control to
a `Value` instead.

Parameters
----------
target
    The control whose property is bound. It must be alive.
property_name
    The bindable property of the control, such as "text" or "checked";
    `bindable_property_names` on the control lists them. The name is trimmed
    and compared ignoring ASCII case. A name the control does not have throws
    `std::invalid_argument`.
source
    The source of the rows. It must be alive, or `std::invalid_argument`
    is thrown. The binding holds it weakly.
data_member
    The field (column) of each row, trimmed and compared ignoring ASCII case. It
    must not be empty.
options
    Update modes, formatting and null handling; see `options`.

Properties
----------
target
    The bound control, or null once the binding is disposed.
source
    The source, or null once the source or the binding has been disposed.
property_name
    The control property, in canonical form: trimmed and lower-case.
data_member
    The record field, in canonical form: trimmed and lower-case.
options
    The `BindingOptions`. `data_source_update_mode` chooses when the control
    writes to the field: `DataSourceUpdateMode::on_validation` (the default)
    when the control is validated, `on_property_changed` on every change, or
    `never`. `control_update_mode` chooses whether source changes update the
    control: `ControlUpdateMode::on_property_changed` (the default) or
    `never`. With `formatting_enabled`, the `format`, `parse` and
    `binding_complete` events are raised and `format_string` is applied; the
    only format strings accepted are the invariant fixed-point formats `F0`
    to `F12`. A `null_value` replaces an empty field in the control, and a
    control value equal to `null_value` is written back as
    `data_source_null_value`. Invalid options throw `std::invalid_argument`.
    Setting options on an active binding with `on_property_changed` control
    updates reads the field into the control again.
active
    Whether the binding is connected. It becomes true when the binding is
    added to the control's `data_bindings()`, and false when it is removed or
    disposed, or its source is disposed.
snapshot
    A `BindingSnapshot` counting reads into the control, writes to the
    source, successful and failed updates, and updates suppressed because
    another update of this binding was already running.

Events
------
format
    Raised, when `formatting_enabled` is set, before a field value is shown in
    the control. The `BindingConvertEvent` carries the value and the kind of
    the control property. A handler may replace `value`; setting `handled`
    skips `format_string`. The value is then converted to the property's
    kind.
parse
    Raised, when `formatting_enabled` is set, before the control's value is
    written to the field. The event carries the value and the kind of the
    current field, or of the value itself when the record has no such field.
    A handler may replace `value`, which is then converted to that kind.
binding_complete
    Raised, when `formatting_enabled` is set, after a transfer in either
    direction; after a successful one, the destination has already been
    updated. The `BindingCompleteEvent` gives the direction, whether the
    transfer succeeded or failed with a data error or an exception, and the
    error text. Setting `cancel` makes the transfer report failure. The
    source's `binding_complete` is raised just after this one.

Methods
-------
read_value
    Shows the current field in the control now, even when
    `control_update_mode` is `never`. Returns whether the transfer
    succeeded, and false on a binding that is not `active`.
write_value
    Writes the control's value to the current field now, even when
    `data_source_update_mode` is `never`. Returns whether the transfer
    succeeded, and false on a binding that is not `active`.
validate
    Writes the control's value to the field, as validation does, and returns
    whether it succeeded. With `DataSourceUpdateMode::never` it writes
    nothing and returns true.

See Also
--------
BindingSource : The rows and the current position that bindings follow.
BindingContext : The currency managers of a window's binding sources.
ErrorProvider : Shows the errors of a source's bound fields.

Notes
-----
A binding constructed directly does nothing until it is added with
`data_bindings().add(binding)`; the `add` overloads that take a property name
construct it and add it at once. Adding it shows the current field in the
control, unless `control_update_mode` is `never`. A control has at most one
binding per property; a second one throws `std::invalid_argument`. Removing
the binding from `data_bindings()`, or disposing the control, disposes it.

The control is updated whenever the source raises any change: a reset, a new
current record, or a change to any field of a record. While the source's
binding is suspended, nothing reaches the control; resuming updates it once.

A transfer that fails, because the record has no such field, a value cannot
be converted or a `format` or `parse` handler throws, leaves the destination
unchanged. Its error text is raised as the source's `data_error`, and the
transfer returns false. With `on_validation`, a failed write cancels the
validation, so focus may stay on the control.

Updates do not recurse: a change that arrives while the binding is already
transferring a value is ignored and counted in `snapshot`.

Use bindings on the UI thread of the control's window. The binding's methods
call into the control and the source, which reject other threads with
`std::logic_error`.

Examples
--------
Bind a text box to the name of the current person, writing back on every
change:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("form"));
   std::shared_ptr<gf::TextBox> name = gf::make_control<gf::TextBox>(gf::StableId("form.name"));
   (*root).add_child(name);
   gf::Window window(root, gf::Size{320.0, 120.0});

   std::shared_ptr<gf::BindingSource> people = std::make_shared<gf::BindingSource>(window);
   gf::BindingTable& table = *(*people).table();
   static_cast<void>(table.add_column(gf::BindingColumn{"name", gf::BindingValueKind::text}));
   static_cast<void>(table.append(gf::BindingRow{"person.1", {std::string("Ada")}}));

   gf::BindingOptions options;
   options.data_source_update_mode = gf::DataSourceUpdateMode::on_property_changed;
   std::shared_ptr<gf::Binding> binding =
       (*name).data_bindings().add("text", people, "name", options);
   // the text box shows "Ada"
   (*name).text = "Ada Lovelace";
   // the current record's "name" field is now "Ada Lovelace"

Show a price with two decimals and accept a leading dollar sign when the user
types one:

.. code-block:: cpp

   struct PriceField final : gf::Component {
       void on_parse(gf::BindingConvertEvent& event) {
           const std::string* text = std::get_if<std::string>(&event.value);
           if (text != nullptr && !(*text).empty() && (*text)[0] == '$') {
               event.value = std::string((*text).substr(1));
           }
       }
   };

   void example() {
       std::shared_ptr<gf::TextBox> price = gf::make_control<gf::TextBox>(gf::StableId("item.price"));
       gf::Window window(price, gf::Size{200.0, 32.0});
       std::shared_ptr<gf::BindingSource> items = std::make_shared<gf::BindingSource>(window);
       gf::BindingTable& table = *(*items).table();
       static_cast<void>(table.add_column(gf::BindingColumn{"price", gf::BindingValueKind::number}));
       static_cast<void>(table.append(gf::BindingRow{"item.1", {12.5}}));

       gf::BindingOptions options;
       options.formatting_enabled = true;
       options.format_string = "F2";
       options.data_source_update_mode = gf::DataSourceUpdateMode::on_property_changed;
       PriceField field;
       std::shared_ptr<gf::Binding> binding =
           (*price).data_bindings().add("text", items, "price", options);
       gf::on((*binding).parse(), field, &PriceField::on_parse);
       // the text box shows "12.50"
       (*price).text = "$13.25";
       // the current record's "price" field is now 13.25
   }
