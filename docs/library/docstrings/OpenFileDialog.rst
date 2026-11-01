The host's dialog for choosing one or more existing files.

An open file dialog asks the user for files to open, as Windows Forms'
OpenFileDialog does. It holds its settings (a title, a starting directory, a
filter and whether several files may be chosen) and, after the user accepts,
the chosen paths. `show_dialog` shows it modally over a window and returns
when it closes. The host draws the dialog: `GetOpenFileNameW` on Windows,
`NSOpenPanel` on macOS, and a dialog of GUI.Forms controls on X11.

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
    The filter in Windows Forms' syntax: ``label|patterns`` pairs separated
    by ``|``, the patterns separated by ``;``, as in
    ``"Text files|*.txt;*.log|All files|*.*"``. A pattern is ``*``, ``*.*``
    (any file) or ``*.extension``; the hosts filter by extension only, so
    other patterns are refused. `set_filter` throws `std::invalid_argument`
    for a string `parse_file_dialog_filter` refuses, and keeps the old
    filter.
filters
    The host filters the filter parsed into, in order.
filter_index : default ``0``
    The filter the dialog starts with, an index into `filters` counting from
    zero (Windows Forms' ``FilterIndex`` counts from one). After the user
    accepts it is the filter they had selected, where the host tells: every
    host does, except that the host's own "All files" entry (X11's dialog
    adds one) is no filter of the program's and leaves the index as it was.
    An index past the filters starts the dialog on the first.
multiselect : default ``false``
    Whether the user may choose more than one file.
file_name
    The first chosen path, or empty. Before the dialog is shown, a name set
    with `set_file_name` is the one the dialog suggests; it becomes the one
    chosen path (none for an empty name).
file_names
    Every chosen path, in the host's order. Only an accepted dialog changes
    them.
safe_file_name
    The first chosen file's name without its directory.
safe_file_names
    Every chosen file's name without its directory.
status
    The host's answer to the last `show_dialog`.

Events
------
file_ok
    Raised when the user accepts, before the dialog takes the paths. The
    payload's ``file_names`` holds the chosen paths for the handler's call.
    Setting ``cancel`` keeps the old paths and shows the dialog again with
    the same settings, where Windows Forms keeps its dialog open.

Methods
-------
show_dialog
    Shows the dialog over `owner` and returns when it closes: ``ok`` when the
    user accepted and no `file_ok` handler cancelled, ``cancel`` when the
    user cancelled, ``none`` when the dialog could not be shown. Throws
    `std::logic_error` for a disposed dialog, for one already showing, and
    off the window's UI thread.
reset
    Restores every setting to its default and forgets the chosen paths.

See Also
--------
HostServices : The dialog contract under the component.
FileDialogOk : The payload of `file_ok`.

Notes
-----
A `file_ok` handler may dispose or destroy the dialog; `show_dialog` then
returns ``cancel`` without touching it. A handler that cancels and destroys
the owner window also ends the call with ``cancel``, since the dialog cannot
be shown again. Each
request has its own identity, unique in the process, so a dialog may be shown
while another component's host dialog is on the modal stack. The name part of
a path follows the rules of the platform the program runs on: Windows splits
at ``\`` and ``/``, the other hosts at ``/`` only.

Not yet supported: the filter index (the host request has no field for it),
the read-only check box, checking that a typed name exists (each host's open
dialog already accepts only existing files) and opening the chosen file;
GUI.Forms reads no files for the program.

Examples
--------
A dialog for one or more recordings, shown over a window:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("doc.root"));
   gf::Window window(root, {320.0, 200.0});
   gf::OpenFileDialog dialog;
   dialog.set_title("Open recordings");
   dialog.set_filter("Recordings|*.wav;*.flac|All files|*.*");
   dialog.set_multiselect(true);
   const gf::DialogResult result = dialog.show_dialog(window);
   if (result == gf::DialogResult::ok) {
       for (const std::string& path : dialog.file_names()) {
           static_cast<void>(path);
       }
   } else if (result == gf::DialogResult::none) {
       // Without a host, dialog.status().error is gf::HostServiceError::unsupported.
   }
