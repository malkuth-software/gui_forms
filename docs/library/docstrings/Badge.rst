A small count or dot, such as the number of unread messages beside a mailbox.

In count mode the badge draws an accent pill holding `count`. Once the
count passes `maximum` the pill shows the maximum followed by ``+`` (``99+``
by default). A zero count hides the pill unless `show_zero` is set, and a
hidden badge measures empty, so a layout closes up around it. In dot mode
the badge draws a small dot without text, to say that something is new
without saying how much.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
count : default ``0``
    The number shown in count mode.
maximum : default ``99``
    The largest count shown as it is; a larger count shows the maximum
    followed by ``+``. Zero throws `std::invalid_argument` and keeps the
    old maximum.
mode : default ``BadgeMode::count``
    Whether the badge shows a count pill or a dot.
show_zero : default ``false``
    Whether a zero count still shows its pill.
display_text
    The text the pill shows now: the count, the maximum followed by ``+``,
    or nothing in dot mode or for a hidden zero.
showing
    Whether the pill or dot is drawn.

See Also
--------
Label : Text without a pill.

Notes
-----
The pill takes the theme's ``badge`` family, which has normal and disabled
looks only; dot mode adds the custom state ``dot`` for a theme's
selectors. The badge takes no focus or pointer input. It publishes the
accessibility role `static_text`, named by its accessible name or else by
the text it shows; a hidden badge, or a dot without an accessible name, is
not exposed. A badge is a control of its own (owner decision D7), placed
beside what it counts, not an adornment drawn over another control.

Examples
--------
An unread count beside a caption, capped at 9:

.. code-block:: cpp

   std::shared_ptr<gf::Badge> unread = gf::make_control<gf::Badge>(gf::StableId("inbox.unread"));
   (*unread).maximum = 9U;
   (*unread).count = 12U;
   (*unread).accessible_name = "12 unread messages";
   // unread->display_text() is "9+"
