A message strip across a form or a page, such as "Changes saved" or
"Connection lost; retrying".

The banner draws its severity's icon and a keyline along its start edge,
then the `title` and the `message` on a line each, then the optional action
control, then a close mark while `closable` is set. Pressing and releasing
the close mark, Escape while the banner has focus, or the close semantic
action calls `close`, which hides the banner and raises `closed`.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
severity : default ``NoticeSeverity::information``
    How much the message matters: information, warning, success or error.
    It chooses the icon and the keyline's colour, and is the banner's one
    custom state.
title : default empty
    The title, in a heavier weight; it names the banner to assistive
    technology. Invalid UTF-8 throws `std::invalid_argument`.
message : default empty
    The message, on the line below the title; it describes the banner to
    assistive technology. Invalid UTF-8 throws `std::invalid_argument`.
closable : default ``true``
    Whether the banner shows a close mark, takes focus and closes on
    Escape. `close` still works when it is off.
action
    The action control, owned as a child and placed at the end of the text
    row, or null. `set_action(Control::Ptr control)` installs one and
    returns the detached predecessor; a control that already has a parent
    throws `std::logic_error`.

Events
------
closed
    Raised after `close` hides the banner.

Methods
-------
close
    Hides the banner and raises `closed`; does nothing while it is hidden.

See Also
--------
Badge : A small count or dot.

Notes
-----
The face takes the theme's ``notice`` family. The severity is published as
exactly one of the custom states ``information``, ``warning``, ``success``
and ``error``, so a theme selects ``:family(notice):state(warning)``. Until
the theme floor gives the status colours roles, the keyline and the icon
use a built-in colour per severity, which high contrast replaces with the
theme's own. The title and the message are single lines; text past a
line's end is clipped. The banner publishes the accessibility role
`live_region`, so a host announces it when it appears or changes, with the
close action while it is closable.

Examples
--------
A warning with a retry action:

.. code-block:: cpp

   std::shared_ptr<gf::InfoBanner> offline = gf::make_control<gf::InfoBanner>(gf::StableId("sync.offline"));
   (*offline).severity = gf::NoticeSeverity::warning;
   (*offline).title = "Offline";
   (*offline).message = "Changes will sync when the connection returns.";
   gf::Control::Ptr previous = (*offline).set_action(gf::make_control<gf::Button>(gf::StableId("sync.retry"), "Retry"));
   // previous is null: the banner had no action before
