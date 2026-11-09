A round button with a caption that the user selects from a group of exclusive options.

A radio button presents one option among several that exclude each other,
such as a page orientation. Clicking it, pressing Space or Enter while it
has focus, or pressing its mnemonic checks it and unchecks the other radio
buttons of its group, then raises `clicked`. A group is every radio button
with the same parent and the same `group_name`; give the buttons of a
second group in the same container another name. Use a `ChoiceGroup` for a
row of choices bound to a `Value` with arrow-key navigation, and a
`CheckBox` for an independent on/off option.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
text
    Caption drawn beside the indicator. `&` marks the next character as the
    mnemonic, so ``"&Landscape"`` responds to Alt+L; write `&&` for a
    literal ampersand.

Properties
----------
checked : default ``false``
    Whether the button is checked. Setting it to true first unchecks any
    checked sibling in the same group, then checks this button; setting it
    to false unchecks only this button and checks no other. Also available
    to bindings as the ``Checked`` property.
group_name : default ``""``
    The name of the button's group among its siblings. Buttons with the
    same parent and the same name exclude each other; the default empty
    name groups all unnamed radio buttons of a container. Moving a checked
    button into a group with another checked button unchecks the other.
auto_check : default ``true``
    Whether activation checks the button. When false, a click only raises
    `clicked`, and the application sets `checked` itself.
indicator_style : default ``ChoiceIndicatorStyle::classic``
    The indicator drawn beside the caption. `ChoiceIndicatorStyle::toggle`
    draws a switch instead of the round indicator. `modern` draws the
    checked indicator in the accent colour, but only for a button given its
    own style with `set_style`; under the theme it looks like `classic`.

Events
------
checked_changed
    Raised after `checked` changes, whether by input, by the setter or by a
    sibling being checked. It carries the new state. When a button is
    checked, the sibling it unchecks raises `checked_changed` first.

Methods
-------
on_activate
    Checks the button when `auto_check` is true and it is not already
    checked, then raises `clicked`. A click, Space or Enter, the mnemonic,
    a semantic press and `perform_click` all end here.
semantic_descriptor
    Publishes the role `radio_button`, with the value ``selected`` or
    ``not selected`` and, when checked, the states `checked` and
    `selected`.

See Also
--------
ChoiceGroup : A row of exclusive choices bound to a `Value`.
CheckBox : An independent on/off choice.
GroupBox : A captioned frame that holds one group of radio buttons.

Notes
-----
When the user activates an unchecked button, the order is: the previously
checked sibling raises `checked_changed` with false, this button raises
`checked_changed` with true, and finally `clicked` is raised. Activating a
button that is already checked raises only `clicked`. A setter never raises
`clicked`, and setting the current state again raises nothing.

Exclusion happens only when a button is checked or its `group_name`
changes. Adding a checked button to a container that already holds a
checked button of the same group leaves both checked, and a button without
a parent excludes nothing.

Each radio button is a separate tab stop, and the arrow keys do not move
between the buttons of a group. Use a `ChoiceGroup` when the group should
have a single tab stop and arrow-key navigation.

The control publishes the accessibility role `radio_button`.

Examples
--------
Put two exclusive options in a group box. Checking the second unchecks the
first:

.. code-block:: cpp

   std::shared_ptr<gf::GroupBox> orientation =
       gf::make_control<gf::GroupBox>(gf::StableId("print.orientation"), "Orientation");
   std::shared_ptr<gf::RadioButton> portrait =
       gf::make_control<gf::RadioButton>(gf::StableId("print.portrait"), "&Portrait");
   std::shared_ptr<gf::RadioButton> landscape =
       gf::make_control<gf::RadioButton>(gf::StableId("print.landscape"), "&Landscape");
   (*orientation).add_child(portrait);
   (*orientation).add_child(landscape);
   (*portrait).checked = true;
   (*landscape).checked = true;   // portrait is unchecked first

Keep two groups in one panel apart with `group_name`, and react to one
button with an owned handler:

.. code-block:: cpp

   struct PrintOptions final : gf::Component {
       std::shared_ptr<gf::Panel> panel = gf::make_control<gf::Panel>(gf::StableId("print"));
       std::shared_ptr<gf::RadioButton> portrait = gf::make_control<gf::RadioButton>(
           gf::StableId("print.portrait"), "&Portrait");
       std::shared_ptr<gf::RadioButton> landscape = gf::make_control<gf::RadioButton>(
           gf::StableId("print.landscape"), "&Landscape");
       std::shared_ptr<gf::RadioButton> colour = gf::make_control<gf::RadioButton>(
           gf::StableId("print.colour"), "&Colour");
       std::shared_ptr<gf::RadioButton> mono = gf::make_control<gf::RadioButton>(
           gf::StableId("print.mono"), "&Black and white");
       bool landscape_page = false;

       PrintOptions() {
           (*portrait).group_name = "orientation";
           (*landscape).group_name = "orientation";
           (*colour).group_name = "ink";
           (*mono).group_name = "ink";
           (*panel).add_child(portrait);
           (*panel).add_child(landscape);
           (*panel).add_child(colour);
           (*panel).add_child(mono);
           gf::on((*landscape).checked_changed(), *this, &PrintOptions::on_landscape);
       }
       void on_landscape(bool checked) { landscape_page = checked; }
   };

   void example() {
       PrintOptions options;
       (*options.portrait).checked = true;
       (*options.colour).checked = true;   // another group: portrait stays checked
       (*options.landscape).perform_click();  // unchecks portrait; landscape_page is true
   }
