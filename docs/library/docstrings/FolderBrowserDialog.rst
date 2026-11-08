The host's dialog for choosing a folder.

A folder browser dialog asks the user for a folder, as Windows Forms'
FolderBrowserDialog does. It holds its settings (the text it shows and
where it starts) and, after the user accepts, the chosen folder.
`show_dialog` shows it modally over a window and returns when it closes.
The host draws the dialog: `SHBrowseForFolderW` on Windows, with the
description above its tree; `NSOpenPanel` choosing directories on macOS;
and a dialog of GUI.Forms controls on X11.

The dialog is shown through the window's `HostServices` (capability
``dialogs``). A window without a host, or a host without the capability,
does not show it: `show_dialog` returns ``none`` and `status` says why.

Properties
----------
description : default empty
    The text the dialog shows: above the tree on Windows, as the title
    elsewhere. Empty gives the host's own. A write throws
    `std::invalid_argument` for text that is not valid UTF-8, holds NUL or
    is longer than `HostServices::maximum_dialog_text_bytes`, and keeps the
    old text.
initial_directory : default empty
    The folder the dialog starts in while no folder is selected; empty lets
    the host choose. Refused as `description` is.
selected_path : default empty
    The chosen folder, or empty. A folder set before the dialog is shown is
    the one it starts in, as in Windows Forms. Only an accepted dialog
    changes it.
status
    The host's answer to the last `show_dialog`.

Methods
-------
show_dialog
    Shows the dialog over `owner` and returns when it closes: ``ok`` when the
    user chose a folder, ``cancel`` when the user cancelled, ``none`` when
    the dialog could not be shown. Throws `std::logic_error` for a disposed
    dialog, for one already showing, and off the window's UI thread.
reset
    Restores every setting to its default and forgets the chosen folder.

See Also
--------
OpenFileDialog : The dialog for choosing files.
HostServices : The dialog contract under the component.

Notes
-----
A callback the host runs inside its modal loop may dispose or destroy the
dialog; `show_dialog` then returns ``cancel`` without touching it. Each
request has its own identity, unique in the process and shared with the
other dialog components.

Not yet supported: the root folder and hiding the new-folder button (the
host request has a field for neither; the Windows dialog always offers the
button), and Windows Forms' choice between the description and the title
on Windows.

Examples
--------
A dialog for choosing where exports go, shown over a window:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("doc.root"));
   gf::Window window(root, {320.0, 200.0});
   gf::FolderBrowserDialog dialog;
   dialog.description = "Choose where the exports go";
   const gf::DialogResult result = dialog.show_dialog(window);
   if (result == gf::DialogResult::ok) {
       const std::string folder = dialog.selected_path.read();
       static_cast<void>(folder);
   } else if (result == gf::DialogResult::none) {
       // Without a host, dialog.status().error is gf::HostServiceError::unsupported.
   }
