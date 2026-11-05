A field that shows a date and time and lets the user pick a date from a drop-down calendar.

A date-time picker shows its `value` as text in one of four formats and
opens a month calendar when the user clicks it or presses F4. The arrow keys
step the date by one day, and an up-down mode replaces the calendar with a
pair of step buttons. With `show_check_box`, a check box lets the user
leave the date unset. The value stays within `minimum` and `maximum`. Month
and day names and the format patterns come from a `DateTimeFormatProvider`,
so the display does not depend on the host's locale.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID. The calendar is
    created as ``<stable_id>.overlay.calendar`` while it is open.

Properties
----------
value : default ``DateTimeValue{2000, 1U, 1U}``
    The date and time, to the millisecond. `set_value` throws
    `std::out_of_range` for a date that does not exist, such as February 30,
    or one outside the range. Setting the current value again does nothing.
minimum : default ``DateTimeValue{1753, 1U, 1U}``
    The earliest value. Set it together with `maximum` through `set_range`.
maximum : default ``DateTimeValue{9998, 12U, 31U, 23U, 59U, 59U, 999U}``
    The latest value. Set it together with `minimum` through `set_range`.
format : default ``DateTimePickerFormat::long_date``
    Which pattern the field shows: the provider's long date, short date or
    time pattern, or `custom_format`.
custom_format : default ``"MMMM d, yyyy"``
    The pattern used when `format` is `DateTimePickerFormat::custom`. It
    must be valid UTF-8, or the setter throws `std::invalid_argument`. See
    the notes for the pattern letters.
format_provider : default ``DateTimeFormatProvider::english_united_states()``
    The month and day names, the three patterns and the AM and PM
    designators. Every name and pattern must be nonempty valid UTF-8, and
    the designators valid UTF-8, or the setter throws `std::invalid_argument`
    and keeps the previous provider. Setting it closes an open calendar.
formatted_value
    The value as the field shows it, in the current format.
show_check_box : default ``false``
    Whether a check box is drawn at the left of the field. While it is
    unchecked, the text is drawn dimmed.
checked : default ``true``
    The state of the check box, which the application reads as "a date is
    set". It does not change `value`, which keeps a date while unchecked.
show_up_down : default ``false``
    Whether a pair of up and down buttons replaces the drop-down arrow.
    There is no calendar in this mode; turning it on closes an open one.
dropped_down : default ``false``
    Whether the calendar is open. Opening it does nothing when the picker is
    not in a window or is in up-down mode; in a window, opening it while the
    picker is hidden or disabled throws `std::logic_error`.
drop_down_alignment : default ``DateTimeDropDownAlignment::left``
    Whether the calendar lines up with the left or the right edge of the
    field. Another value throws `std::invalid_argument`.
font : default ``FontSpec{FontRole::content, 12.0, 400, false}``
    The font of the text. An invalid font specification throws
    `std::invalid_argument`.
style
    The colours of the field and the calendar. The field's background is the
    style's paper colour.

Events
------
value_changed
    Raised after `value` changes, whatever the cause: a setter, a range
    change that clamps the value, a step, the calendar or an accessibility
    action. It carries the new value.
checked_changed
    Raised after `checked` changes, by `set_checked`, by the user, or
    because stepping or setting the date through input checks the box.
drop_down_changed
    Raised after the calendar opens, with `true`, and after it closes, with
    `false`, however that happened.

Methods
-------
set_range
    Sets `minimum` and `maximum` together. Both must be valid dates and
    `minimum` may not be later than `maximum`, or it throws
    `std::invalid_argument`. A value outside the new range is clamped into
    it. An open calendar is closed, raising `drop_down_changed`, and then
    `value_changed` is raised if the value was clamped.

See Also
--------
DateTimeValue : The date and time value, with civil date arithmetic.
DateTimeFormatProvider : Month and day names and date and time patterns.
NumericUpDown : A text field for a number, with up and down buttons.

Notes
-----
A click is a press and release of the primary button over the same part of
the field. A click on the check box toggles it. Outside up-down mode, a
click anywhere else opens or closes the calendar. In up-down mode, a click
on the upper or lower half of the button at the right steps the date one day
forward or back, and a click on the text does nothing. While the field has
focus, F4 and Alt+Down open the calendar, Up and Right step one day forward,
Down and Left step one day back, and Space toggles the check box when it is
shown. In up-down mode F4 does nothing and Alt+Down steps back like Down.

Stepping changes only the date: the time of day is kept, and the arrow keys
step days even when `format` shows only the time. A step that would leave
the range is ignored. If the check box is shown and unchecked, a step first
checks it, so `checked_changed` comes before `value_changed`.

The calendar holds the keyboard focus while it is open. Left and Right move
the selected day by one, Up and Down by a week, and Page Up and Page Down
change the month; dates outside the range are shown dimmed and cannot be
chosen. Enter or a click on a day commits the date: `value_changed` is
raised if the date differs, and then the calendar closes. Escape, or a press
anywhere outside the calendar, closes it without changing the value. Hiding,
disabling or detaching the picker closes it too.

The patterns use these letters, repeated as shown: ``yy`` or ``yyyy`` for
the year; ``M``, ``MM``, ``MMM`` or ``MMMM`` for the month as a number, a
two-digit number, an abbreviated name or a full name; ``d`` or ``dd`` for
the day of the month and ``ddd`` or ``dddd`` for the abbreviated or full
weekday name; ``H`` or ``HH`` for the 24-hour hour; ``h`` or ``hh`` for the
12-hour hour; ``m`` or ``mm`` for minutes; ``s`` or ``ss`` for seconds; and
``t`` or ``tt`` for the first letter of, or the whole, AM or PM designator.
Text in single quotes is copied as it is, a backslash copies the next
character, and other characters are copied unchanged.

The control publishes the accessibility role `date_picker`, with the
formatted value as its value, the state `SemanticState::checkable` when the
check box is shown and `SemanticState::checked` when it is also checked, and `SemanticState::expanded` while the
calendar is open. It supports `SemanticAction::increment` and
`SemanticAction::decrement`, which step one day;
`SemanticAction::set_value`, which takes an ISO date such as ``2026-12-24``,
keeps the time of day and refuses a date that does not exist or is outside
the range; `SemanticAction::press`, which toggles the check box when it is
shown; and `SemanticAction::expand` and `SemanticAction::collapse` outside
up-down mode. The open calendar is published with the role `calendar`, its
days as `date_cell` nodes named
``<stable_id>.overlay.calendar.day.<yyyy-mm-dd>``, and buttons for the
previous and next month.

Examples
--------
Limit a due date to the current year and show it in the short format:

.. code-block:: cpp

   std::shared_ptr<gf::DateTimePicker> due =
       gf::make_control<gf::DateTimePicker>(gf::StableId("task.due"));
   (*due).set_range({2026, 1U, 1U}, {2026, 12U, 31U, 23U, 59U, 59U, 999U});
   (*due).set_value({2026, 10U, 11U});
   (*due).set_format(gf::DateTimePickerFormat::short_date);
   // formatted_value() is "10/11/2026"

An optional date, held as one `std::optional` so that "no date" and a date
cannot disagree. Unchecking the box keeps the picker's own date, and
stepping with the keyboard checks it again before changing the date:

.. code-block:: cpp

   struct Reminder final : gf::Component {
       std::shared_ptr<gf::DateTimePicker> when =
           gf::make_control<gf::DateTimePicker>(gf::StableId("reminder.when"));
       // Empty while the box is unchecked.
       std::optional<gf::DateTimeValue> date{};

       Reminder() {
           (*when).set_show_check_box(true);
           date = (*when).value();
           gf::on((*when).checked_changed(), *this, &Reminder::on_checked);
           gf::on((*when).value_changed(), *this, &Reminder::on_date);
       }
       void on_checked(bool checked) {
           if (checked) {
               date = (*when).value();
           } else {
               date.reset();
           }
       }
       void on_date(gf::DateTimeValue value) {
           if ((*when).checked()) date = value;
       }
   };

   void example() {
       Reminder reminder;
       (*reminder.when).set_checked(false);   // date is empty
       std::shared_ptr<gf::Panel> root =
           gf::make_control<gf::Panel>(gf::StableId("reminder.root"));
       (*reminder.when).set_requested_bounds({10.0, 10.0, 240.0, 29.0});
       (*root).add_child(reminder.when);
       gf::Window window(root, {320.0, 300.0});
       window.perform_layout();
       window.request_focus(reminder.when);
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::up});
       // date is January 2, 2000
   }
