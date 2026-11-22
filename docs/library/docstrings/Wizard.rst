A wizard: a guided sequence of pages shown one at a time, with a header,
an optional step indicator, and Back, Next (Finish on the last page) and
Cancel buttons.

The pages are the `WizardPage` children in child order; other children are
ignored. The header shows the current page's title and description, and the
step indicator names every page by its title. Next is available while the
current page's ``can_advance`` is set, and Back on every page but the first.
Finish raises `finished` and Cancel raises `cancelled`; the wizard itself
stays as it is, and its owner closes it. Use a `TabControl` when the user
may visit the pages in any order.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
current_page : default ``0``
    The page shown, counted from zero among the `WizardPage` children. A
    write moves there at once and raises `page_changed`, without
    `page_changing`. A page past the last throws `std::invalid_argument`;
    zero is always accepted.
show_step_indicator : default ``true``
    Whether the step indicator shows above the page.
back_text : default ``"< Back"``
    The Back button's text. It is localizable; invalid UTF-8 throws
    `std::invalid_argument`. `next_text` (default ``"Next >"``),
    `finish_text` (``"Finish"``, the Next button on the last page) and
    `cancel_text` (``"Cancel"``) are the other buttons' texts, under the same
    rules.
pages
    The `WizardPage` children, in order.
page_count
    The number of pages.
can_go_next
    Whether Next (or Finish) is available now.
can_go_back
    Whether Back is available now.
back_button
    The Back `Button`; `next_button` and `cancel_button` are the others.
step_indicator
    The wizard's `StepIndicator`.

Events
------
page_changing
    Raised before a move by `go_next`, `go_back`, `go_to`, the buttons or the
    step indicator, with the page shown and the page asked for; a handler
    refuses the move by setting the event's ``cancel``.
page_changed
    Raised after the page shown changed, with the new page.
finished
    Raised when the user finishes on the last page, or `go_next` does.
cancelled
    Raised by Cancel, Escape or `cancel`.

Methods
-------
page
    The page at an index.
go_next
    Moves to the next page, or finishes on the last; false when the current
    page cannot advance or a handler refused.
go_back
    Moves to the previous page; false on the first page or when refused.
go_to
    Moves to a page through `page_changing`; false when the index is out of
    range, is the current page, or the move is refused.
cancel
    Raises `cancelled`.

See Also
--------
WizardPage : One page of the wizard.
StepIndicator : The row of steps above the page.

Notes
-----
A press on a completed step in the indicator goes back to it, through
`page_changing` like the buttons. Enter left unhandled by the focused
control acts as Next or Finish, and Escape as Cancel.

To assistive technology the wizard is a group named by the current page's
title. A theme draws the header's ``caption`` (the title, in the heading
face) and ``label`` (the description, secondary), and the ``divider`` above
the buttons, all of the panel family; the indicator and the buttons are
stock controls.
