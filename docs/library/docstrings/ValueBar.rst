A value bar: one number shown as a fill (the slider form) or a field (the
number form), changed by scrubbing, typing and stepping, as in Blender.

`minimum` and `maximum` are the hard limits; `soft_minimum` and
`soft_maximum` the scrub and fill range (none means the hard limits, and a
soft limit outside the hard range is held at it). Scrubbing stays within the
soft range; a typed value may leave it but never the hard range, and the
fill then shows full or empty with an overrange marker at that end. Use a
`TrackBar` for a slider with ticks, and a `NumericUpDown` for a plain number
field.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
soft_minimum : default none
    The low end of the scrub and fill range, or none for the hard minimum.
    A value that is not finite throws `std::invalid_argument`.
soft_maximum : default none
    The high end of the scrub and fill range, or none for the hard maximum.
    A value that is not finite throws `std::invalid_argument`.
snap_step : default none
    The grid Ctrl snaps to while scrubbing, from the soft minimum, or none
    for the small change. A value that is not positive and finite throws
    `std::invalid_argument`.
precision : default ``3``
    The decimals the value shows, from 0 to `maximum_value_bar_precision`
    (12); outside that range throws `std::invalid_argument`.
unit : default ``ValueUnit::none``
    The unit the value is shown and typed in; the value is held in the
    unit's base.
default_value : default ``0``
    The value Backspace resets to, clamped into the hard range when applied.
    A value that is not finite throws `std::invalid_argument`.
presentation : default ``ValueBarStyle::fill``
    The fill style (a slider: the value's proportion of the soft range as a
    fill under the text) or the field style (a number field, with step
    arrows at both ends while hovered).
label : default empty
    The name shown at the bar's start, with the value at its end; empty
    centres the value. It is localizable.
scrub_rate : default none
    Value units per logical unit of scrub travel, or none for the soft range
    over the bar's width. A value that is not positive and finite throws
    `std::invalid_argument`.
formatted_value
    The value as shown: in the unit's display unit, at the precision.
effective_soft_minimum
    The soft range's low end in force: the soft limit, else the hard one,
    held within the hard range. `effective_soft_maximum` is the high end.
soft_proportion
    The value's proportion of the soft range, from 0 to 1: the fill's width.
overrange
    Whether the value lies outside the soft range.
editing
    Whether the inline editor is open.
edit_defect
    Why the last typed text was refused (`ValueTextDefect`).
editor
    The inline editor's `TextBox`.
scrubbing
    Whether a scrub is running, past its threshold or from the keyboard.
fill_text_contrast
    The contrast of the label's two halves in the current theme and state
    (`ValueBarContrast`): over the track and over the fill, against 4.5:1
    (7:1 in high contrast).
track_bounds
    The track, in local coordinates. `fill_bounds` is the fill (empty in the
    field style), `content_bounds` the text area where the editor lies while
    open, `decrement_bounds` and `increment_bounds` the arrows (empty in the
    fill style), and `overrange_bounds` the overrange marker (empty within
    the soft range).

Events
------
value_committed
    Raised after each completed user edit, with the new value: a scrub's
    release, a typed commit, a step, a reset, a negation or a paste. Never
    raised by a programmatic write, a scrub's preview or a restore.
    ``value_changed`` follows every change, previews included.

Methods
-------
begin_edit
    Opens the inline editor with the formatted value selected; false while
    disabled or scrubbing.
commit_edit
    Parses the editor's text. Accepted, the value (clamped to the hard
    range) is applied, `value_committed` raised and the editor closed;
    refused, the editor stays open and `edit_defect` says why.
cancel_edit
    Closes the editor, changing nothing.
apply_text
    Applies a text as if typed: returns the defect, or none when it was
    applied (clamped to the hard range).
step_by
    Steps the value by a delta, clamped to the hard range; raises
    `value_committed` when the value changed.
reset_to_default
    Resets the value to `default_value`; raises `value_committed` when it
    changed.
negate
    Negates the value within the hard range; raises `value_committed` when
    it changed.
copy_value
    Copies the value as text to the clipboard; false without the window's
    host services. `paste_value` pastes it back as typed text.

See Also
--------
TrackBar : A slider with ticks.
NumericUpDown : A number field with arrows.

Notes
-----
A press and a drag sideways scrubs: Shift slows it, Ctrl snaps to
`snap_step`, and Escape or a right press restores the start value; the
release raises `value_committed` once. The press captures the pointer, so a
scrub that leaves the bar keeps tracking until the release. Where the host
offers relative pointer motion the pointer is hidden and the scrub is
unbounded; otherwise it stops at the window's edge. A lost capture restores
the start value.

A click without a drag, Enter or F2 opens the inline editor. Enter, Tab or
focus leaving it commits the typed text (bounded arithmetic and units,
clamped to the hard range); Escape cancels; a refused text keeps the editor
open with the defect shown, and focus leaving it then cancels the edit. Tab
commits and keeps the focus on the bar.

Focused, Left and Right or Down and Up step by the small change (a tenth
with Shift, the large change with Ctrl), Page Up and Page Down by the large
change, Home and End go to the soft limits, Backspace resets to
`default_value`, Minus negates, and Ctrl+C and Ctrl+V copy and paste the
value. In the field style the arrows at both ends step by the small change.
Ctrl with the wheel steps while hovered; a plain wheel is left to the
parent, so a bar never takes over scrolling.

To assistive technology the fill style is a slider and the field style a
spin button, each with its range and `formatted_value` as its value text;
the inline editor is an edit while it is open. The parts are ``track``,
``fill``, ``label``, ``value``, the arrows and their glyphs, ``overrange``,
``validation`` and ``focus``. The label and value are painted twice,
clipped: over the track in their own colour and over the fill in the label
part's ``fill-text-color``, so the text stays readable where the fill
crosses it.
