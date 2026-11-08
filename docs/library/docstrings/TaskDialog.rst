A message dialog of pages: heading, text, icon, buttons, choices, details and progress.

A task dialog asks the user one question with more structure than a message
box, as Windows Forms' TaskDialog does. A `TaskDialogPage` holds a caption,
a heading and text, a standard icon or a custom image, standard and custom
buttons with a default button, radio buttons, a verification check box,
expanded information behind an expander, a footnote and a progress bar.
`show` lays the dialog over the owner's window; the button that closed it
arrives through `closed` and `result`. Setting another page while the dialog
is shown navigates to it, for multi-step questions.

The dialog is an owned, in-window dialog built from GUI.Forms controls
(`Label`, `Button`, `RadioButton`, `CheckBox`, `ProgressBar`), so it looks
and behaves the same on every host. Use the host's message dialogs through
`HostServices` when the platform's own dialog is wanted instead.

Parameters
----------
stable_id
    Identity of the dialog. Its children are named after it:
    ``<stable_id>.heading``, ``.text``, ``.button.<id>``, ``.radio.<id>``,
    ``.verification``, ``.expander``, ``.expanded-text``, ``.progress`` and
    ``.footnote``; the layer it sits in while shown is ``<stable_id>.layer``.

Properties
----------
page
    The page shown. `set_page` replaces it and rebuilds the children; while
    the dialog is shown this navigates, moves focus to the new page's default
    button and raises `navigated`. A page without buttons gets an OK button;
    a standard button with an empty ID or text takes its standard name
    (``ok``, ``cancel``, ``try_again``, ...) and text. Invalid UTF-8, an
    empty or repeated button or radio button ID, two checked radio buttons, a
    default button the page lacks, or progress bounds that are not finite and
    ordered throw `std::invalid_argument`.
shown
    Whether the dialog is over its owner's window.
result
    How the dialog last closed: the button, the verification check box and
    the checked radio button. None before the first close.
expanded
    Whether the expanded information shows. A write changes it and
    raises `expanded_changed`; the expander button toggles it.
verification_checked
    The verification check box's state; a write sets it, through the check
    box while the page shows one.
checked_radio_button
    The checked radio button's ID, or an empty string.
verification_check_box
    The verification `CheckBox`, or null for a page without one.
expander_button
    The expander's `Button`, or null for a page without an expander.
progress_bar
    The `ProgressBar`, or null for a page without one.

Events
------
button_clicked
    Raised when a button is clicked, by pointer, keyboard or `click`, before
    the dialog closes. Setting ``keep_open`` keeps it open; a button whose
    ``allow_close`` is false keeps it open without a handler. A handler may
    navigate with `set_page`.
closed
    Raised when the dialog closes, with the `TaskDialogResult`.
navigated
    Raised after `set_page` replaced the page of a shown dialog.
expanded_changed
    Raised after the expanded information shows or hides.
verification_changed
    Raised after the verification check box changes.
radio_button_changed
    Raised with the ID of a radio button the user or the program checked.

Methods
-------
show
    Shows the dialog over an owner's window in a full-client layer that
    takes every pointer press outside the dialog and holds keyboard focus in
    it until it closes; the default button takes focus. Returns at once, and
    false when the dialog is already shown, already has a parent, or the
    owner is not attached to a window.
close
    Closes the dialog with a named result, without raising
    `button_clicked`.
click
    Clicks a button by ID, as the user would. Returns false for an unknown or
    disabled button.
set_progress_value
    Moves the progress bar, clamping the value to its bounds.
button
    The `Button` for a button ID, or null.
radio_button
    The `RadioButton` for a radio button ID, or null.

See Also
--------
HostServices : The platform's own message, file and color dialogs.
AnchoredOverlayLayer : An overlay anchored to a control.
Button : The command buttons a page shows.

Notes
-----
GUI.Forms never runs a nested event loop, so `show` does not wait for the
user: the dialog is modal within its window (the layer under it takes every
pointer press, and its focus scope holds the keyboard) and reports its result
through `closed`. When the owner leaves its window the dialog closes as
cancelled. A disposed dialog closes without raising `closed`.

Enter clicks the default button unless a button has focus, which takes its
own Enter. Escape clicks the page's cancel button; on a page without one it
closes the dialog as ``cancel`` only when ``allow_cancel`` is set, and
otherwise does nothing.

The dialog paints its own parts through the by-role paint path: the ``face``
(card), the ``caption_band``, ``button_band`` and ``footnote_band`` (panel),
the ``button_divider``, the ``expander_glyph`` (a disclosure chevron, open
while expanded) and the icons. The vocabulary has no information, warning,
error or shield mark and the library bundles no icon images, so a standard
icon is an accent ``icon_plate`` chip carrying the letter ``i``
(information) or ``!`` (warning) as its ``icon_label``, the ``close`` mark
(error) as its ``icon_glyph``, or nothing (shield); the footnote icon is drawn
the same way at a smaller size. A custom image in ``icon_image`` replaces the
standard icon. Text, buttons, radio buttons, the check box and the progress
bar are child controls, published as composed parts. The dialog publishes
the accessibility role `dialog`, named by its caption or heading, and is
`modal` while shown.

Not supported yet: a progress bar's paused and error states, hyperlinks in
the text, a timer that ticks while shown, a command-link button style, and
icons of the window's title bar.

Examples
--------
Ask a question with a default button and a check box:

.. code-block:: cpp

   std::shared_ptr<gf::TaskDialog> dialog =
       gf::make_control<gf::TaskDialog>(gf::StableId("app.replace"));
   gf::TaskDialogPage page;
   page.heading = "Replace the saved stations?";
   page.text = "Replacing the list removes the stations you saved by hand.";
   page.icon = gf::TaskDialogIcon::warning;
   page.buttons = {{.standard = gf::TaskDialogStandardButton::yes},
                   {.standard = gf::TaskDialogStandardButton::no}};
   page.default_button = "no";
   page.verification_text = "Do not ask again";
   (*dialog).set_page(std::move(page));
   // (*dialog).show(owner) lays it over the owner's window; closed reports
   // the button ("yes" or "no") and the check box.
