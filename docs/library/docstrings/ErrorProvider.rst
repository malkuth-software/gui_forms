A component that marks controls with validation errors.

An error provider shows an error icon beside each control that has an error,
with the message in a tooltip, and adds the message to the control's
accessible description. Set an error with `set_error` when validation fails
and clear it with an empty message, or let the provider collect the errors of
a `BindingSource` with `bind_to_data_and_errors`. One provider serves any
number of controls in its window, and several providers may mark the same
control. Use a `ToolTip` for plain hints, and a `HelpProvider` for help text.

Parameters
----------
window
    The window whose controls the provider marks. Construct the provider on
    that window's UI thread.

Properties
----------
has_errors
    Whether any live control in the window currently has a nonempty error
    from this provider.
icon_size : default ``16.0``
    Side of the error icon in layout units. It must be finite and from 8 to
    64, or `std::invalid_argument` is thrown.
blink_rate : default ``std::chrono::milliseconds(250)``
    Time between the icon's blink phases. It must be from 50 ms to one
    minute, or `std::invalid_argument` is thrown.
blink_style : default ``ErrorBlinkStyle::blink_if_different_error``
    When the icon blinks. `ErrorBlinkStyle::blink_if_different_error` blinks
    three times whenever a control's message changes, then stays visible;
    `ErrorBlinkStyle::always_blink` blinks while the icon is shown;
    `ErrorBlinkStyle::never_blink` never blinks. Changing it restarts the
    blinking of every icon.
right_to_left : default ``false``
    Whether icon alignments are mirrored: `top_left` is drawn as `top_right`,
    and so on.
icon : default ``std::nullopt``
    An image from the window's registry to draw instead of the built-in red
    exclamation mark. An `ImageId` that is not in the registry throws
    `std::invalid_argument`.
container_control
    The root control of the provider's window, or null once the window has
    shut down.
data_source : default ``nullptr``
    The `BindingSource` whose errors are shown, held weakly. It must be alive
    and belong to the provider's window, or `std::invalid_argument` is
    thrown. Setting the current source again refreshes the errors.
data_member : default ``""``
    A prefix for the record fields whose errors are shown: with
    `data_member` "address", a binding to the field "city" shows the
    record's error for "address.city", or for "city" when that is empty. The
    name is trimmed and compared ignoring ASCII case.
tag
    Application data, kept as given and never interpreted.
snapshot
    An `ErrorProviderSnapshot` listing every marked control with its message,
    icon bounds in window coordinates, and whether the icon is shown and
    blinking. Use it in tests and diagnostics.

Events
------
error_changed
    Raised after a control's message changes, once the control's semantics
    and icon have been updated. It carries an `ErrorProviderChange` with the
    control's stable ID and the new message, which is empty when the error
    was cleared. Setting the same message again raises nothing.
right_to_left_changed
    Raised after `right_to_left` changes, with the new value.
data_source_changed
    Raised after `data_source` changes, before the errors are refreshed, and
    when the source is disposed.
data_member_changed
    Raised after `data_member` changes, with the new member.

Methods
-------
can_extend
    Whether the provider can mark a control: the control must be alive,
    attached to the provider's window and registered there under its stable
    ID.
set_error
    `set_error(control, message)` shows a message for a control; an empty
    message clears it. The message must be valid UTF-8. Throws
    `std::invalid_argument` when `can_extend` is false for the control.
error
    The control's current message from this provider, or an empty string.
clear
    Clears every message and removes every icon, without raising
    `error_changed`.
set_icon_alignment
    `set_icon_alignment(control, alignment)` places the control's icon at one
    of the six `ErrorIconAlignment` positions: left or right of the control,
    aligned with its top, middle or bottom. The default is `middle_right`.
icon_alignment
    The control's icon alignment, `ErrorIconAlignment::middle_right` unless
    set.
set_icon_padding
    `set_icon_padding(control, padding)` sets the gap between the control and
    its icon, in layout units. It must be finite and within plus or minus
    1024. The default is 0.
icon_padding
    The control's icon padding, 0 unless set.
bind_to_data_and_errors
    `bind_to_data_and_errors(source, member = {})` sets `data_member` and
    then `data_source` in one call.
update_binding
    Recomputes the errors shown for `data_source`. The provider calls it
    itself when the source's list or current record changes and when a
    binding completes; call it after changing record errors that the source
    has not announced.

See Also
--------
HelpProvider : Attaches help text to controls.
ToolTip : Shows a hint when the pointer rests on a control.
BindingSource : Records whose field errors the provider can show.

Notes
-----
For a control with a message, the provider adds `Error:` followed by the
message to the control's accessible description and sets the `invalid`
semantic state; a control that was not exposed to assistive technology
becomes exposed, as a `group` when its role was `generic`. With several
providers, their messages appear in the order in which the providers were
created.

The icon is drawn in an overlay layer above the window, sized `icon_size` and
placed by the control's alignment and padding, kept inside the window. It
follows the control when the control moves, is removed while the control or
an ancestor is hidden, and returns when it is shown again. Resting the
pointer on the icon shows the message in a tooltip after 250 ms. Blinking
runs on the window's frame scheduler: it stops while the window is occluded,
and the icon stays visible without blinking while the presentation settings
ask for reduced motion.

With a `data_source`, the provider shows, for each control bound to the
source, the current record's record-wide error, its error for the bound
field and the error of a binding that failed to convert, one per line. A
binding reports conversion failures to the provider only when its options
have `formatting_enabled`, because only then does it raise
`binding_complete`.

Use the provider only on the window's UI thread. Its setters, its queries
that take a control, and `snapshot` throw `std::logic_error` from another
thread.
After disposal, or after the window has shut down, the members that change
the provider throw `std::logic_error`. Disposal, and destruction, clear the provider's messages
from every control and remove its icons.

Examples
--------
Mark a text box whose value failed validation, then clear the mark:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("form"));
   std::shared_ptr<gf::TextBox> port =
       gf::make_control<gf::TextBox>(gf::StableId("form.port"), "99999");
   (*root).set_requested_bounds({0.0, 0.0, 320.0, 120.0});
   (*port).set_requested_bounds({20.0, 20.0, 200.0, 28.0});
   (*root).add_child(port);
   gf::Window window(root, gf::Size{320.0, 120.0});
   window.perform_layout();

   gf::ErrorProvider errors(window);
   errors.set_error(port, "The port must be between 1 and 65535.");
   // errors.has_errors() is true, and the icon sits to the right of the box
   errors.set_error(port, "");

Show the errors that a data source carries for the fields bound to its
controls:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("form"));
   std::shared_ptr<gf::TextBox> email = gf::make_control<gf::TextBox>(gf::StableId("form.email"));
   (*root).add_child(email);
   gf::Window window(root, gf::Size{320.0, 120.0});

   std::shared_ptr<gf::BindingSource> people = std::make_shared<gf::BindingSource>(window);
   gf::BindingTable& table = *(*people).table();
   const std::size_t email_column = table.add_column(gf::BindingColumn{"email"});
   static_cast<void>(table.append(gf::BindingRow{"person.1", {std::string("ada@")}}));
   table.set_error(0U, email_column, "Enter a complete e-mail address.");
   (*email).data_bindings().add("text", people, "email");

   gf::ErrorProvider errors(window);
   errors.bind_to_data_and_errors(people);
   // errors.error(*email) is "Enter a complete e-mail address."
