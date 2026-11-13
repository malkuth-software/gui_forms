A hierarchy of labelled items that the user expands, collapses and selects.

A tree view shows nested items, such as folders, one row each, indented by
depth. Expandable rows show an expander arrow; clicking it, or pressing
Right and Left, expands and collapses the row. One item at a time is
selected, and double-clicking or pressing Enter activates it. Items are
supplied as one flat list in preorder, each with a string ID and a depth,
and are addressed by that ID. Only the rows in view are painted and
published to accessibility, and no child control is created per item. Use
a `ListBox` for a flat list and an `ObjectView` for the contents of one
level.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
items
    The items in preorder: each `TreeViewItem` is followed by its
    descendants, at one more `depth` for each level. The first item must
    have depth 0 and no item may be more than one level deeper than the
    item before it. IDs must be unique and follow the
    `StableId` segment rule, the text
    valid UTF-8, and an `image_key` at most 256 bytes, or `set_items`
    throws `std::invalid_argument`. Only items marked `expandable` can be
    collapsed; the descendants of an expandable item that is not
    `expanded` are hidden. `set_items` keeps the selected and active IDs
    if they are still present and clears them otherwise, without raising
    `selection_changed`. The `expanded` flags reflect `set_expanded` and
    user input.
selected_id : default ``""``
    The ID of the selected item, or empty when nothing is selected. Setting
    an empty ID clears the selection; an ID that is not in `items` throws
    `std::out_of_range`. The setter also selects disabled items and items
    hidden inside a collapsed parent.
active_id : default ``""``
    The ID of the keyboard-active row, where the focus rectangle is drawn
    and from which the arrow keys move. Selecting an item makes it active.
    When the tree gains focus with no active row, the selected item, or
    else the first visible row, becomes active.
item_height : default ``24.0``
    Height of each row in logical pixels, before text scaling. Values
    outside 14 to 256, or not finite, throw `std::invalid_argument`.
indentation : default ``18.0``
    Horizontal indent per level, in logical pixels. Values outside 8 to 96,
    or not finite, throw `std::invalid_argument`.
show_expanders : default ``true``
    Whether expandable rows draw an expander arrow that a click toggles.
    When false, rows still expand and collapse with the keyboard and
    `set_expanded`.
top_row : default ``0``
    The index, among the visible rows, of the first row shown. The mouse
    wheel moves it by three rows. An index outside the visible rows throws
    `std::out_of_range`.
font : default ``FontSpec{FontRole::content, 12.0, 400, false}``
    The font of the row text. An invalid specification throws
    `std::invalid_argument`.
image_list
    The `ImageList` whose images the rows show, looked up by each item's
    `image_key`. Empty by default, which shows no images. The list must be
    alive and, while the tree is in a window, belong to that window, or
    `std::invalid_argument` is thrown; attaching the tree to a different
    window later throws `std::logic_error`.

Events
------
selection_changed
    Raised after `selected_id` changes, by input or by the setter. It
    carries a `TreeSelectionChange` with the `previous_id` and the
    `current_id`.
expansion_changed
    Raised after an item expands or collapses, by input or by
    `set_expanded`. It carries a `TreeExpansionChange` with the item's
    `stable_id` and whether it is now `expanded`. When the collapsed item
    contained the selection, `selection_changed` follows, moving the
    selection to the collapsed item.
item_activated
    Raised when the user activates an item: on the release of a
    double-click on its row, on Enter, or on a semantic press. It carries
    the item ID, after the `selection_changed` the activation caused, if
    any. Disabled items are never activated.

Methods
-------
expanded
    Whether the item with the given ID is expanded. An unknown ID throws
    `std::out_of_range`.
set_expanded
    Expands or collapses the item with the given ID and raises
    `expansion_changed`. Nothing happens for an item that is not
    `expandable` or already in that state. An unknown ID throws
    `std::out_of_range`.
on_paint
    Paints the rows in view with their indentation, expander arrows and
    images, and the selection, hover and focus highlights.
on_mouse_down
    Handles the pointer input described under Notes, with the other mouse
    raisers the class overrides, then raises the event. The window calls it.
on_key_down
    Raises the event, then handles a key its subscribers left unhandled as
    described under Notes. The window calls it.
on_text_input
    Selects by typed prefix, as described under Notes. The window calls
    it.
semantic_descriptor
    Publishes the role `tree`, the accessible name and description, and as
    value the selected item's ID.
semantic_virtual_children
    Publishes the rows in view as `tree_item` children named by their text,
    with their level (1 for a root) and the `selected`, `focused` and
    `expanded` states.
on_semantic_child_action
    Handles actions on a visible row: `expand` and `collapse` change its
    expansion; `focus`, `select` and `press` focus the tree and select the
    item, and `press` also activates it.

See Also
--------
ListBox : A flat list of text rows.
ObjectView : The items of one level, with icons or details columns.
BreadcrumbTrail : The path to the current location, segment by segment.
ImageList : Shared images looked up by key.

Notes
-----
When the pointer is over a node whose text runs past the tree's edge, the
whole text shows in place over the row, as wide as it needs and painted as
the row is. Nodes have no tooltips; the reveal is not a tooltip and needs
nothing set. It ignores the pointer, and a press, scroll, selection or item
change hides it until the pointer moves again.

A primary press focuses the tree. On an expander arrow it toggles the row;
elsewhere on an enabled row it selects the row. Releasing a double-click on
the same row raises `item_activated`; a single click never activates.

With focus, Up and Down move to the previous or next visible row, Home and
End to the first and last, Page Up and Page Down by a page, and each key
selects the row it reaches. Right expands a collapsed expandable row, or
moves to the first child of an expanded one; Left collapses an expanded
row, or moves to the parent. Enter activates the active row. A disabled row cannot be
selected by input, so the keys stop at it.

Typing selects the next visible row, after the active one, whose text
starts with the typed characters. Characters typed within 850 milliseconds
of each other form one prefix; letters compare without regard to ASCII
case.

The control publishes the accessibility role `tree`: each visible row is a
virtual `tree_item` that publishes its level, counting from 1 at the roots.

Examples
--------
Supply a small hierarchy in preorder, then select and expand items by ID:

.. code-block:: cpp

   std::shared_ptr<gf::TreeView> tree =
       gf::make_control<gf::TreeView>(gf::StableId("files.folders"));
   std::vector<gf::TreeViewItem> folders;
   folders.push_back({"home", "Home", 0U, true, true});
   folders.push_back({"home.documents", "Documents", 1U, true, false});
   folders.push_back({"home.documents.taxes", "Taxes", 2U});
   folders.push_back({"home.music", "Music", 1U});
   (*tree).set_items(folders);
   (*tree).selected_id = "home.music";
   (*tree).set_expanded("home.documents", true);   // Taxes is now visible

Collapsing an item that contains the selection moves the selection to it.
An owner sees both changes, expansion first:

.. code-block:: cpp

   struct FolderPane final : gf::Component {
       std::shared_ptr<gf::TreeView> tree =
           gf::make_control<gf::TreeView>(gf::StableId("pane.folders"));
       std::string current;
       int collapses = 0;

       FolderPane() {
           gf::on((*tree).selection_changed(), *this, &FolderPane::on_selected);
           gf::on((*tree).expansion_changed(), *this, &FolderPane::on_expansion);
       }
       void on_selected(const gf::TreeSelectionChange& change) {
           current = change.current_id;
       }
       void on_expansion(const gf::TreeExpansionChange& change) {
           if (!change.expanded) ++collapses;
       }
   };

   void example() {
       FolderPane pane;
       std::vector<gf::TreeViewItem> folders;
       folders.push_back({"src", "src", 0U, true, true});
       folders.push_back({"src.core", "core", 1U});
       (*pane.tree).set_items(folders);
       (*pane.tree).selected_id = "src.core";   // pane.current is "src.core"
       (*pane.tree).set_expanded("src", false);    // collapses, then pane.current is "src"
   }
