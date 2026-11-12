A field that shows one selected item and drops down a list of the others.

A combo box takes the space of one line and opens an overlay list when the
user clicks it or presses F4, Alt+Down or Space. Choosing a row in the
list selects it and closes the list; Up, Down, Home and End change the
selection without opening it. In the default `drop_down_list` style the
field cannot be typed into: it always shows an item from `items`, or
`placeholder_text` when nothing is selected. In the `drop_down` style the
field is an `editor` text box: the user types any text or chooses an item,
and the field can complete what is typed (`auto_complete_mode`). Use a
`ListBox` to show all choices at once or to allow several, and a
`ChoiceGroup` for a few choices that should all stay visible.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID. The overlay list is
    named after it, ``<stable_id>.overlay.list``, and its rows
    ``<stable_id>.overlay.list.item.<index>``; the drop_down style's editor is
    ``<stable_id>.editor``.

Properties
----------
items
    The choices, in display order, edited in place. Empty by default.
    Read them with `items.read()` (a span) or `items[i]`; write them with
    `items = {...}`, `items.insert(i, text)`, `items.append(text)` or
    `items.append(choices)`, `items.erase(i, n)`, `items.replace(i, text)`
    and `items.clear()`. Each edit raises `items_changed`; an assignment
    equal to the current items changes nothing. In the drop_down style the
    selection follows the editor's text. Otherwise the selected index
    follows its item: an insertion or erasure before it moves it, erasing
    it clears it, and an assignment keeps an index that still exists, even
    if a different item now has it; `selected_index_changed` and then
    `text_changed` are raised first when the index or its text changed.
    Every item must be valid UTF-8, or `std::invalid_argument` is thrown,
    and a position outside the items throws `std::out_of_range`; a refused
    edit changes nothing. The same collection is also available to
    bindings as the ``Items`` property.
selected_index : default ``std::nullopt``
    The index of the selected item, or no value when nothing is selected.
    Setting an index outside the items throws `std::out_of_range`. Also
    available to bindings as ``SelectedIndex``, with -1 for no selection.
selected_text
    The text of the selected item, or an empty view when nothing is
    selected.
drop_down_style : default ``ComboBoxStyle::drop_down_list``
    Whether the field takes text. Switching to `drop_down` adds the
    `editor` showing the selected item's text and makes the editor take the
    focus for the field; switching back removes it and keeps the selection.
    Either switch closes an open list. Windows Forms' third style, a list
    always shown under the field, is not offered.
text
    The field's text: the editor's in the drop_down style, otherwise the
    selected item's. Writing `text` in the drop_down style sets the editor's
    text and selects the item it equals, ignoring ASCII case, or clears the
    selection; in the drop_down_list style it selects the first item equal
    to the text, or clears the selection. Invalid UTF-8 throws
    `std::invalid_argument`. Also available to bindings as ``Text``.
editor
    The drop_down style's `TextBox`, or null in the drop_down_list style.
auto_complete_mode : default ``AutoCompleteMode::none``
    How the drop_down style's editor completes what is typed, as
    `TextBox::auto_complete_mode`. The setting is kept in the
    drop_down_list style, where nothing can be typed, and acts once the
    style is drop_down.
auto_complete_source : default ``AutoCompleteSource::none``
    Where the candidates come from: `list_items` uses `items`,
    `custom_source` uses `auto_complete_custom_source`, and the host
    sources offer nothing yet, as for `TextBox`.
auto_complete_custom_source
    The collection the `custom_source` source reads; a new combo box owns an
    empty one, and an assigned one is shared. A null collection throws
    `std::invalid_argument`.
drop_down_filter : default ``ComboBoxFilter::none``
    Whether typing searches the list, in the drop_down style: `prefix`
    shows the items that start with the typed text and `substring` the items
    that contain it, ignoring ASCII case, with the matched text highlighted.
    While a filter is set the editor does not auto-complete. Changing it
    closes the list; it is kept in the drop_down_list style, where it does
    nothing.
drop_down_items
    The items the open list shows, as indices into `items`, in row order;
    empty while the list is closed.
placeholder_text : default ``""``
    Text shown in a muted colour when nothing is selected; in the
    `drop_down` style, the edit field shows it while empty, and it then
    takes precedence over the field hint. It is also the
    accessible description when the control has none of its own. It must
    be valid UTF-8, or `std::invalid_argument` is thrown.
dropped_down : default ``false``
    Whether the overlay list is open. Opening has no effect, and leaves the
    property false, while the control is not attached to a window or has
    no items.
maximum_drop_down_items : default ``8``
    The most rows the open list shows before it scrolls and shows a scroll
    bar; a short window can make the list shorter still. Values outside 1
    to 64 throw `std::invalid_argument`.
    Changing it while the list is open closes and reopens the list.
drop_down_width : default ``0.0``
    Width of the overlay list in logical pixels. Zero makes the list as wide
    as the field. The list never exceeds the window width. Values outside 0
    to 4096, or not finite, throw `std::out_of_range`.
font : default ``FontSpec{FontRole::content, 12.0, 400, false}``
    The font of the field and of the overlay rows. An invalid specification
    throws `std::invalid_argument`.

Events
------
drop_down_changed
    Raised with `true` after the overlay list opens and with `false` after
    it closes, whichever way it closed.
items_changed
    Raised after each edit of `items`, after any `selected_index_changed`
    and `text_changed` the edit caused.
text_changed
    Raised after `text` changes, with the new text: in the drop_down style
    for every change of the editor's text, before any selection change it
    causes; in the drop_down_list style after `selected_index_changed`.

Methods
-------
on_pointer
    Opens the overlay list on a primary press. The window calls it.
on_key
    Handles the keyboard input described under Notes. The window calls it.
on_key_preview
    In the drop_down style, sees the editor's keys first and handles F4,
    Alt+Down, Up and Down, and Enter and Escape while the list is open. The
    window calls it.
arrange
    Places the drop_down style's editor left of the drop-down button.
semantic_descriptor
    Publishes the role `combo_box`, with `text` as value, the `expanded`
    state while the list is open, and the action `expand` or `collapse`; in
    the drop_down style also `set_value`, with the editor published through
    the field rather than as a node of its own.
on_semantic_action
    Opens the list on `expand` and closes it on `collapse`; `focus` focuses
    the field (its editor in the drop_down style), and in the drop_down
    style `set_value` sets the text. Other actions are not handled.

See Also
--------
ListBox : All choices visible at once, with optional multiple selection.
ChoiceGroup : A row of exclusive choices bound to a `Value`.

Notes
-----
In the `drop_down` style, the combo box's `tool_tip_text` shows as grey
hint text in the empty edit field while it has focus, in place of a
tooltip bubble, unless `placeholder_text` is set. Resting the pointer on it still shows the bubble.

A primary press on the field opens the list. With the field focused, F4,
Alt+Down and Space open the list, and Up, Down, Home and End select the
previous, next, first or last item directly without opening it. When
nothing is selected Up and Down count from the first item, so Down selects
the second. The drop_down_list style has no type-ahead search.

The open list is a `ListBox` in an overlay layer over the window, and it
takes the keyboard focus. Moving through it with the arrow keys selects
each row as it is reached, raising `selected_index_changed` each time.
Clicking a row, or pressing Enter, selects it and closes the list; a
semantic press on a row does the same. Escape or a press outside the list
closes it without restoring the earlier selection. Closing returns the
focus to the field. The list also closes when the control is hidden,
disabled or removed from the window.

In the drop_down style the open list does not take the focus: the editor
keeps it, so typing goes on while the list is open, and the list closes
when the field loses the focus. Up and Down choose the previous or next
item, and the editor shows each one: while the list is open they move
through its rows, and from no selection either goes to the first row;
while it is closed they step through the items as in the drop_down_list
style. F4 and Alt+Down open and close the list, and Enter and Escape close
it. Space is
typed. A press on the field's button opens or closes the list; a press in
the editor places the caret. The selection follows the text: typed text
that equals an item, ignoring ASCII case, selects it (the text stays as
typed), and other text clears the selection. While the editor's suggestion
list is open (`auto_complete_mode`), Up, Down, Enter and Escape are the
suggestion list's; F4 and Alt+Down still open the field's own list, which
closes the suggestions.

With a `drop_down_filter`, the combo box is searchable. Each edit the user
makes opens the list on the items that match the typed text, in their own
order, narrows or widens the rows as the text changes, and closes the list
when nothing matches; text set by the program (a `text` write, or the editor
showing a chosen item) does not filter. Opening the list with the button,
F4, Alt+Down or writing `dropped_down` shows every item. The rows keep their
items' identities: the selection is always an index into `items`, a row's
semantic ID ends in its item's index rather than its position, a row
pressed or chosen with the keys selects its item, and when the items change
under the open list the filter is applied again and the selection follows
the text. The matched text of each row is drawn as a highlight under its
label and published as the row's `match` part, except on the selected row,
whose own plate marks it.

Opening the list and clicking a row therefore raises `drop_down_changed`
with `true`, then `selected_index_changed` if the row was not already
selected, then `drop_down_changed` with `false`.

The control publishes the accessibility role `combo_box`; the open list
publishes the role `list` with `list_item` rows.

Examples
--------
Offer three choices with a placeholder, and select one:

.. code-block:: cpp

   std::shared_ptr<gf::ComboBox> density =
       gf::make_control<gf::ComboBox>(gf::StableId("view.density"));
   (*density).items = {"Compact", "Comfortable", "Spacious"};
   (*density).placeholder_text = "Choose a density";
   (*density).selected_index = 1U;
   // (*density).selected_text() is now "Comfortable"

React to the selection with an owned handler. Shrinking the items clears a
selection that no longer exists, and the handler sees that too:

.. code-block:: cpp

   struct DensityField final : gf::Component {
       std::shared_ptr<gf::ComboBox> combo =
           gf::make_control<gf::ComboBox>(gf::StableId("view.density"));
       std::string density;

       DensityField() {
           (*combo).items = {"Compact", "Comfortable", "Spacious"};
           gf::on((*combo).selected_index_changed(), *this, &DensityField::on_density);
       }
       void on_density(std::optional<std::size_t> index) {
           density = index ? std::string((*combo).selected_text()) : std::string();
       }
   };

   void example() {
       DensityField field;
       (*field.combo).selected_index = 2U;                 // field.density is "Spacious"
       (*field.combo).items = {"Compact", "Comfortable"};  // and now ""
   }

An editable field that completes from its items:

.. code-block:: cpp

   std::shared_ptr<gf::ComboBox> band =
       gf::make_control<gf::ComboBox>(gf::StableId("radio.band"));
   (*band).items = {"Long wave", "Medium wave", "Short wave"};
   (*band).drop_down_style = gf::ComboBoxStyle::drop_down;
   (*band).auto_complete_source = gf::AutoCompleteSource::list_items;
   (*band).auto_complete_mode = gf::AutoCompleteMode::append;
   gf::Window window(band, gf::Size{220.0, 30.0});
   window.request_focus((*band).editor());
   window.dispatch_text(gf::TextInputEvent{"m"});
   // The field shows "medium wave" with "edium wave" selected, and the
   // text equals the second item, so (*band).selected_index.read() is 1.

A searchable field that lists the stations containing what is typed:

.. code-block:: cpp

   std::shared_ptr<gf::ComboBox> station =
       gf::make_control<gf::ComboBox>(gf::StableId("trip.station"));
   (*station).items = {"Hamburg Hbf", "Köln Hbf", "Paris Nord", "Zürich HB"};
   (*station).drop_down_style = gf::ComboBoxStyle::drop_down;
   (*station).drop_down_filter = gf::ComboBoxFilter::substring;
   gf::Window window(station, gf::Size{240.0, 200.0});
   window.request_focus((*station).editor());
   window.dispatch_text(gf::TextInputEvent{"hb"});
   // The list is open on Hamburg Hbf, Köln Hbf and Zürich HB:
   // (*station).drop_down_items() is {0, 1, 3}.
