A switch for a setting that takes effect as soon as it is turned, such as
"Wi-Fi" on or off.

The control draws a rounded track with a thumb at its start (off) or its
end (on), the label after it and, when given, the state text after the
label. Unlike a CheckBox it has no third state.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
initial_text
    The label, default empty.

Properties
----------
checked : default false
    Whether the switch is on. A change raises `checked_changed`.
on_text : default empty
    The state text shown while the switch is on, such as "On". Invalid
    UTF-8 throws `std::invalid_argument`.
off_text : default empty
    The state text shown while the switch is off. Invalid UTF-8 throws
    `std::invalid_argument`.
state_text
    The state text that shows now: `on_text` while on, `off_text` while
    off.
thumb_position
    Where the thumb stands, from 0 at the track's start (off) to 1 at its
    end (on); between while it travels.

Events
------
checked_changed
    Raised after `checked` changes, with its new value. Activation (a
    click, Space or the press action) turns the switch over first, so
    `checked_changed` comes before `clicked`.

See Also
--------
CheckBox : A box that is checked, unchecked or indeterminate.

Notes
-----
The track and the thumb take the theme's ``choice`` family; the thumb is
drawn as the toggle knob. The natural width keeps room for the wider of the
two state texts, so turning the switch does not change its size. When
`checked` changes the thumb travels to its new end in 150 ms on the
window's motion clock, while the state, the event and the semantics change
at once; under reduced motion, in an occluded window or outside a window it
stands at its end at once. To assistive technology the switch is a ``toggle_switch`` (a host
without a mapping publishes a check box), named by its label and checked
while on.

Examples
--------
A switch with state text:

.. code-block:: cpp

   std::shared_ptr<gf::ToggleSwitch> wifi = gf::make_control<gf::ToggleSwitch>(gf::StableId("settings.wifi"), "Wi-Fi");
   (*wifi).on_text = std::string{"On"};
   (*wifi).off_text = std::string{"Off"};
   (*wifi).checked = true;
   // (*wifi).state_text() is "On"
