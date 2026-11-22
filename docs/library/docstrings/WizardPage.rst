One page of a `Wizard`: a panel whose controls are the page's content,
with the title and description the wizard shows, and the page's validation
gate.

While the page is shown, `can_advance` enables or disables the wizard's Next
(or Finish) button, so the page can hold the user until its fields are
valid. A `WizardPage` outside a `Wizard` is an ordinary `Panel`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
title : default empty
    The page's title, shown in the wizard's header and as its step's name.
    It is localizable; invalid UTF-8 throws `std::invalid_argument`.
description : default empty
    The line under the title in the wizard's header; empty for none. It is
    localizable; invalid UTF-8 throws `std::invalid_argument`.
can_advance : default ``true``
    Whether Next (or Finish) is available on this page.

See Also
--------
Wizard : The sequence the page belongs to.
