A button with an arrow that asks the application to open a menu.

In `DropDownButtonMode::menu` the whole button is the disclosure: any
activation raises `drop_down_requested` instead of `clicked`. In
`DropDownButtonMode::split` the button keeps a primary action and reserves a
strip at its trailing or bottom edge for the arrow; clicking the strip raises
`drop_down_requested`, and clicking elsewhere raises `clicked` as an ordinary
`Button` does. The button does not own a menu. Show a `ContextMenu` from the
`drop_down_requested` handler and report the menu's open state back with
`set_drop_down_open`. Use a plain `Button` when there is nothing to choose,
and a `ComboBox` when the user picks a value rather than a command.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.
text
    Caption. `&` marks the next character as the mnemonic, so `"&Export"`
    responds to Alt+E.
mode
    `DropDownButtonMode::menu` (the default) or `DropDownButtonMode::split`.
    Any other value throws `std::invalid_argument`.

Properties
----------
drop_down_mode : default ``DropDownButtonMode::menu``
    Whether the whole button opens the menu or only its arrow strip. An
    unknown value throws `std::invalid_argument`.
drop_down_width : default ``16.0``
    Extent of the arrow strip on `drop_down_edge`, between 12 and 64; other
    values throw `std::invalid_argument`. Changing it moves the reserved
    `content_padding` with it, so set your own padding after this property.
drop_down_edge : default ``DropDownButtonEdge::right``
    `right` puts the arrow strip at the trailing edge; `bottom` puts it below
    the caption, as on a ribbon button. Changing it moves the reserved
    padding to the other axis.
drop_down_open : default ``false``
    Whether the button draws itself open, with a dark line along its lower
    edge, and reports itself to accessibility as expanded. The button never
    changes it; the application sets it while its menu is shown.

Events
------
drop_down_requested
    Raised when the user asks for the menu: by clicking the button in menu
    mode or its arrow strip in split mode, by Space, Enter or the mnemonic on
    a menu-mode button, by Alt+Down while it has focus, by an assistive
    technology's show-menu or expand action, or by `perform_drop_down`. It
    carries the button.
drop_down_close_requested
    Raised when an assistive technology asks to collapse a button whose
    `drop_down_open` is true. It carries the button; close the menu in the
    handler.

Methods
-------
perform_drop_down
    Raises `drop_down_requested` after the same checks as `perform_click`.
    It returns false, and raises nothing, when the button is hidden, disabled
    or disposed, or when focus validation refuses.

See Also
--------
ContextMenu : The overlay menu to show from `drop_down_requested`.
Button : A push button with a single action.
CommandBar : A row of command buttons.
ComboBox : A drop-down list for choosing a value.

Notes
-----
In split mode the region is decided when the primary button goes down, so
pressing the main area and releasing over the arrow still raises `clicked`,
and the reverse still requests the menu. Space and Enter always take the
primary action in split mode; Alt+Down requests the menu. In menu mode
`clicked` is never raised, so a `Command` bound with `bind` never runs from a
menu-mode button.

Neither mode applies the inherited `dialog_result` to the window: the
split-mode primary action raises `clicked` without it, and a menu-mode button
never raises `clicked`.

The default `content_padding` is `Insets{6.0, 4.0, 22.0, 4.0}`, which leaves
room for the 16-unit arrow strip, so the preferred width is 16 more than a
`Button` with the same caption.

The control publishes the accessibility role `button` with the actions focus,
press, show menu, and expand or collapse, and the expanded state while
`drop_down_open` is true. Press performs the button's activation, so in menu
mode it requests the menu.

Examples
--------
A split button: the main area runs the default action and the arrow asks for
the menu:

.. code-block:: cpp

   struct RunButton final : gf::Component {
       std::shared_ptr<gf::DropDownButton> button = gf::make_control<gf::DropDownButton>(
           gf::StableId("build.run"), "&Run", gf::DropDownButtonMode::split);
       int runs = 0;
       int menu_requests = 0;

       RunButton() {
           gf::on((*button).clicked(), *this, &RunButton::on_run);
           gf::on((*button).drop_down_requested(), *this, &RunButton::on_menu);
       }
       void on_run() { ++runs; }
       void on_menu() { ++menu_requests; }
   };

   void example() {
       RunButton run;
       (*run.button).perform_click();       // run.runs is now 1
       (*run.button).perform_drop_down();   // run.menu_requests is now 1
   }

Show a `ContextMenu` below the button and keep the button's open state in
step with the menu:

.. code-block:: cpp

   // compile only
   struct ExportButton final : gf::Component {
       gf::ContextMenu menu{"export.menu"};
       std::shared_ptr<gf::DropDownButton> button =
           gf::make_control<gf::DropDownButton>(gf::StableId("export"), "&Export");

       ExportButton() {
           menu.set_items({gf::MenuItemSpec(
               "export.pdf", gf::MenuItemKind::command,
               std::make_shared<gf::Command>("export.pdf", "PDF"), "PDF")});
           gf::on((*button).drop_down_requested(), *this, &ExportButton::open_menu);
           gf::on((*button).drop_down_close_requested(), *this, &ExportButton::close_menu);
           gf::on(menu.open_changed(), *this, &ExportButton::menu_open_changed);
       }
       void open_menu(gf::DropDownButton& source) {
           const gf::Rect bounds = source.absolute_bounds();
           menu.show(source.shared_from_this(), gf::Point{bounds.x, bounds.y + bounds.height},
                     gf::MenuOpenMode::pointer);
       }
       void close_menu() { menu.close(); }
       void menu_open_changed(bool open) { (*button).set_drop_down_open(open); }
   };

   void example() { ExportButton exporter; }
