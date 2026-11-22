An application's About box: the information icon, the product and version
as the heading, the description and copyright, the credits behind an
expander, and OK.

Windows Forms offers this as Visual Studio's AboutBox form template rather
than a control. Here it is a preset over `TaskDialog`, an owned in-window
dialog that looks and behaves the same on every host. `show` returns at
once, and `closed` reports the result later. What the box shows is an
`AboutInfo`: ``product`` and ``version`` make the heading (``"Product
1.2"``), ``description`` and ``copyright`` are the body's paragraphs, and
``credits`` (licences, acknowledgements) sit behind a "Show credits"
expander, left out when empty. An empty ``caption`` gives
"About <product>". For the program's identity card with its links, see
`BusinessCard`.

Parameters
----------
stable_id
    Identity of the component.
info
    What the dialog shows. Text that is not valid UTF-8 throws
    `std::invalid_argument`.

Properties
----------
info
    What the dialog shows.
shown
    Whether the dialog is shown.
task_dialog
    The `TaskDialog` this dialog shows, for automation and tests; null once
    disposed.

Events
------
closed
    Raised when the dialog closes, with ``DialogResult::ok``: for OK, Enter
    and Escape, and when its window takes the dialog away.

Methods
-------
show
    Shows the dialog over an owner's window. Returns false when it is
    already shown or the owner is not attached to a window. Throws
    `std::logic_error` for a disposed dialog.
close
    Closes the dialog as OK would; does nothing when it is not shown.

See Also
--------
TaskDialog : The dialog this one presets.
BusinessCard : The program's identity card.

Notes
-----
The dialog is used on the UI thread of the window it is shown over.
Disposing or destroying a shown dialog closes it without raising `closed`.
