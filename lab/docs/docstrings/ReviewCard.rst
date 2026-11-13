A card that presents one review record: a title, a summary and a verdict.

`ReviewCard` is a `Card` whose header, body and footer are three `Label`
controls it owns. Give it a `ReviewRecord` with `set_record` and it shows the
record's title as a heading, its summary as wrapped body text and its verdict
as a caption, names itself after the title for accessibility, and draws a
pending or rejected record in the theme's busy or invalid state. Like any
card it is static until `interactive = true`, after which the user can
click it or press Space or Enter to activate it. Use a plain `Card` to
compose other content.

Parameters
----------
stable_id
    Identity of the card, unique within its window. Its labels get the
    identities `<stable_id>.title`, `<stable_id>.summary` and
    `<stable_id>.verdict`.

Properties
----------
record : default ``ReviewRecord{}``
    The record shown. The default record is empty, and the card shows nothing
    until `set_record`. A record needs a `key` of 1 to 128 bytes and a
    `title` of 1 to 512 bytes; the `summary` may have up to 8192 bytes and the
    `verdict` up to 1024, all valid UTF-8, with a defined `disposition`.
    Otherwise `set_record` throws `std::invalid_argument` and nothing
    changes. Setting an equal record does nothing.
title_label
    The header label, which shows the title in the theme's heading font,
    vertically centred.
summary_label
    The body label, which shows the summary in the body font, wrapped at
    words and aligned to the top.
verdict_label
    The footer label, which shows the verdict in the caption font,
    vertically centred.

Events
------
record_changed
    Raised after `set_record` has updated the labels, the accessible name and
    description, and the visual status. It carries the new record.

Methods
-------
initialize_control_tree
    Installs the three labels as the card's header, body and footer.
    `make_control` calls it, and calling it again does nothing.

See Also
--------
Card : Header, body and footer composition, interaction and selection.
Label : The control used for each section.

Notes
-----
`set_record` writes the record's text into the three labels, sets the card's
accessible name to the title and its accessible description to the summary,
and sets `visual_status` from the disposition: `ReviewDisposition::pending`
becomes `ControlVisualStatus::pending`, `ReviewDisposition::rejected`
becomes `ControlVisualStatus::invalid`, and every other disposition becomes
`ControlVisualStatus::normal`. These values replace any you set yourself.
Neutral, information, accepted and warning records therefore look the same.

You may restyle the labels through their ordinary `Label` properties, such as
fonts, colours and alignment, but the card rewrites their text on every
`set_record`.

Layout follows `Card`: the title and verdict take fixed-height header and
footer strips from the theme, and the summary fills the space between them.

With `interactive = true` the card takes focus when clicked, and a click
or Space or Enter raises the inherited `activated` event, after applying the
card's `selection_behavior`. The labels never intercept the pointer, so a
click anywhere on the card reaches it.

The card publishes the accessibility role `group`, or `list_item` when
interactive. Its name is the title, its description the summary and its value
the verdict, and it reports busy for a pending record and invalid for a
rejected one. It is not exposed until a record has been set.

Examples
--------
Show a pending review:

.. code-block:: cpp

   std::shared_ptr<gf::ReviewCard> card =
       gf::make_control<gf::ReviewCard>(gf::StableId("review.render"));
   (*card).record = gf::ReviewRecord{
       .key = "render-pipeline",
       .title = "Render pipeline",
       .summary = "Painting records renderer-neutral commands before host replay.",
       .verdict = "Measured, partial",
       .disposition = gf::ReviewDisposition::pending};
   // (*card).visual_status() is now gf::ControlVisualStatus::pending

Make the card clickable and react when the user opens it:

.. code-block:: cpp

   struct Reviews final : gf::Component {
       std::shared_ptr<gf::ReviewCard> card =
           gf::make_control<gf::ReviewCard>(gf::StableId("review.fonts"));
       std::string opened;

       Reviews() {
           (*card).record = gf::ReviewRecord{.key = "fonts",
                                               .title = "Bundled fonts",
                                               .summary = "Control faces ship with the library.",
                                               .verdict = "Accepted",
                                               .disposition = gf::ReviewDisposition::accepted};
           (*card).interactive = true;
           (*card).selection_behavior = gf::CardSelectionBehavior::select_on_activation;
           gf::on((*card).activated(), *this, &Reviews::on_open);
       }
       void on_open() { opened = (*card).record.read().key; }
   };

   void example() {
       Reviews reviews;
       (*reviews.card).set_requested_bounds({0.0, 0.0, 320.0, 140.0});
       gf::Window window(reviews.card, gf::Size{320.0, 140.0});
       window.perform_layout();
       window.dispatch_pointer(gf::PointerEvent{gf::PointerAction::down,
                                                gf::PointerButton::primary, gf::Point{40.0, 70.0}});
       window.dispatch_pointer(gf::PointerEvent{gf::PointerAction::up,
                                                gf::PointerButton::primary, gf::Point{40.0, 70.0}});
       // the card is now selected, and reviews.opened is "fonts"
   }
