The common base of every clickable control: text, image, input and the click.

`ButtonBase` supplies what `Button`, `DropDownButton`, `CheckBox`,
`RadioButton` and `LinkLabel` share: a caption with an optional mnemonic, an
optional image, focus, pointer and keyboard activation, command binding and the
`clicked` event. You normally create one of those concrete controls rather than
a `ButtonBase`; refer to `ButtonBase&` when one handler serves several kinds of
button, because `clicked` passes the control that was activated.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID. A bound `Command`
    receives it as the invocation's source.
initial_text
    Caption. `&` marks the next character as the mnemonic, so `"&Save"`
    responds to Alt+S; write `&&` for a literal ampersand. A `\n` starts a new
    line.

Properties
----------
text : default ``""``
    The caption, including any `&` mnemonic marker. Setting a different value
    raises `text_changed`.
font : default ``FontSpec{FontRole::control, 12.0, 400, false, 0.24}``
    Font used to measure and paint the caption. It is scaled by the window's
    text scale. An invalid specification throws `std::invalid_argument`.
text_line_spacing : default ``1.25``
    Line height of a multi-line caption, as a multiple of the font size. It
    must lie between 0.75 and 3.0, or the setter throws
    `std::invalid_argument`.
style
    The colours used when the control paints in the classic style. Until you
    call `set_style` this is the theme's basic style and the control paints
    from the theme's visual recipes; after `set_style` it paints the classic
    relief in the given colours. `clear_style` returns to the theme.
image : default ``ImageId{}``
    An image from the window's image registry, drawn with the caption. Setting
    it clears `image_index` and `image_key`. While the control is in a window,
    an ID that is not live in that window throws `std::invalid_argument`.
image_list : default ``nullptr``
    An `ImageList` from which `image_index` or `image_key` picks the image.
    The list must be live and belong to the control's window; the button then
    shows the list's hot, pressed, selected and disabled variants as its state
    changes.
image_index : default ``-1``
    Index into `image_list`, or -1 for none. Setting it clears `image` and
    `image_key`. Values below -1, or past the end of the list, throw
    `std::out_of_range`.
image_key : default ``""``
    Key into `image_list`. Setting it clears `image` and `image_index`. The
    key must be valid UTF-8 of at most 256 bytes.
image_alignment : default ``ContentAlignment::middle_center``
    Where the image sits when `text_image_relation` is `overlay` or there is
    no caption.
text_alignment : default ``ContentAlignment::middle_center``
    Where the caption, or the caption and image together, sit inside the
    content area.
text_image_relation : default ``TextImageRelation::overlay``
    How the image and caption are arranged: on top of each other, or side by
    side and stacked in either order.
image_gap : default ``4.0``
    Space between image and caption when both are shown side by side or
    stacked; between 0 and 64.
content_padding : default ``Insets{6.0, 4.0, 6.0, 4.0}``
    Space between the frame and the content. Each side must lie between 0 and
    128. The control's preferred size grows with it.
use_mnemonic : default ``true``
    Whether `&` in `text` marks a mnemonic. When false the ampersand is drawn
    and Alt shortcuts are ignored.
expanded_state : default ``std::nullopt``
    Optional disclosure state for a button that opens or shows something. A
    value adds expanded or collapsed state and an expand or collapse action
    to the accessibility tree. Setting a different value raises
    `expanded_changed`.
connection_topology : default ``std::nullopt``
    Places the button in a joined row or column of buttons, as segment
    `index` of `count`, so adjoining segments share edges. The count must be
    at least 2 and the index below it, or the setter throws
    `std::invalid_argument`.
command_connected
    Whether a `Command` is currently bound with `bind`.

Events
------
clicked
    Raised when the control is activated: by a primary-button click, by Space
    or Enter while it has focus, by its mnemonic, as the window's accept or
    cancel button, by an assistive-technology press, or by `perform_click`.
    It carries the control. A subclass changes its own state first, so a
    check box is already toggled and a link already visited when it is
    raised. A `DropDownButton` in menu mode raises `drop_down_requested`
    instead.
text_changed
    Raised after `text` changes. It carries the new text.
expanded_changed
    Raised after `expanded_state` changes. It carries the new state.

Methods
-------
bind
    `bind(Command&)` connects the control to a command, replacing any earlier
    command. The control's enabled state follows the command, a check box's
    checked state and a button's `selected` state follow a checkable
    command, and each activation executes the command once, through the
    window's command chain while the control has a window (decision DK-47:
    a target on the chain takes it first; `Command::execute_routed`). The
    command runs as a `clicked` handler connected by `bind`, so handlers you
    connected earlier run before it. The control borrows the command; a
    disposed command is never invoked. Binding a disposed command throws
    `std::invalid_argument`.
unbind_command
    Ends the command connection. The control keeps its current enabled state.
perform_click
    Activates the control as a mnemonic would. It returns false, and raises
    nothing, when the control is disposed, hidden or disabled, or when focus
    validation of the currently focused control refuses the change.
clear_style
    Removes the colours given to `set_style` and returns to theme painting.
clear_image
    Removes the direct `image`.
set_visual_recipes
    Replaces the theme's visual recipe for this control with one recipe per
    surface state (normal, hot, pressed, disabled and so on). High-contrast
    themes still use their own recipe, and colours given to `set_style` take
    precedence. Invalid recipes throw `std::invalid_argument`.
clear_visual_recipes
    Returns to the theme's recipe.

See Also
--------
Button : A push button, with default and dialog-result behaviour.
CheckBox : An on/off choice built on `ButtonBase`.
RadioButton : One choice of a mutually exclusive group.
LinkLabel : Clickable text drawn as a hyperlink.
DropDownButton : A button that opens a menu.

Notes
-----
Pointer input: pressing the primary button gives the control focus and draws
it pressed. Moving off it while the button is held releases the pressed look,
and moving back restores it. Releasing over the control raises `clicked`;
releasing elsewhere does nothing. If focus validation refuses to let focus
leave the current control, the press is consumed and no click follows.

Keyboard input: Space or Enter draws the focused control pressed, and
releasing the same key raises `clicked`. Auto-repeat does not repeat the
click, and losing focus while the key is held cancels it. Because the focused
control handles Enter itself, Enter activates the focused button rather than
the window's accept button. Alt with the mnemonic letter activates the
control from anywhere in the window, through the same validation as
`perform_click`; when several controls share a letter, repeated presses cycle
through them in tab order.

The focus rectangle appears only while the window shows keyboard focus cues,
so a mouse click focuses the button without drawing it.

The control publishes the accessibility role `button`. Its name is the
accessible name, or else the caption without the mnemonic marker. It offers
the actions focus and press; press runs `perform_click`. With an
`expanded_state` it also reports expanded and offers expand or collapse, and
those actions raise `clicked` without changing `expanded_state`, so the
application updates it in its handler. A `visual_status` of pending or invalid
is reported as busy or invalid.

The preferred size fits the caption and image plus `content_padding`, and is
at least 24 high; a requested width or height takes precedence.

Examples
--------
Several kinds of button can share one handler, because `clicked` passes the
control that was activated:

.. code-block:: cpp

   struct Toolbar final : gf::Component {
       std::shared_ptr<gf::Button> bold =
           gf::make_control<gf::Button>(gf::StableId("format.bold"), "&Bold");
       std::shared_ptr<gf::LinkLabel> help =
           gf::make_control<gf::LinkLabel>(gf::StableId("format.help"), "Formatting help");
       std::string last;

       Toolbar() {
           gf::on((*bold).clicked(), *this, &Toolbar::on_click);
           gf::on((*help).clicked(), *this, &Toolbar::on_click);
       }
       void on_click(gf::ButtonBase& source) {
           last = std::string(source.stable_id().value());
       }
   };

   void example() {
       Toolbar toolbar;
       (*toolbar.help).perform_click();   // toolbar.last is now "format.help"
   }

Bind a button to a command. The button is enabled only while the command is,
and a click executes it:

.. code-block:: cpp

   gf::Command save("file.save", "Save");
   std::shared_ptr<gf::Button> button =
       gf::make_control<gf::Button>(gf::StableId("file.save"), "&Save");
   (*button).bind(save);
   save.set_enabled(false);    // the button is now disabled
   save.set_enabled(true);
   (*button).perform_click();  // executes save once
