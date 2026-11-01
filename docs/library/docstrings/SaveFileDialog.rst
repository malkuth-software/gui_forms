The host's dialog for choosing where to save a file.

A save file dialog asks the user for a path to save to, as Windows Forms'
SaveFileDialog does. It holds its settings (a title, a starting directory, a
filter, a default extension and whether to ask before replacing a file)
and, after the user accepts, the chosen path. `show_dialog` shows it
modally over a window and returns when it closes. The host draws the
dialog: `GetSaveFileNameW` on Windows, `NSSavePanel` on macOS, and a dialog
of GUI.Forms controls on X11.

The dialog is shown through the window's `HostServices` (capability
``dialogs``). A window without a host, or a host without the capability,
does not show it: `show_dialog` returns ``none`` and `status` says why.

Properties
----------
title : default empty
    The dialog's title; empty gives the host's own. `set_title` throws
    `std::invalid_argument` for text that is not valid UTF-8, holds NUL or
    is longer than `HostServices::maximum_dialog_text_bytes`, and keeps the
    old title.
initial_directory : default empty
    The directory the dialog starts in; empty lets the host choose. Refused
    as `title` is.
filter : default empty
    The filter in Windows Forms' syntax, as `OpenFileDialog` reads it:
    ``"Reports|*.txt|All files|*.*"``. `set_filter` throws
    `std::invalid_argument` for a string `parse_file_dialog_filter` refuses,
    and keeps the old filter.
filters
    The host filters the filter parsed into, in order.
filter_index : default ``0``
    The filter the dialog starts with, an index into `filters` counting from
    zero (Windows Forms' ``FilterIndex`` counts from one). After the user
    accepts it is the filter they had selected, where the host tells: every
    host does, except that the host's own "All files" entry (X11's dialog
    adds one) is no filter of the program's and leaves the index as it was.
    An index past the filters starts the dialog on the first.
    A chosen name without an extension takes the selected filter's first
    extension (unless that filter is any file), else `default_ext`.
default_ext : default empty
    The extension a chosen name without one is given, without its dot; a
    leading dot is removed when it is set. An extension holding a wildcard,
    a path separator or a character Windows forbids in a name is refused.
add_extension : default ``true``
    Whether a chosen name without an extension is given an extension: the
    selected filter's, or `default_ext` (see `filter_index`). A
    name has an extension when it holds a dot that is not its last
    character, as .NET's ``Path.HasExtension`` says.
overwrite_prompt : default ``true``
    Whether the host asks before the user chooses a file that exists.
file_name
    The chosen path, or empty. Before the dialog is shown, a name set with
    `set_file_name` is the one the dialog suggests.
file_names
    The chosen path as a list of one, or none.
status
    The host's answer to the last `show_dialog`.

Events
------
file_ok
    Raised when the user accepts, before the dialog takes the path. The
    payload's ``file_names`` holds the path, with `default_ext` already
    added. Setting ``cancel`` keeps the old path and shows the dialog again
    with the same settings, where Windows Forms keeps its dialog open.

Methods
-------
show_dialog
    Shows the dialog over `owner` and returns when it closes: ``ok`` when the
    user accepted and no `file_ok` handler cancelled, ``cancel`` when the
    user cancelled, ``none`` when the dialog could not be shown. Throws
    `std::logic_error` for a disposed dialog, for one already showing, and
    off the window's UI thread.
reset
    Restores every setting to its default and forgets the chosen path.

See Also
--------
OpenFileDialog : The dialog for choosing files that exist.
HostServices : The dialog contract under the component.
FileDialogOk : The payload of `file_ok`.

Notes
-----
The hosts add the default extension themselves (Windows and X11 to a typed
name without one, macOS as the type it allows when the filter names none),
so it is sent to the host only while `add_extension` is on; the dialog adds
it to a path the host returned without one. A path the addition would make
longer than the dialog contract allows is kept as chosen. Windows Forms
first tries the extension of the selected filter; GUI.Forms does not yet
know which filter the user selected, so it uses `default_ext` alone.

A `file_ok` handler may dispose or destroy the dialog; `show_dialog` then
returns ``cancel`` without touching it. A handler that cancels and destroys
the owner window also ends the call with ``cancel``. Each request has its
own identity, unique in the process and shared with the other dialog
components.

Not yet supported: the filter index (the host request has no field for it),
the prompt to create a file that does not exist, checking that the chosen
directory exists (each host's save dialog chooses only existing
directories), and opening the chosen file; GUI.Forms writes no files for
the program.

Examples
--------
A dialog for exporting a report, shown over a window:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("doc.root"));
   gf::Window window(root, {320.0, 200.0});
   gf::SaveFileDialog dialog;
   dialog.set_title("Export report");
   dialog.set_filter("Reports|*.txt|All files|*.*");
   dialog.set_default_ext("txt");
   dialog.set_file_name("summary");
   const gf::DialogResult result = dialog.show_dialog(window);
   if (result == gf::DialogResult::ok) {
       const std::string& path = dialog.file_name();
       static_cast<void>(path);
   } else if (result == gf::DialogResult::none) {
       // Without a host, dialog.status().error is gf::HostServiceError::unsupported.
   }
