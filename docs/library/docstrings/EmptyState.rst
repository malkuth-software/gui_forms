What a list, a page or a search shows when it has nothing to show, such as
"No messages" in an empty mailbox.

The control draws a column centred in itself: the `image`, the `title`,
the `description` and the optional action control, each only when it has
something to show. It draws no face of its own, so the container's ground
shows through.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
image : default none
    The picture above the title, drawn in a 48-unit square.
title : default empty
    The heading that says what is empty; it names the control to
    assistive technology. Invalid UTF-8 throws `std::invalid_argument`.
description : default empty
    The line below the title that says why, or what to do next, in the
    muted ink; it describes the control to assistive technology. Invalid
    UTF-8 throws `std::invalid_argument`.
action
    The action control, owned as a child and placed below the
    description, or null. `set_action(Control::Ptr control)` installs one
    and returns the detached predecessor; a control that already has a
    parent throws `std::logic_error`.

See Also
--------
InfoBanner : A message strip across a form or a page.

Notes
-----
The parts take the theme's ``panel`` family. The title and the description
are single lines, centred; text wider than the control is clipped. The
control publishes the accessibility role `group`, named by its title and
described by its description, with the action inside it. It takes no focus
or pointer input of its own.

Examples
--------
An empty mailbox with an action:

.. code-block:: cpp

   std::shared_ptr<gf::EmptyState> empty = gf::make_control<gf::EmptyState>(gf::StableId("mail.empty"));
   (*empty).title = "No messages";
   (*empty).description = "Messages you receive appear here.";
   gf::Control::Ptr previous = (*empty).set_action(gf::make_control<gf::Button>(gf::StableId("mail.compose"), "Write a message"));
   // previous is null: the control had no action before
