A push button that carries out an action when the user clicks it.

The user clicks the button, presses Space or Enter while it has focus, or
presses Alt with its mnemonic letter, and the button raises `clicked`. In a
dialog, make one button the window's accept button so that Enter activates it,
and another the cancel button for Escape; give each a `dialog_result`, and
the window records the result of the button that was used. Use a `CheckBox` for an on/off
setting, a `DropDownButton` for a button that opens a menu, and a `CommandBar`
for a row of commands.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
text
    Caption. `&` marks the next character as the mnemonic, so `"&Apply"`
    responds to Alt+A; write `&&` for a literal ampersand.

Properties
----------
default_button : default ``false``
    Whether the button draws the default-button cue. `Window::set_accept_button`
    turns it on for the new accept button and off for the previous one.
    Setting it yourself changes only the appearance: Enter activates the
    window's accept button, not every button with this flag.
dialog_result : default ``DialogResult::none``
    The result the button gives its window when it is clicked. Assigning the
    button as the window's cancel button sets it to `DialogResult::cancel` if
    it is still `none`. A value outside the enumeration throws
    `std::invalid_argument`. Setting a different value raises
    `dialog_result_changed`.
visual_style : default ``ButtonVisualStyle::standard``
    With theme painting, `accent` draws the theme's emphasised button and
    `command` a command button with left-aligned content; `standard` and
    `flat` look the same. After `set_style`, each style has its own classic
    look, and `flat` draws a plain face with a border of `flat_border_width`.
selected : default ``false``
    A persistent "on" look for a button that represents the current choice,
    such as the active tab of a custom tab strip. It is reported to
    accessibility as selected. While a `Command` is bound, it follows the
    command's checked state, and stays false for a command that is not
    checkable. Setting a different value raises `selected_changed`.
flat_border_width : default ``1.0``
    Border width for the `flat` style after `set_style`; zero draws no
    border. A negative or non-finite width throws `std::invalid_argument`.
danger : default ``false``
    Marks a dangerous or irreversible action. The button's parts take the
    custom state ``danger``, and a warning mark leads its content, widening
    it by `leading_mark_column`, so the mark is a cue that does not rest on
    colour. A theme that styles ``button::part(face):state(danger)`` lights
    the face from inside (the default theme glows it red); the button fades
    the lit face slowly between `danger_glow_floor` and full over
    `danger_glow_period`, some twelve frames a second, only inside the
    face, and holds it full and steady under reduced motion, while
    disabled and outside a window.
danger_glow
    The lit face's current share of the danger glow, 1 while steady.
danger_glowing
    Whether the danger glow is pulsing.

Events
------
dialog_result_changed
    Raised after `dialog_result` changes, including when the button becomes
    the window's cancel button. It carries the new result.
selected_changed
    Raised after `selected` changes, whether by an assignment or by a bound
    command. It carries the new state. A click alone does not change
    `selected`.

See Also
--------
ButtonBase : Text, image, input and command binding shared by all buttons.
DropDownButton : A button that opens a menu, optionally with a primary action.
CheckBox : An independent on/off choice.
CommandBar : A toolbar row of command buttons with arrow-key navigation.
LinkLabel : Clickable text drawn as a hyperlink.

Notes
-----
When the button is activated it first raises `clicked`. Then, if
`dialog_result` is not `none` and the button is still in the same window, it
sets the window's `dialog_result`, which raises the window's
`dialog_result_changed` if the value changes. The value is read after `clicked`, so a handler may
change or clear it. The window does not close itself; observe its
`dialog_result_changed` and close the dialog there.

Enter and Escape reach the accept and cancel buttons only when the focused
control does not handle them. A focused button handles Enter itself and is
activated by it, and a focused `TextBox` handles both Enter and Escape, so
those keys do not reach the dialog buttons while such a control has focus.
The keys and mnemonics run
focus validation of the focused control first; when it refuses, the key is
consumed and the button is not clicked. Write `causes_validation = false`
on a Cancel button so that it works while a field holds an invalid value.

The control publishes the accessibility role `button`, with the selected
state while `selected` is true. Pointer, keyboard and mnemonic behaviour are
described under `ButtonBase`.

Examples
--------
A dialog with OK and Cancel. Enter activates the accept button, which hands
its result to the window:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("dialog"));
   std::shared_ptr<gf::Button> ok =
       gf::make_control<gf::Button>(gf::StableId("dialog.ok"), "OK");
   std::shared_ptr<gf::Button> cancel =
       gf::make_control<gf::Button>(gf::StableId("dialog.cancel"), "Cancel");
   (*ok).dialog_result = gf::DialogResult::ok;
   (*ok).set_requested_bounds({8.0, 8.0, 80.0, 28.0});
   (*cancel).set_requested_bounds({96.0, 8.0, 80.0, 28.0});
   (*cancel).causes_validation = false;
   (*root).add_child(ok);
   (*root).add_child(cancel);
   gf::Window window(root, gf::Size{184.0, 44.0});
   window.set_accept_button(ok);       // ok now draws the default cue
   window.set_cancel_button(cancel);   // cancel's dialog_result is now cancel
   window.dispatch_key(gf::KeyEvent{gf::KeyAction::down, gf::PhysicalKey::enter});
   // window.dialog_result() is now gf::DialogResult::ok

Run application code from a named handler. `on` keeps the connection on the
owner, so it ends when the owner is destroyed:

.. code-block:: cpp

   struct Printer final : gf::Component {
       std::shared_ptr<gf::Button> print =
           gf::make_control<gf::Button>(gf::StableId("file.print"), "&Print");
       int jobs = 0;

       Printer() { gf::on((*print).clicked(), *this, &Printer::on_print); }
       void on_print() { ++jobs; }
   };

   void example() {
       Printer printer;
       (*printer.print).perform_click();   // printer.jobs is now 1
   }
