A scrolling list of records whose rows expand to show the evidence behind them.

A correspondence view presents search results, messages, activity or
evidence ledgers. Each `CorrespondenceItem` shows a compact row with its
glyph, title, secondary text and metric; an expanded row adds the metadata,
a matched excerpt with its emphasis terms marked, and an information lane
with a heading, tags and detail. A row expands for any of three independent
reasons: it is the one pinned record, which the user toggles by clicking
it or pressing Space; it has the keyboard focus while the view is focused;
or the pointer has rested on it for `hover_intent_delay`. When rows grow or
shrink, the row the user is looking at stays where it was. Only the rows in
view are painted and published, and no child control is created per
record. Use an `ObjectView` for items of one size with icons or columns,
and a `ListBox` for plain text rows.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
items
    The records in display order. Empty by default. IDs must be unique and
    follow the `StableId` segment rule, every text field valid UTF-8, and every
    information tag and emphasis term nonempty, or `set_items` throws
    `std::invalid_argument`. Replacing the records keeps the selected,
    focused and pinned IDs that are still present, ends any hover
    expansion, and keeps the first row in view where it was. It raises
    `selection_changed` or `pin_changed` when the selected or pinned record
    is gone.
selected_id : default ``""``
    The ID of the selected record, or empty when nothing is selected.
    Setting an empty ID clears the selection; an ID that is not in `items`
    throws `std::out_of_range`. Selecting does not expand or pin a record.
focused_id
    The record with the keyboard focus. It moves with the arrow keys and
    with clicks, and is expanded while the view has focus. When the view
    gains focus with no focused record, the selected record, or else the
    first, takes the focus.
hovered_id
    The record under the pointer, or empty.
pinned_id : default ``""``
    The one pinned record, which stays expanded. Setting a new ID moves the
    pin, an empty ID removes it, and an ID that is not in `items` throws
    `std::out_of_range`.
compact_height : default ``45.0``
    Height of a compact row in logical pixels, before text scaling. Values
    outside 24 to 192, not below `expanded_height`, or not finite throw
    `std::invalid_argument`.
expanded_height : default ``126.0``
    Height of an expanded row in logical pixels, before text scaling.
    Values not above `compact_height`, above 512, or not finite throw
    `std::invalid_argument`.
status_rail_width : default ``4.0``
    Width of the coloured rail at the left edge of each row, which marks
    pinned, stale and unavailable records. Values outside 1 to 12 throw
    `std::invalid_argument`.
hover_intent_delay : default ``std::chrono::milliseconds{180}``
    How long the pointer must rest on a row before the row expands. Values
    outside 1 millisecond to 5 seconds throw `std::invalid_argument`.
scroll_offset : default ``0.0``
    The vertical scroll position, from the top of the content, in pixels
    after text scaling. It is clamped to the scrollable range. A negative
    or non-finite value throws `std::invalid_argument`. Scrolling ends any
    hover expansion; the mouse wheel scrolls by two compact rows.
content_height
    The height of all rows together, expanded rows included, in pixels
    after text scaling.
realized_count
    How many rows are realized for painting and accessibility: the rows in
    view and one more on each side.
font : default ``FontSpec{FontRole::content, 11.0, 400, false}``
    The font of the row text. Titles use it in bold and the secondary text
    two points smaller. An invalid specification throws
    `std::invalid_argument`.

Events
------
selection_changed
    Raised after `selected_id` changes, by input, by a write, or by
    `set_items` removing the selected record. It carries a
    `CorrespondenceSelectionChange` with the `previous_id` and the
    `current_id`.
pin_changed
    Raised after `pinned_id` changes, by input, by a write, or by
    `set_items` removing the pinned record. It carries a
    `CorrespondencePinChange` with the `previous_id` and the `current_id`,
    and comes before the `expansion_changed` events the new pin causes.
expansion_changed
    Raised when a record expands or collapses. It carries a
    `CorrespondenceExpansionChange` with the record's `stable_id`, whether
    it is now `expanded`, whether it is the `pinned` record, and the
    `reason`: `CorrespondenceExpansionReason::pin` for a pin change by
    input or setter, `keyboard_focus` for the focus moving, and
    `hover_intent` for the pointer. A record expanded for two reasons at
    once is reported only when it first expands and when it finally
    collapses. The reason `programmatic` is not raised.
item_activated
    Raised when the user activates a record: on the release of a
    double-click on it, on Enter, or on a semantic press of the record or
    of its ``.activate`` button. It carries the record ID. Enter and the
    ``.activate`` button do nothing for a disabled record.
context_requested
    Raised on the release of a secondary click on a record, on a secondary
    click on the background, and on a semantic `show_menu`. It carries an
    `ObjectContextRequest` with the record ID, empty for the background,
    and the position to open a menu at, in window coordinates despite its
    name `screen_position`. A background click clears the selection first.

Methods
-------
expanded
    Whether the record with the given ID is expanded: pinned, expanded by
    hover, or focused while the view has focus. An unknown ID throws
    `std::out_of_range`.
item_bounds
    The bounds of a record's row in the view's own coordinates, or no value
    for an unknown ID. Rows scrolled out of view lie outside the view's
    bounds. Convert a point with `point_to_window` to hit-test it.
on_pointer
    Handles the pointer input described under Notes. The window calls it.
on_key
    Handles the keyboard input described under Notes. The window calls it.
semantic_descriptor
    Publishes the role `list`, the accessible name and description, the
    selected ID as value, and the scroll offset as a numeric value from 0
    to the largest offset. The `increment` and `decrement` actions it lists
    are not handled.
semantic_virtual_children
    Publishes each realized row as a `list_item`, with the children
    described under Notes.
on_semantic_child_action
    Handles actions on a record: every action focuses the view and, for an
    enabled record, gives the record the focus; `select` selects it; `press`
    selects and activates it; `show_menu` selects it and raises
    `context_requested`; `expand` pins it; `collapse` removes its pin or
    hover expansion. A press of ``<id>.activate`` selects the record and
    activates it as Enter does.

See Also
--------
ObjectView : Items of one size, as icons or details columns.
ListBox : A plain list of text rows.
ContextMenu : An overlay menu for the target of `context_requested`.

Notes
-----
When the pointer moves onto a row, the row becomes hovered and, after
`hover_intent_delay`, expands; moving to another row or leaving the view
collapses that hover expansion. A primary or secondary press on a row
focuses the view, selects the row and gives it the focus. Releasing a
single primary click on the same row toggles its pin; releasing a
double-click raises `item_activated` instead, after the first click of the
pair has toggled the pin. Releasing a secondary click raises
`context_requested`. A click on the background clears the selection.

With focus, Up and Down move the focus and the selection to the previous or
next row, Home and End to the first and last, and Page Up and Page Down by
as many compact rows as fit in the view. Space pins the focused row, or
unpins it if it is already pinned, and Enter activates it. A disabled row
can be selected by a click or a key but cannot take the focus, so the
arrow keys stop at it. There is no type-ahead search.

A row shows PINNED, OFFLINE or STALE near its right end when it is pinned,
`unavailable` or `stale`. In an expanded row the information detail is
drawn only when the row is at least 650 pixels wide, scaled with the text;
a narrower row shows the heading and tags on one line.

The control publishes the accessibility role `list`. Each realized row is a
virtual `list_item` named by its title, with the metric as value and the
secondary text as description, followed by "source unavailable", "stale
metadata" or "pinned" as they apply. It has a `button` child
``<id>.activate`` named "Open" and the title, and an expanded row adds
static text children for the metric, the metadata and the excerpt, and a
`group` child for the information lane.

Examples
--------
Supply two records and pin one; the pinned record stays expanded:

.. code-block:: cpp

   std::shared_ptr<gf::CorrespondenceView> results =
       gf::make_control<gf::CorrespondenceView>(gf::StableId("search.results"));
   std::vector<gf::CorrespondenceItem> records(2);
   records[0].stable_id = "mail.1041";
   records[0].title = "Welcome aboard";
   records[0].secondary_text = "team@example.com";
   records[1].stable_id = "mail.1042";
   records[1].title = "Invoice 1042";
   records[1].secondary_text = "accounts@example.com";
   records[1].metric = "92%";
   records[1].excerpt = "Payment for invoice 1042 is due on Friday.";
   records[1].emphasis_terms = {"invoice"};
   (*results).set_items(records);
   (*results).pinned_id = "mail.1042";
   // (*results).expanded("mail.1042") is now true

Observe the pin and the expansion it causes. `pin_changed` comes first:

.. code-block:: cpp

   struct ResultsPane final : gf::Component {
       std::shared_ptr<gf::CorrespondenceView> results =
           gf::make_control<gf::CorrespondenceView>(gf::StableId("search.results"));
       std::string pinned;
       bool expanded_by_pin = false;

       ResultsPane() {
           gf::on((*results).pin_changed(), *this, &ResultsPane::on_pin);
           gf::on((*results).expansion_changed(), *this, &ResultsPane::on_expansion);
       }
       void on_pin(const gf::CorrespondencePinChange& change) {
           pinned = change.current_id;
       }
       void on_expansion(const gf::CorrespondenceExpansionChange& change) {
           expanded_by_pin = change.expanded &&
               change.reason == gf::CorrespondenceExpansionReason::pin;
       }
   };

   void example() {
       ResultsPane pane;
       std::vector<gf::CorrespondenceItem> records(2);
       records[0].stable_id = "mail.1";
       records[0].title = "Welcome aboard";
       records[1].stable_id = "mail.2";
       records[1].title = "Invoice 1042";
       (*pane.results).set_items(records);
       (*pane.results).pinned_id = "mail.2";
       // pane.pinned is "mail.2", and then pane.expanded_by_pin is true
   }
