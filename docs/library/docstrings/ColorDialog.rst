The host's dialog for choosing a color.

A color dialog asks the user for a color, as Windows Forms' ColorDialog
does. It holds its settings (a title, the color it starts with and whether
a translucent color may be chosen) and, after the user accepts, the chosen
color. `show_dialog` shows it modally over a window and returns when it
closes. The host draws the dialog: `ChooseColorW` on Windows, an
`NSColorWell` in an alert on macOS, and a dialog of GUI.Forms controls on
X11.

The dialog is shown through the window's `HostServices` (capability
``dialogs``). A window without a host, or a host without the capability,
does not show it: `show_dialog` returns ``none`` and `status` says why.

Properties
----------
title : default empty
    The dialog's title; empty gives the host's own. Windows Forms' dialog
    has none; the hosts that title their dialog use it. A write throws
    `std::invalid_argument` for text that is not valid UTF-8, holds NUL or
    is longer than `HostServices::maximum_dialog_text_bytes`, and keeps the
    old title.
color : default opaque black
    The chosen color, and before the dialog is shown the one it starts
    with. Only an accepted dialog changes it.
allow_alpha : default ``false``
    Whether the user may choose a translucent color. While it is off, as in
    Windows Forms, the chosen color is taken as opaque whatever the host
    returns.
status
    The host's answer to the last `show_dialog`.

Methods
-------
show_dialog
    Shows the dialog over `owner` and returns when it closes: ``ok`` when the
    user chose a color, ``cancel`` when the user cancelled, ``none`` when
    the dialog could not be shown. Throws `std::logic_error` for a disposed
    dialog, for one already showing, and off the window's UI thread.
reset
    Restores every setting to its default.

See Also
--------
HostServices : The dialog contract under the component.

Notes
-----
A callback the host runs inside its modal loop may dispose or destroy the
dialog; `show_dialog` then returns ``cancel`` without touching it. Each
request has its own identity, unique in the process and shared with the
other dialog components.

Not yet supported: custom colors, opening the dialog with its full picker,
refusing the full picker and limiting the choice to solid colors (the host
request has a field for none of them).

Examples
--------
A dialog for choosing an accent color, shown over a window:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("doc.root"));
   gf::Window window(root, {320.0, 200.0});
   gf::ColorDialog dialog;
   dialog.color = gf::Color::rgba(0x27U, 0x74U, 0xB8U);
   const gf::DialogResult result = dialog.show_dialog(window);
   if (result == gf::DialogResult::ok) {
       const gf::Color accent = dialog.color.read();
       static_cast<void>(accent);
   } else if (result == gf::DialogResult::none) {
       // Without a host, dialog.status().error is gf::HostServiceError::unsupported.
   }
