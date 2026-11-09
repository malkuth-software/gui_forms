The File tab's full page: a column of commands and topics beside the topic's page.

A Backstage view (stage RB, phase 6; owner decisions 3 and 26, after Office
2010) shows a column down its left of fast commands, which run their
`Command`, and topics, which show their page beside the column. Pages are
child controls the application made. A `Ribbon` given a Backstage view
(`Ribbon::set_backstage`) opens it from the File tab over everything below
its tab row; Escape, the File tab, another tab or a definitive command
returns to the document. The view itself only asks to close.

Parameters
----------
stable_id
    Identity of the view.

Properties
----------
items
    The column (`BackstageItemSpec`: ID, kind, Command, text with an
    optional mnemonic, image, definitive flag, page). `set_items` keeps the
    selection on a topic with the same ID, else the first topic, and throws
    `std::invalid_argument`, keeping the old items, for a repeated ID or one
    that breaks the `StableId` segment rule, invalid UTF-8, a command without
    a `Command`, a topic without a page or text, a page given twice or owned
    elsewhere, a page or definitive flag where it does not belong, or more
    than `BackstageView::maximum_items` (64).
selected_topic
    The topic whose page shows. Assigning an item past the end throws
    `std::out_of_range`; an item that is not a topic, or no value, throws
    `std::invalid_argument`.
column_width : default ``132``
    The column's width in logical units at text scale 1.
    `set_column_width` throws `std::invalid_argument` outside 48 to 480.
realized_items
    The column's entries, their parts and states.

Events
------
topic_changed
    Raised after the selected topic changes.
close_requested
    Raised for Escape and after a definitive command ran; the owner closes
    the view.

Methods
-------
activate
    The one path every activation takes: runs a command (a definitive one
    then asks to close) or selects a topic. False for an unknown ID or a
    disabled command.

See Also
--------
Ribbon : The ribbon whose File tab opens it.
Command : The commands its column runs.

Notes
-----
Keyboard: Up and Down move through the column, skipping disabled entries,
Home and End go to its ends, Enter and Space activate, Escape asks to close,
and Tab leaves the column for the page's controls. Mnemonics in the entries'
texts activate them.

The view publishes ``region``, ``column``, ``page`` and ``focus``, and the
``backstage_item`` item system (``item_face``, ``item_icon``, ``item_label``,
``item_focus``; hot, pressed, selected, disabled, focus_visible). The column
and page are told apart by their surfaces (decision 26): the column on the
card family and the page on the window family, until the default theme (T4)
gives the column its own. To accessibility the view is a group whose
entries are positioned buttons (commands) and tabs (topics, selected while
shown).

Examples
--------
A Backstage view with a fast command and a topic, opened by a ribbon's File
tab:

.. code-block:: cpp

   std::shared_ptr<gf::Command> save = std::make_shared<gf::Command>("doc.save", "&Save");
   std::shared_ptr<gf::Label> info = gf::make_control<gf::Label>(gf::StableId("doc.info"), "Two pages");
   std::shared_ptr<gf::BackstageView> view = gf::make_control<gf::BackstageView>(gf::StableId("doc.backstage"));
   (*view).set_items({{.stable_id = "save", .command = save},
                      {.stable_id = "info", .kind = gf::BackstageItemKind::topic, .text = "&Info", .page = info}});
   std::shared_ptr<gf::Ribbon> ribbon = gf::make_control<gf::Ribbon>(gf::StableId("doc.ribbon"));
   (*ribbon).backstage = view;
