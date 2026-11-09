A slider the user drags, clicks or steps with the keyboard to pick a value in a range.

A track bar shows a thumb on a track between `minimum` and `maximum`, with
optional tick marks. The user drags the thumb, clicks the track to move the
thumb straight to that point, turns the mouse wheel, or uses the arrow, Page
Up, Page Down, Home and End keys. The range, the value, the step sizes, the
orientation, `bind` and the events all come from `RangeControl`. Use a
`NumericUpDown` instead when the user needs to enter an exact number, and a
`ScrollBar` to scroll a view.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
tick_frequency : default ``10``
    The spacing of the tick marks in value units. The track shows the whole
    number of intervals that fit in the range, plus one mark, spread evenly
    from end to end, and never more than 257 marks. It must be finite and
    greater than zero, or the setter throws `std::invalid_argument`. It only
    affects drawing: the value is not snapped to the ticks.
show_ticks : default ``true``
    Whether tick marks are drawn below a horizontal track or to the right of
    a vertical one. `TrackBarVisualStyle::compact` never draws them.
visual_style : default ``TrackBarVisualStyle::classic``
    `TrackBarVisualStyle::classic` draws a sunken track and a raised thumb.
    `TrackBarVisualStyle::filled` draws a thin track filled with the accent
    colour up to the thumb. `TrackBarVisualStyle::compact` draws the filled
    track with a smaller thumb and no tick marks.

See Also
--------
RangeControl : The range, value, steps, binding and events of a track bar.
NumericUpDown : A text field for an exact number, with up and down buttons.
ScrollBar : A bar that scrolls a view.
ProgressBar : A bar that shows progress and takes no input.

Notes
-----
Input changes the value in these steps, each raising `scroll` with the
matching `RangeAction` and then `value_changed`:

- Pressing the primary button on the control moves the thumb to the pointer
  (`RangeAction::thumb_track`), and the control keeps the pointer until the
  button is released. Dragging follows the pointer (`thumb_track`), clamped
  to the range, and releasing reports the final position
  (`thumb_position`). Values from the pointer are continuous; they are not
  rounded to `small_change` or to the ticks.
- The mouse wheel steps by `small_change`: up increases the value and down
  decreases it.
- Left and Down decrease by `small_change`, Right and Up increase by it,
  Page Down and Page Up step by `large_change`, and Home and End go to
  `minimum` and `maximum`. These keys work in both orientations; a vertical
  track bar has `maximum` at the top.

An input that would not change the value, for example Home at the minimum,
raises nothing. Losing focus ends a drag. A disabled track bar receives no
input. The track bar takes focus when clicked and shows it by highlighting
the thumb.

Without requested bounds, a horizontal track bar measures 160 by 28 and a
vertical one 28 by 160.

The control publishes the accessibility role `slider`, with its value as
text and as a number, its minimum and maximum, and `small_change` and
`large_change` as its steps. The
`SemanticAction::increment` and `SemanticAction::decrement` actions step by
`small_change`. The `SemanticAction::set_value` action takes a number as
text, clamps it into the range and reports `scroll` with
`RangeAction::thumb_position`; text that is not a complete finite number is
refused.

Examples
--------
Configure a vertical level control with a coarse page step:

.. code-block:: cpp

   std::shared_ptr<gf::TrackBar> level =
       gf::make_control<gf::TrackBar>(gf::StableId("mixer.level"));
   (*level).orientation = gf::Orientation::vertical;
   (*level).set_range(-60.0, 0.0);
   (*level).large_change = 6.0;
   (*level).tick_frequency = 6.0;   // eleven ticks, one every 6 dB
   (*level).value = -12.0;

Tell user input apart from programmatic changes. `scroll` reports only what
the user did, while `value_changed` follows every change:

.. code-block:: cpp

   struct Mixer final : gf::Component {
       std::shared_ptr<gf::TrackBar> volume =
           gf::make_control<gf::TrackBar>(gf::StableId("mixer.volume"));
       double level = 0.0;
       int user_moves = 0;
       gf::RangeAction last_action = gf::RangeAction::thumb_position;

       Mixer() {
           gf::on((*volume).scroll(), *this, &Mixer::on_scroll);
           gf::on((*volume).value_changed(), *this, &Mixer::on_value);
       }
       void on_scroll(const gf::RangeScrollEvent& event) {
           ++user_moves;
           last_action = event.action;
       }
       void on_value(double value) { level = value; }
   };

   void example() {
       Mixer mixer;
       (*mixer.volume).set_requested_bounds({0.0, 0.0, 200.0, 28.0});
       (*mixer.volume).value = 30.0;   // level is 30, user_moves is 0
       gf::Window window(mixer.volume, {200.0, 28.0});
       window.perform_layout();
       window.request_focus(mixer.volume);
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::right});
       // level is 31, user_moves is 1 and last_action is small_increment
   }
