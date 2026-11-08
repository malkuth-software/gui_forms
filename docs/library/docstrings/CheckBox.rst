A box with a caption that the user checks and unchecks.

A check box presents an independent on/off choice, or with `three_state` a
third "indeterminate" value for mixed selections. Clicking it, pressing Space
or Enter while it has focus, or pressing its mnemonic advances its state and
then raises `clicked`. Use a `RadioButton` or a `ChoiceGroup` instead when the
choices exclude each other.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
text
    Caption drawn beside the box. `&` marks the next character as the
    mnemonic, so `"&Wrap lines"` responds to Alt+W; write `&&` for a
    literal ampersand.

Properties
----------
checked : default ``false``
    Whether the box is checked. Setting it is the same as setting
    `check_state` to `CheckState::checked` or `CheckState::unchecked`.
check_state : default ``CheckState::unchecked``
    The full state, including `CheckState::indeterminate`. When
    `three_state` is false, setting `indeterminate` stores `checked`
    instead.
three_state : default ``false``
    Whether activation cycles through unchecked, checked and indeterminate.
    Turning it off while the box is indeterminate changes the state to
    checked.
auto_check : default ``true``
    Whether activation changes the state. When false, a click only raises
    `clicked`, and the application sets the state itself.
appearance : default ``CheckBoxAppearance::normal``
    `CheckBoxAppearance::button` draws the control as a toggle button. The
    accessibility role, state and input behaviour stay those of a check box.
indicator_style : default ``ChoiceIndicatorStyle::classic``
    The indicator drawn for `appearance` `normal`: a classic box, a modern
    box or a toggle switch.

Events
------
check_state_changed
    Raised after `check_state` changes, whether by input or by a setter. It
    carries the new state.
checked_changed
    Raised after `checked` changes, immediately after `check_state_changed`.
    A change between checked and indeterminate raises only
    `check_state_changed`.

Methods
-------
bind
    `bind(Value<bool>&)` keeps `checked` and the value in step. The value
    wins when the binding starts, so the box first takes the value's state;
    after that a click updates the value and a change to the value updates
    the box. `bind(Command&)` connects the box to a command instead, which
    replaces any value binding.
unbind
    Ends the value binding. The box keeps its current state.

See Also
--------
RadioButton : One choice from a group of mutually exclusive options.
ChoiceGroup : A row of exclusive choices bound to a `Value`.

Notes
-----
When the user activates the box, the order is: the state changes,
`check_state_changed` and then `checked_changed` are raised, and finally
`clicked` is raised. A setter raises the two state events but never
`clicked`. Setting the current state again raises nothing.

The control publishes the accessibility role `check_box`, with the value
`checked`, `unchecked` or `mixed` and the state `checkable`. With
`use_mnemonic`, the character after the `&` in its text is its access key.

Examples
--------
Bind a check box to an application setting. The value wins when the
binding starts, and afterwards each side follows the other:

.. code-block:: cpp

   gf::Value<bool> wrap_lines(true);
   std::shared_ptr<gf::CheckBox> wrap =
       gf::make_control<gf::CheckBox>(gf::StableId("options.wrap"), "&Wrap lines");
   (*wrap).bind(wrap_lines);   // the box is now checked
   wrap_lines.set(false);      // and now unchecked

React to changes with an owned handler. `on` keeps the connection on the
owner, so it ends when the owner is destroyed:

.. code-block:: cpp

   struct Options final : gf::Component {
       std::shared_ptr<gf::CheckBox> wrap = gf::make_control<gf::CheckBox>(
           gf::StableId("options.wrap"), "&Wrap lines");
       bool wrapping = false;

       Options() { gf::on((*wrap).checked_changed(), *this, &Options::on_wrap); }
       void on_wrap(bool checked) { wrapping = checked; }
   };

   void example() {
       Options options;
       (*options.wrap).checked = true;   // options.wrapping is now true
   }
