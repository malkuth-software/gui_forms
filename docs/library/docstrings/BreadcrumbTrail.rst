A location bar that shows the path to the current place as clickable segments.

A breadcrumb trail shows a path, such as the folders from the root to the
current folder, as a row of connected segments, the last one in bold.
Activating a segment raises `segment_activated` with its ID, so the
application can navigate there. When the segments do not fit, the trail
keeps the first and the last, and as many of the middle segments nearest
the last as fit, and replaces the rest with an overflow item after the
first segment that raises `overflow_activated`. An edit button at the
right end turns the row into a text box for typing an exact path.
Segments are virtual: the editor is the trail's only child control. Use a
`TreeView` to show the whole hierarchy rather than one path.

Parameters
----------
stable_id
    Identity of the trail, unique within its window. Logs, automation and
    semantic actions address the trail by this ID. The overflow item is
    named ``<stable_id>.overflow`` and the edit button
    ``<stable_id>.edit``.
editor_stable_id
    Identity of the editing text box. Empty, the default, names it
    ``<stable_id>.editor``.

Properties
----------
font : default ``FontSpec{FontRole::control, 10.5, 400, false}``
    The font of the segments and of the editor. An invalid specification
    throws `std::invalid_argument`.
appearance : default ``BreadcrumbAppearance::plain``
    `BreadcrumbAppearance::plain` draws flat faces joined by chevron edges;
    `BreadcrumbAppearance::raised` draws raised, pointed faces whose hit
    areas follow the points.
segments
    The path, from the root to the current location. Empty by default. Each
    `BreadcrumbSegment` has an ID, text, a description of at most 4096
    bytes used as its accessible description, and an `enabled` flag. IDs
    must be unique, follow the `StableId` segment rule and differ from the
    overflow and edit IDs, or `set_segments` throws `std::invalid_argument`.
hidden_segment_ids
    The IDs of the middle segments hidden behind the overflow item, in path
    order; empty when every segment fits. Show them, for example in a menu,
    when `overflow_activated` is raised.
active_id
    The ID of the item with the keyboard focus rectangle: a segment, the
    overflow item or the edit button. When it is not on screen it moves to
    the last visible segment; before the trail has been laid out it is
    empty. Setting an ID that is none of these throws `std::out_of_range`.
editing : default ``false``
    Whether the editor replaces the segments. Setting it only shows or
    hides the editor; `begin_edit` also fills it and gives it the focus.
    Ending the edit also clears `tab_completion_available`.
editor
    The `TextBox` used for editing. It has no border, the placeholder
    "Enter an exact path" and the accessible name "Exact location path".
tab_completion_available : default ``false``
    Whether Tab in the editor raises `edit_completion_requested` instead of
    moving the focus. It can only be true while `editing`; setting it at
    other times keeps it false.
overflow_stable_id
    The ID of the overflow item, ``<stable_id>.overflow``.
edit_stable_id
    The ID of the edit button, ``<stable_id>.edit``.

Events
------
segment_activated
    Raised when the user activates an enabled segment by a click, Enter or
    Space, or a semantic press. It carries the segment ID. Activating a
    disabled segment moves `active_id` to it but raises nothing.
overflow_activated
    Raised when the user activates the overflow item, including by the
    semantic `show_menu` action.
edit_committed
    Raised when the user presses Enter in the editor. It carries the
    editor's text. The trail stays in editing mode: validate the path, and
    call `set_editing` with false when you accept it.
edit_started
    Raised by `begin_edit`, after the editor is shown and focused. It
    carries the editor's text.
edit_cancelled
    Raised when the user presses Escape in the editor, after the trail has
    left editing mode and taken back the focus.
edit_completion_requested
    Raised when the user presses Tab in the editor while
    `tab_completion_available` is true. The Tab key is consumed.

Methods
-------
initialize_control_tree
    Creates the editor. `make_control` calls it after construction; a trail
    constructed any other way cannot edit.
begin_edit
    Shows the editor with the given text, selected unless `select_all` is
    false, gives it the focus, and raises `edit_started`. Activating the
    edit button calls it with the editor's current text. A trail not made
    with `make_control` throws `std::logic_error`.
arrange
    Lays out the segments for the new width, choosing which middle segments
    overflow, and sizes the editor to the whole row.
on_pointer
    Handles the pointer input described under Notes. The window calls it.
on_key
    Handles the keyboard input described under Notes. The window calls it.
on_key_preview
    Turns Tab in the editor into `edit_completion_requested` while
    completion is available. The window calls it.
semantic_descriptor
    Publishes the role `group`, with the editor's text as value.
semantic_virtual_children
    Publishes each visible item as a `button`: segments named by their text
    and described by their description, the overflow item as "Hidden path
    segments", and the edit button as "Edit complete path". Nothing is
    published while editing.
on_semantic_child_action
    Handles focus, press and show_menu on a visible item: the trail takes
    focus and the item becomes active; press and show_menu then activate
    it.

See Also
--------
TreeView : The whole hierarchy, expandable level by level.
TextBox : The editor used for exact paths.
ContextMenu : A pop-up menu for the hidden segments.

Notes
-----
A primary press focuses the trail; releasing it over the item it pressed
activates that item. With focus, Left and Right move `active_id` along the
visible items, Home and End to the first item and the edit button, and
Enter or Space activates the active item. While editing, the trail ignores
the pointer and its own keys, and the editor handles them: Enter commits,
Escape cancels.

Segment widths follow their text, between 38 and 220 pixels, and the edit
button is always 30 pixels wide at the right end. When there are only one
or two segments and they do not fit, their faces shrink instead of
overflowing.

The trail publishes the accessibility role `group`, with one virtual
`button` per visible item. Hidden middle segments are not published; the
overflow item stands for them, and its description gives their number.

Examples
--------
Show the path to the current folder with raised segments:

.. code-block:: cpp

   std::shared_ptr<gf::BreadcrumbTrail> trail =
       gf::make_control<gf::BreadcrumbTrail>(gf::StableId("browser.location"));
   (*trail).accessible_name = "Current folder";
   (*trail).set_segments({{"root", "Home", "Go to Home"},
                          {"projects", "Projects", "Go to Projects"},
                          {"site", "Site", "Current folder"}});
   (*trail).set_appearance(gf::BreadcrumbAppearance::raised);

Navigate when the user activates a segment, and accept a typed path. In a
window, Home moves to the first segment and Enter activates it:

.. code-block:: cpp

   struct LocationBar final : gf::Component {
       std::shared_ptr<gf::BreadcrumbTrail> trail = gf::make_control<gf::BreadcrumbTrail>(
           gf::StableId("browser.location"));
       std::string target;

       LocationBar() {
           (*trail).set_segments({{"root", "Home"}, {"projects", "Projects"}});
           gf::on((*trail).segment_activated(), *this, &LocationBar::on_segment);
           gf::on((*trail).edit_committed(), *this, &LocationBar::on_path);
       }
       void on_segment(const std::string& id) { target = id; }
       void on_path(const std::string& path) {
           target = path;
           (*trail).set_editing(false);   // a commit does not end editing by itself
       }
   };

   void example() {
       LocationBar bar;
       (*bar.trail).set_requested_bounds({0.0, 0.0, 400.0, 28.0});
       gf::Window window(bar.trail, {400.0, 28.0});
       window.perform_layout();
       window.request_focus(bar.trail);
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::home});
       window.dispatch_key({gf::KeyAction::down, gf::PhysicalKey::enter});
       // bar.target is now "root"
   }
