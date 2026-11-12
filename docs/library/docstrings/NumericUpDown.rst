A text field for a number, with up and down buttons that step it.

A numeric up-down shows its `value` in an `editor` text box beside a pair of
spin buttons. The user types a number, clicks the upper or lower button,
presses Up or Down, or turns the mouse wheel; every step adds or subtracts
`increment` and stays within `minimum` and `maximum`. The value can be shown
with a fixed number of `decimal_places` or in `hexadecimal`. Use a
`TrackBar` instead when the position within the range matters more than the
exact number.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID. The editor is created
    as ``<stable_id>.editor`` and the spin buttons as ``<stable_id>.spinner``.

Properties
----------
minimum : default ``0``
    The lowest value. A minimum above `maximum` raises `maximum` to it, and
    a value below the new minimum is clamped into the range, raising
    `value_changed`. A number that is not finite throws
    `std::invalid_argument`.
maximum : default ``100``
    The highest value. A maximum below `minimum` lowers `minimum` to it, and
    a value above the new maximum is clamped into the range, raising
    `value_changed`. A number that is not finite throws
    `std::invalid_argument`.
value : default ``0``
    The current number. A write does not clamp: a value outside the
    range, or one that is not finite, throws `std::out_of_range`. The value
    keeps its full precision; `decimal_places` and `hexadecimal` only change
    how it is shown.
increment : default ``1``
    The amount one step adds or subtracts. It must be finite and greater than
    zero, or the setter throws `std::invalid_argument`.
decimal_places : default ``0``
    How many digits are shown after the decimal point, from 0 to 12; a larger
    number throws `std::invalid_argument`. Setting a nonzero number turns
    `hexadecimal` off.
hexadecimal : default ``false``
    Whether the value is shown as an upper-case hexadecimal whole number,
    and typed text is read as hexadecimal. The shown number is the value
    rounded to the nearest whole number. Turning it on sets
    `decimal_places` to 0.
button_width : default ``18``
    The width of the spin buttons, from 12 to 64; a width outside that range,
    or one that is not finite, throws `std::out_of_range`.

Events
------
value_changed
    Raised after `value` changes, whatever the cause: a setter, a range
    change that clamps the value, a step, or typing. It carries the new
    value.

Methods
-------
set_range
    Sets `minimum` and `maximum` together. Both must be finite and `minimum`
    may not exceed `maximum`, or it throws `std::invalid_argument`; a range
    of a single number is allowed. A value outside the new range is clamped
    into it and `value_changed` is raised.
initialize_control_tree
    Creates the editor and the spin buttons. `make_control` calls it; do not
    call it yourself.

See Also
--------
TrackBar : A slider for picking a value within a range.
TextBox : A field for free text.

Notes
-----
Its `tool_tip_text` shows as grey hint text in the edit field when the
field is empty and has focus, in place of a tooltip bubble.

Typing commits as the user goes: each edit that leaves the editor holding a
complete number within the range becomes the new value at once and raises
`value_changed`. Text that is incomplete, unparseable or out of range leaves
the value unchanged and stays in the editor. Pressing Enter commits the text
in the same way and then rewrites the editor with the formatted value,
discarding text that was not accepted. Leaving the field does not rewrite it.

Because typing commits as it goes, intermediate numbers can become the
value: typing 150 into an empty field whose range ends at 100 commits 1 and
then 15, and refuses 150. Also, because Enter commits the displayed text, a
value shown rounded, such as 1.2345 with two decimal places, becomes the
rounded number 1.23 when the user presses Enter.

Up and Down step by `increment` even while the editor has focus. A click on
the upper half of the spin buttons steps up and a click on the lower half
steps down; the buttons do not repeat while held. Turning the mouse wheel
up anywhere over the control steps up, and turning it down steps down. A
step that reaches the range edge stops there. After any step in a window,
the editor takes focus and selects its text. The spin buttons never take
focus themselves, and a disabled control ignores all of this input.

Hexadecimal display is meant for non-negative whole numbers: a negative
value is shown as a 64-bit two's complement number, which cannot be typed
back.

The control publishes the accessibility role `numeric_field`, with the
formatted value as text, the value as a number, the minimum and maximum, and
the increment as its small step (there is no large step); the editor and
the buttons are not published separately. The
`SemanticAction::increment` and `SemanticAction::decrement` actions step,
`SemanticAction::focus` moves focus to the editor, and
`SemanticAction::set_value` takes a decimal number as text and refuses text
that is incomplete or out of range.

Examples
--------
A zoom field that steps in quarters from 25 to 400 percent:

.. code-block:: cpp

   std::shared_ptr<gf::NumericUpDown> zoom =
       gf::make_control<gf::NumericUpDown>(gf::StableId("view.zoom"));
   (*zoom).set_range(25.0, 400.0);
   (*zoom).increment = 25.0;
   (*zoom).value = 100.0;   // the editor shows "100"

Show two decimal places and follow the value from an owner. The Up key steps
the value while the editor has focus:

.. code-block:: cpp

   struct PageSetup final : gf::Component {
       std::shared_ptr<gf::NumericUpDown> margin =
           gf::make_control<gf::NumericUpDown>(gf::StableId("page.margin"));
       double inches = 0.0;

       PageSetup() {
           (*margin).set_range(0.0, 3.0);
           (*margin).increment = 0.25;
           (*margin).decimal_places = 2U;
           gf::on((*margin).value_changed(), *this, &PageSetup::on_margin);
       }
       void on_margin(double value) { inches = value; }
   };

   void example() {
       PageSetup setup;
       (*setup.margin).value = 1.25;   // the editor shows "1.25"
       (*setup.margin).set_requested_bounds({0.0, 0.0, 120.0, 28.0});
       gf::Window window(setup.margin, {120.0, 28.0});
       window.perform_layout();
       window.request_focus((*setup.margin).editor());
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::up});
       // inches is 1.5 and the editor shows "1.50"
   }
