The shared base of controls that show a number within a bounded range.

`RangeControl` holds a `value` between `minimum` and `maximum`, with the step
sizes `small_change` and `large_change` and an `orientation`. It is the base
of `TrackBar`, `ProgressBar` and `ScrollBar` (and so of `HScrollBar` and
`VScrollBar`), which add the drawing and the input. A `RangeControl` on its
own draws nothing and handles no input, so create one of those controls
instead; use this type when code should work with any of them, for example
to bind several of them to one application value. For a number the user
types, use `NumericUpDown`.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
minimum : default ``0``
    The lowest value. Setting it is the same as calling `set_range` with the
    current `maximum`.
maximum : default ``100``
    The highest value. Setting it is the same as calling `set_range` with the
    current `minimum`.
value : default ``0``
    The current value, always within `minimum` and `maximum`. `set_value`
    does not clamp: a value outside the range throws `std::out_of_range`, and
    a value that is not finite throws `std::invalid_argument`. Setting the
    current value again does nothing.
small_change : default ``1``
    The step used by the arrow keys, the mouse wheel and the scroll bar
    arrows. It must be finite and greater than zero, or the setter throws
    `std::invalid_argument`.
large_change : default ``10``
    The step used by Page Up and Page Down and by clicks on a scroll bar
    track; a `ScrollBar` also sizes its thumb from it. It must be finite and
    greater than zero, or the setter throws `std::invalid_argument`.
orientation : default ``Orientation::horizontal``
    Whether the control runs left to right or bottom to top. Changing it
    remeasures the control. `ScrollBar` sets it from its constructor
    argument, which defaults to `Orientation::vertical`.
normalized_value
    The position of `value` within the range, from 0 at `minimum` to 1 at
    `maximum`.
style
    The colours the derived control draws with.

Events
------
range_changed
    Raised after `set_range`, `set_minimum` or `set_maximum` changes the
    range. It carries the new minimum and maximum. When the new range
    excludes the current value, the value is clamped into it and
    `value_changed` is raised immediately afterwards.
value_changed
    Raised after `value` changes, whatever the cause: a setter, `increment`,
    a range change, a bound `Value`, or user input. It carries the new value.
scroll
    Raised only when the user changes the value through a derived control:
    by dragging, clicking, the keyboard, the mouse wheel or an accessibility
    action. It carries a `RangeScrollEvent` with the old value, the new value
    and the `RangeAction` that caused it, and is raised before
    `value_changed`. Setters never raise it.

Methods
-------
set_range
    Sets `minimum` and `maximum` together. Both must be finite and `minimum`
    must be less than `maximum`, or it throws `std::invalid_argument`; an
    empty range is not allowed. The current value is clamped into the new
    range.
increment
    Adds a positive or negative amount to `value`, clamped to the range. It
    behaves like `set_value`, so it raises `value_changed` but not `scroll`.
    A delta that is not finite throws `std::invalid_argument`.
bind
    `bind(Value<double>&)` keeps `value` and an application value in step.
    The value wins when the binding starts, so the control first takes the
    value's number; after that a change on either side updates the other.
    The value must already lie within the control's range, or `bind` throws
    `std::out_of_range`, and a disposed `Value` throws
    `std::invalid_argument`. Afterwards, setting the `Value` to a number
    outside the range is rejected with `std::out_of_range`. Binding again
    replaces the previous binding.
unbind
    Ends the value binding. The control keeps its current value.

See Also
--------
TrackBar : A slider the user drags or steps with the keyboard.
ProgressBar : A bar that shows how far an operation has progressed.
ScrollBar : A bar that scrolls a view, with arrows, track and thumb.
NumericUpDown : A text field for a number, with up and down buttons.

Notes
-----
State events describe every change and the input event describes only user
input: observe `scroll` to react to the user, and `value_changed` to follow
the value whatever changed it. When the user changes the value, `scroll`
comes first and `value_changed` second. `ProgressBar` takes no input, so it
never raises `scroll`.

Several controls can bind the same `Value<double>`. A change made through
one of them is checked against every bound control's range before anything
changes, so a value that one of them cannot show is rejected everywhere.
Give the controls compatible ranges before binding them, and unbind before
moving a control to a range that excludes the current value.

Between `begin_init` and `end_init`, `range_changed` and `value_changed` are
held back and raised once each, with the latest values, after `end_init`.
The value is also registered as the bindable property ``Value``, so data
bindings and `PropertyGrid` can read and set it.

The derived controls publish their own accessibility roles: `slider` for a
`TrackBar`, `progress_bar` for a `ProgressBar` and `scroll_bar` for a
`ScrollBar`, each with the value and the range.

Examples
--------
Bind a slider and a progress bar to one application value. The value wins
when each binding starts, and afterwards all three stay in step:

.. code-block:: cpp

   gf::Value<double> volume(40.0);
   std::shared_ptr<gf::TrackBar> slider =
       gf::make_control<gf::TrackBar>(gf::StableId("mixer.volume"));
   std::shared_ptr<gf::ProgressBar> meter =
       gf::make_control<gf::ProgressBar>(gf::StableId("mixer.meter"));
   (*slider).bind(volume);      // the slider shows 40
   (*meter).bind(volume);       // and so does the meter
   (*slider).value = 65.0;   // volume and the meter are now 65

Changing the range clamps the value into it, raising `range_changed` and then
`value_changed`:

.. code-block:: cpp

   std::shared_ptr<gf::ProgressBar> copied =
       gf::make_control<gf::ProgressBar>(gf::StableId("copy.progress"));
   (*copied).set_range(0.0, 250.0);
   (*copied).value = 100.0;   // normalized_value() is 0.4
   (*copied).maximum = 50.0;  // the value is clamped to 50
