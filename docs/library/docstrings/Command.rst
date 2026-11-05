An application action, such as Save or Copy, shared by every button, menu item and shortcut that triggers it.

A command holds the state that all of its presentations show, such as
whether it is enabled or checked, its text and its shortcut label, and one
`invoked` event that runs whichever way the user triggers it. Bind buttons
and check boxes to it with `bind`, list it in `MenuItemSpec` items of a
`ContextMenu` or `MenuStrip`, and give it a key with `bind_shortcut`. Disable
the command once and every presentation is disabled. The command does not
own its presentations. Bound buttons borrow it, and destroying or disposing
it disconnects them; menu items share it through `std::shared_ptr`.

Parameters
----------
stable_id
    Identity of the command, reported in every invocation. The default
    constructor makes an anonymous command with an empty identity and text,
    suitable as a member of an owner. With an identity, it must be nonempty,
    and both it and `text` must be valid UTF-8, or the constructor throws
    `std::invalid_argument`.
text
    The command's display text. Menu items show it when their spec has no
    text of its own.

Properties
----------
stable_id
    The identity given to the constructor, or an empty string.
state
    The command's current `CommandState`: `text`, `description`, `icon_id`,
    `shortcut`, `mnemonic`, `availability_reason`, `enabled`
    (default true), `visible` (default true), `checked` and `checkable`
    (both default false), `default_action`, `destructive`, and a
    `generation` counter that increases with every change.

Events
------
state_changed
    Raised after any part of `state` changes, carrying the new state. Each
    setter raises it once when it changes something and not at all when the
    value is already current.
invoked
    Raised each time the command runs, carrying the command's ID, the ID of
    the source that triggered it, such as a button's stable ID, a menu row's
    control ID or ``"shortcut"``, and a sequence number that starts at 1.

Methods
-------
execute
    `execute(source_id)` runs the command: it raises `invoked` and returns
    true. When the command is disabled or disposed, or `source_id` is not
    valid UTF-8, nothing is raised and it returns false. Bound buttons run
    the command through `execute`.
execute_routed
    `execute_routed(window, source_id)` runs the command through
    `window`'s command routing chain: the first `CommandTarget` with a
    handler for the command's identity runs it, and when none has one,
    `invoked` is raised as `execute` raises it. Menus, the ribbon, tool
    strips and shortcuts run their commands this way, so a window with no
    targets behaves as `execute` does. Returns false, running nothing, when
    the command is disabled or disposed, `source_id` is not valid UTF-8, or
    the window refused a routing nested too deeply.
bind_shortcut
    `bind_shortcut(window, gesture)` makes a key gesture in `window` run the
    command and returns an `AcceleratorToken`; the shortcut lasts as long as
    you keep the token, and destroying the command or the window removes
    it. A disabled command lets the key pass on. A gesture without a key
    throws `std::invalid_argument`.
set_text
    Changes `state().text`. Invalid UTF-8 throws `std::invalid_argument`, as
    it does for every string setter.
set_description
    A longer explanation, added to the accessible description of the
    command's menu items.
set_shortcut
    The shortcut label, such as ``"Ctrl+S"``, drawn at the right of the
    command's menu items. It is only a label: use `bind_shortcut` to make
    the key work.
set_availability_reason
    Why the command is unavailable. Menu items add it to their accessible
    description while the command is disabled.
set_icon_id
    An icon identifier kept in `state` for your own presentations; the stock
    menus do not draw it.
set_mnemonic
    A mnemonic kept in `state` for your own presentations. Menus take their
    mnemonics from the `&` in the item text instead.
set_enabled
    Enables or disables the command. A disabled command cannot run, and
    bound buttons, menu items and shortcuts are disabled with it.
set_visible
    Shows or hides the command. Menus leave out items whose command is
    hidden when they next open.
set_checked
    Makes the command checkable, if it was not already, and sets whether it
    is checked. Bound check boxes follow it and bound buttons show it as
    selected; menu check and radio items show a mark.
clear_checked
    Makes the command no longer checkable, which also clears `checked`.
set_default_action
    Marks the command as the default action; menus draw its item in bold.
set_destructive
    Marks the command as destructive, such as Delete; menus draw its item in
    red.

See Also
--------
ButtonBase : Buttons and check boxes bind to a command with `bind`.
ContextMenu : An overlay menu whose items run commands.
MenuStrip : A menu bar whose menus run commands.
CommandBar : A toolbar of command items.
CommandBinding : An older helper that also copies the text to a button.

Notes
-----
Running a command does not change its checked state. A bound `CheckBox` is
the exception: clicking it changes its state, which sets the command's
checked state, and then runs the command. For a menu check item, set the
new state in your `invoked` handler.

`button.bind(command)` borrows the command: the button follows `enabled`, a
bound `CheckBox` follows `checked` while the command is checkable, and a
`Button` shows `checked` as its selected state. Clicking the button runs the
command once. A disposed command is never run through a surviving button.

A command's identity also selects its handler in a window's command routing
chain (`Window::route_command`): a focused control and its containers, the
active panel, the active document and the main window may each hold a
`CommandTarget` with a handler for it, so Save typed in a tool panel reaches
the active document. A handler that takes the command replaces `invoked`
for that run.

A shortcut is tried only after the focused control and its containers have
declined the key, so text boxes and open menus get the first chance at it.
The most recently bound shortcut for a gesture is tried first.

Disposing the command disconnects every handler of `state_changed` and
`invoked`.

Examples
--------
One Save command behind a toolbar button and Ctrl+S. Disabling it disables
both:

.. code-block:: cpp

   struct Document final : gf::Component {
       gf::Command save{"file.save", "Save"};
       int saves = 0;

       Document() { gf::on(save.invoked(), *this, &Document::on_save); }
       void on_save() { ++saves; }
   };

   void example() {
       Document document;
       document.save.set_shortcut("Ctrl+S");
       std::shared_ptr<gf::Button> button =
           gf::make_control<gf::Button>(gf::StableId("toolbar.save"), "Save");
       (*button).bind(document.save);
       gf::Window window(button, {120.0, 32.0});
       const gf::AcceleratorToken shortcut = document.save.bind_shortcut(
           window, gf::KeyGesture{gf::PhysicalKey::s, gf::Modifier::control});

       gf::KeyEvent key;
       key.physical_key = gf::PhysicalKey::s;
       key.modifiers = gf::Modifier::control;
       static_cast<void>(window.dispatch_key(key));   // document.saves is 1
       static_cast<void>((*button).perform_click());  // document.saves is 2

       document.save.set_enabled(false);              // the button is disabled
       static_cast<void>(window.dispatch_key(key));   // nothing runs
   }

A checkable command shared by a check box. The command's state wins when the
box is bound, and a click changes both:

.. code-block:: cpp

   gf::Command wrap("view.wrap", "Word wrap");
   wrap.set_checked(true);
   std::shared_ptr<gf::CheckBox> box =
       gf::make_control<gf::CheckBox>(gf::StableId("view.wrap.box"), "&Word wrap");
   (*box).bind(wrap);                          // the box is checked
   static_cast<void>((*box).perform_click());  // both are now unchecked
