The docked part of a docking workspace in one window: tool stacks with captions around the document area, separated by seams.

A dock site shows the main site of its `DockWorkspace`'s layout. Each split
of the layout becomes a seam between its two children; each tool stack
becomes a captioned `PageView` with its tabs at the bottom when it holds two
or more panels; the document area holds the document stacks, each a
`PageView` with its tabs on top. Auto-hidden stacks wait behind side tabs
on the window's edges and slide out in a flyout. The site follows every
layout change, and the user's seam moves, tab choices, pins and caption
commands go back to the layout as transactions. Use it as the main area of
an application window.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Each stack's view is
    ``<stable_id>.stack.<node id>``, its caption buttons
    ``<view id>.menu``, ``.pin`` and ``.close``, each seam
    ``<stable_id>.seam.<split id>``, the navigator ``<stable_id>.navigator``,
    the position menu ``<stable_id>.position-menu``, each side strip
    ``<stable_id>.side.left`` (``top``, ``right``, ``bottom``) and the
    flyout ``<stable_id>.flyout``.
workspace
    The workspace to show, borrowed: it must outlive the site, or be
    destroyed first, which unbinds the site.
floating_site : default none
    The floating site to show (phase D4) instead of the main site. The
    workspace makes these sites itself, as the roots of its floating
    windows; a second site for the same floating site replaces the first.

Properties
----------
workspace : default the constructor's
    The bound workspace, or null after `release_workspace`.
floating : default ``false``
    Whether the site shows a floating site.
floating_site : default none
    The floating site shown, or none for the main site.
showing_remote_drag : default ``false``
    Whether the site shows a drag that runs in another site's window: the
    pointer is over this window (phase D4).
moving_window : default ``false``
    Whether a floating window is being moved by its caption.
keyboard_increment : default ``8``
    How far one arrow key moves a focused seam, in logical units, from 1 to
    256; outside that range throws `std::out_of_range`.
document_area_maximized : default ``false``
    Whether every tool stack is hidden so the documents fill the site
    (Ctrl+Shift+F12). Focus in a tool panel moves to the active document.
realized_stacks
    The stacks shown, collapsed ones included, in tree order, then the
    auto-hidden stacks in layout order.
collapsed_stacks
    The tool stacks hidden by shrinking or by the maximized document area
    after the last layout pass, in the order they collapsed.
minimum_size
    The smallest size that shows every stack: the panels' minimum sizes,
    the captions, the tab strips and the seams.
focused_seam : default none
    The seam the keyboard is on while the site has focus.
position_menu_open
    Whether a position menu is open.
navigator_open
    Whether the navigator is open.
navigator : default none
    The open navigator.
realized_seams
    The seams after the last layout pass, with their parts and states.
realized_stack_items
    The stacks shown after the last layout pass, with their parts and
    states, then the open flyout's stack with its caption in the site's
    coordinates.
flyout_open
    Whether a flyout is open.
flyout_panel : default none
    The panel the open flyout shows.
flyout_reveal : default ``1``
    How far the flyout has slid out, from 0 to 1; 1 while none is open.
flyout_extent : default ``0``
    The open flyout's extent across its edge, in logical units.
flyout_control : default none
    The open flyout's root control, an overlay of the window.
realized_tiles
    While dragging: the drop tiles shown, the hovered pane's five then the
    window's four edge tiles, with their parts and states.
drag_phase : default ``DockDragPhase::idle``
    Where a drag is: ``armed`` after a press on a caption, ``dragging``,
    ``settling`` while the stamp grows into its place.
drag_subject : default none
    The stack or panel being dragged.
drag_targets
    While dragging, every target the drag offers, in the keyboard's order:
    each pane's top, left, centre, right and bottom (those the
    restrictions accept), panes in tree order, then the window's top, left,
    right and bottom edges.
drag_panes
    The panes the targets were planned over: the stacks as laid out with
    the dragged subject's place closed up, or the empty document area.
drag_target : default none
    The target a drop would use now.
drag_suppressed : default ``false``
    Whether Ctrl suppresses docking.
drag_stamp
    The stamp's rectangle while dragging or settling.
drag_announcement
    What the drag announces ("Output: dock left of Properties"), also the
    name of the site's last semantic child, a `live_region` with the ID
    ``<site>.announcements`` that UI Automation announces when its name
    changes.
drag_threshold : default ``4``
    How far, in logical units along either axis, a caption press moves
    before it drags; outside 1 to 64 throws `std::out_of_range`.

Events
------
flyout_changed
    Raised after a flyout opened or closed, with the panel and the cause
    (`DockFlyoutCause`): hover, pointer, keyboard or programmatic when it
    opens; pointer_left, outside_press, focus_lost, escape, swapped, layout,
    pointer (its pin or close) or programmatic when it closes. Opened by
    click or keyboard, focus is in the panel before it is raised; closing
    with focus inside, focus has returned before it is raised.
drag_ended
    Raised once when a drag that started ends, with how it ended
    (`DockDragEnd`: dropped, escaped, capture lost, deactivated, layout
    changed, rejected, floated), its subject and, for a drop, the target,
    whether it was in another window, and the transaction's result; for a
    float, the new window's client area. Raised after the transaction. A
    floating window moved by its caption onto a target raises it too.

Methods
-------
initialize_control_tree
    Realizes the workspace's layout. `make_control` calls it; do not call it
    yourself.
release_workspace
    Unbinds the workspace; the site then shows nothing.
stack_view
    The `PageView` showing a stack, or null.
focus_seam
    Focuses the site with the keyboard on a seam; false when there is no
    such seam.
move_seam
    Moves a seam by a distance along its split, within both sides'
    minimums, as a user edit; false when it cannot move.
show_position_menu
    Opens a docked panel's position menu at its caption's menu button, or
    below a document stack's tabs. False for any other panel or a detached
    site; an unknown opening mode throws `std::invalid_argument`.
close_position_menu
    Closes the position menu.
position_command_enabled
    Whether a position command (Float, Dock, Dock as Tabbed Document, Auto
    Hide, Hide, Close, Move, Close All Documents, Close Other Documents) is
    available for a panel now.
run_position_command
    Runs a position command as the menu does; false when it is not
    available.
open_navigator
    Opens the navigator over the site on the tool panels or the documents,
    with the row after the active one current (or the last), choosing when
    the given modifier is released. False when there is nothing to list or
    the site is detached.
close_navigator
    Closes the navigator without choosing.
activate_relative_document
    Activates a document by most-recently-used order (decision DK-46): the
    one used before the active document, or with -1 the one used longest
    ago (documents never active come last, in tab order), wrapping; false
    when there is no document.
side_strip
    The `PageStrip` showing an edge's side tabs, or null while the edge has
    none.
side_panels
    The panels behind side tabs on an edge, in tab order: the auto-hidden
    stacks', then the collapsed stacks'.
open_flyout
    Opens the flyout of a panel behind a side tab, closing another first
    (cause ``swapped``); focus moves into the panel unless the cause is
    ``hover``. False, changing nothing, for a panel behind no side tab or a
    detached site.
close_flyout
    Closes the open flyout with a cause (``programmatic`` by default).
resize_flyout
    Sets the open flyout's extent, as its grip does, within the tree's
    extent, for the rest of the session; false when no flyout is open or the
    extent is not finite.
begin_keyboard_move
    Starts the keyboard move mode for a docked panel, on the first target;
    false for any other panel, while a drag runs, or with no target.
cancel_drag
    Ends a drag as Escape does; false when none runs.
seam_identity
    The semantic identity of a split's seam.
stack_identity
    The identity of a stack's view.

See Also
--------
DockWorkspace : The layout, the panels and the activation tracks.
DockNavigator : The Ctrl+Tab switcher.
PageView : Each stack's tabs and content.
DockLayout : The layout model.

Notes
-----
Captions (owner decision DK-41): a tool stack's caption shows the identity
key, the selected panel's title, the position menu button, the pin and
close. The pin is shown on root-level items only (decision DK-28): every
docked tool stack carries one, the tabs inside a stack none; it is checked
while the stack is docked, and pressing it auto-hides the whole stack.
Close hides the panel (its placeholder stays), or closes one that may not
be hidden. The caption of the stack holding the active panel is
``current``; the default theme (T4) draws it as a pale tint with a 2-unit
accent line on top, and until then today's recipes draw it.

The position menu lists Float, available while the workspace has windows
attached and the panel may float (it floats where it floated last, else
where it stands); Dock, checked while the panel stands in a docked tool
stack, available for a tool panel docked as a document (it returns to its
default side), for an auto-hidden panel (its stack returns to where it
stood) and for a floating panel (it returns where it was docked last); Dock as
Tabbed Document, for a tool panel whose descriptor allows it; Auto Hide,
checked while the panel is auto-hidden and available for a root-level
docked tool stack; Hide; Close; and Move, the keyboard move mode. A
document's menu lists Float, Close, Close All Documents (while any
document may close), Close Other Documents (while another may) and Move.
Close and the two document commands ask before documents close
(`DockWorkspace::request_close`, `close_all_documents` and
`close_other_documents`); a cancelled prompt counts as not run.

Auto-hide (phase D5; decisions DK-27, DK-28, DK-41, DK-42): the pin, or
Auto Hide, takes the whole stack (E4-1) out of the tree in one transaction
onto the window edge nearest it, keeping its extent across that edge. Each
edge holding auto-hidden panels shows a side strip, a `PageStrip` with one
tab per panel (the stacks in layout order, each stack's panels in its
order); left and right strips turn their labels, bottom to top on the left
and top to bottom on the right. The strips' bands come off the site's edges
(left and right the full height, top and bottom between them) and the tree
is laid out in what is left. A side tab opens its panel's flyout: an
overlay of the window (E3-1) that slides out from the strip over the tree
in 180 ms (``flyout_slide_duration``; plain interpolation until the motion
of stage T2, and at once under reduced motion), casts the overlay shadow,
and holds the stack's caption with an unchecked pin, the panel, and a grip
on its inner edge. The pointer resting on a tab opens it after 400 ms
(``flyout_hover_delay``), unless the workspace's `flyout_opening` is
``click`` (E1-1); a click opens it at once; Enter or Space opens the
focused tab's (each strip is a focus region, and its arrows move without
opening). Opened by click, keyboard or program, focus moves into the panel;
opened by hover, focus stays where it was until the user clicks inside. A
flyout without focus closes 500 ms (``flyout_leave_delay``) after the
pointer leaves it and its tab; one with focus stays until focus leaves it
(focus on a side strip keeps it). A press outside the flyout and the strips
closes it and goes on to what was pressed (E2-1). Escape inside closes it
and returns focus to where it was before (never a side strip), else to the
active document; Shift+Escape hides its panel. Another tab swaps the
flyout. The grip resizes it within the tree's extent for the session; the
layout is not edited. Opening, swapping, resizing and closing never edit
the layout: a group's tab shows that panel without changing the stack's
selection. The flyout's pin returns the stack to where it stood
(`DockEdit::unpin`); a pin and an unpin with nothing between leave the
saved layout byte for byte as it was.

Seams are 3 units with a grip of dots (decision DK-41). Dragging one, or
moving it with the arrow keys along its split while the site has focus,
changes the split's ratio within both sides' minimums. Tab and Shift+Tab
step between seams and leave the site past either end.

Shrinking (decision DK-23): when the site is smaller than its minimum, tool
stacks collapse in increasing collapse priority (a stack's priority is its
panels' largest; on equal priority the later stack in tree order goes
first), each only if its collapse reduces the shortfall, until the rest
fits. A collapsed stack goes behind side tabs on the window edge nearest the
place it would have in the tree and opens in a flyout like an auto-hidden
one, but the layout does not change: the stack stays docked (its flyout's
pin is checked; pressing it auto-hides the stack for good), nothing is saved
differently, and the stack returns to the tree as soon as there is room (an
open flyout of it then closes, cause ``layout``). The side strips' bands
are taken before the collapse is planned, and a collapse that needs a strip
on a further edge is planned again with that band. The maximized document
area hides every tool stack in place, without side tabs.

Keyboard (decision DK-30), while focus is in the site: F6 and Shift+F6 move
between the stacks and then the side strips, which are the window's focus
regions in that order (the program's other regions keep their places); Ctrl+Tab and
Ctrl+Shift+Tab open the navigator on the documents and Alt+F7 and
Alt+Shift+F7 on the tool panels, and releasing Ctrl or Alt activates the
current row; Ctrl+F6 and Ctrl+Shift+F6 step through the documents in
most-recently-used order (decision DK-46): the first press takes the order
as it stands, each further press with Ctrl still held steps on along it,
and releasing Ctrl (or any other key, or a press) ends the run; Escape in a
tool panel, when its content leaves it unhandled,
returns to the active document; Shift+Escape hides the active tool panel;
Ctrl+Shift+F12 toggles the maximized document area; Alt+Hyphen, Shift+F10
and the context-menu key open the active panel's position menu. A focused
`TabControl` keeps Ctrl+Tab: the stacks pass it on. Every key but F6 is a
named keyboard command (`KeyCommand`: ``cycle_forward``,
``cycle_backward``, ``next_tool_panel``, ``previous_tool_panel``,
``next_document``, ``previous_document``, ``return_to_document``,
``hide_tool_panel``, ``maximize_document_area``, ``position_menu`` and
``context_menu``), so `Window::rebind_key_command` changes it and the
navigator commits on the release of the new chord's Ctrl, Alt or Meta.

Activation: focus entering a stack or a flyout (a click, Tab, F6, a tab
chosen) makes its panel the workspace's active panel; a press on a caption, or
anywhere in a panel, activates it and moves focus to the control that last
had focus there, else its first focusable control. When the panel holding
focus is hidden, closed, moved or collapsed, focus goes to the active panel,
else the active document.

Dragging (phase D3, decisions DK-24 to DK-26): a press on a caption that
moves 4 units drags the whole stack; a tool tab dragged drags one panel at
once; a document tab first slides along its strip (reordering) and tears
off when the pointer leaves the strip's band. At the start the subject
leaves its place and the place closes up; the layout itself does not change
until the drop, so Escape leaves it byte-identical. A miniature of the
panel (its caption strip and title by role over a picture of the panel's
view, taken with `Window::snapshot_control` as the drag starts; the content
well stands in where the window cannot snapshot, as headless) shrinks from the panel's rectangle to about 100
units over 220 ms and follows the pointer; at the end it grows into the
panel's new place, or back into its old one. Over the pane under the
pointer, and only that pane, a cross of five tiles appears (fading in over
100 ms); where the pointer is in the pane, or the tile under it, selects
top, left, right or bottom (split the pane) or the centre (tab into it),
and that tile is lit; the area the subject would take is tinted. A tool
panel over a document stack's side docks beside the whole document area.
The window's edges are global targets throughout, with a tile at each
edge's middle. Targets the restrictions (dock groups, allowed placements,
the user lock) refuse are not offered and their tiles are disabled. Ctrl
held suppresses docking. A release on a target applies one transaction
and activates the moved panel; a release where nothing accepts floats the
subject into a new floating window at the pointer, at the panel's size,
when it may float (D4-1), and snaps it back otherwise, with a rejected cue
under the pointer shown only then. Escape, losing the pointer capture, the
window deactivating and a layout change by the program end a drag with the
layout unchanged.

Floating windows (phase D4; decisions DK-1, DK-6 to DK-12, DK-41): while
the workspace has windows attached, a drag follows the desktop pointer and
the topmost site under it shows the tiles, the tint and the stamp, over its
hovered pane only (decision 41, T8-3); the other sites show none. A
floating site has no document area, side strips, collapsing or window-edge
targets. Its tool stacks keep their drawn captions; the host's title bar
stays as well, since no host offers a frameless owned window yet. A
double-click on a floating caption docks the stack back. In a floating
window holding one tool stack, dragging the caption moves the window itself
with the framework's capture drag (`WindowFrameDrag`, decision 12; elsewhere
the caption drags the stack), while the window under the pointer shows its
targets; a release on one docks the stack there and the floating window
closes after the panel has appeared. Keys a floating window does not use go
to the main window's accelerators.

The keyboard move mode (Move in the position menu, or the ``move_panel``
key command, Ctrl+Alt+Shift+M): the arrows step through the current
pane's tiles, Left and Right along left, centre and right, Up and Down
along top, centre and bottom; Tab and Shift+Tab move to the next and
previous pane and then the window's edges, over exactly the targets the
pointer reaches. Enter drops; Escape cancels.

Not yet: a drawn caption in place of the host's title bar for a one-stack
floating window (no host offers frameless owned windows); a stamp outside
every window of the run (no drag image host service); a
flyout closes at once rather than sliding back; the grip's extent is not
saved with the layout; the strips leave no gap between groups; document tab
close glyphs and the overflow list arrive with the page view's document
preset.

To accessibility the site is a group named "Docking workspace" (or its
accessible name). Each stack is a ``pane`` named by its selected panel, or
"Documents", holding its tab list, its tab panel and, for a tool stack, the
caption buttons: "Window position" (a ``menu_button``), "Pinned" (a
``check_box``, checked while the stack is docked, whose press auto-hides or
docks it) and "Close". Each side strip is a tab list named "Auto-hidden
panels", vertical on the left and right; an open flyout's stack is a
``pane`` named by its panel with the same caption buttons. Each seam is a ``split_pane`` whose
value is its first side's extent, between the two sides' minimums, with
focus, increment, decrement and set_value. A document tab's close action
closes the document.

Every part is drawn by the by-role paint path. The site's parts are
``region``, ``document_area`` (the well behind the documents), ``focus``,
and, while a flyout is open, ``flyout_shadow`` (the overlay shadow),
``flyout_face`` and ``flyout_grip`` (the ``grip_dots`` mark, hot and
captured), in the site's coordinates where the flyout stands in its
slide. The ``seam`` item system has ``seam_bar`` and ``seam_grip`` (the
``grip_dots`` mark), with hot, captured, focus_visible and disabled. The
``stack`` item system has ``stack_area`` and, for a tool stack,
``caption``, ``caption_accent`` (present while current), ``caption_key``,
``caption_label``, ``caption_grip`` (the ``grip_dots`` mark in the muted
ink, between the title and the buttons, drawn in every theme; a sheet
recolours it with ``dock-site::part(caption_grip) { muted-color: ... }`` and
hides it with ``muted-color: transparent``), ``caption_menu`` and ``caption_menu_glyph`` (a
``chevron_down``), ``caption_pin`` and ``caption_pin_glyph`` (the ``pin``
mark, checked while docked), ``caption_close`` and ``caption_close_glyph``;
its states
are current and disabled, and the buttons carry their own hot, pressed,
open, checked and disabled. While dragging, the site's ``drag_stamp``,
``drag_stamp_label``, ``drop_area`` (drop_accepted) and ``drop_cue``
(drop_rejected, the ``close`` mark) are present, and the ``drop_tile``
item system has ``tile`` and ``tile_glyph`` (the ``dock_shape`` mark with
the zone's region), with selected (lit), disabled (refused) and expanding
(fading in).

Examples
--------
A workspace with a tool panel beside one document, shown in a window:

.. code-block:: cpp

   struct Views final {
       void make(gf::DockPanelCreation& creation) {
           creation.content = gf::make_control<gf::TextBox>(gf::StableId("view." + creation.panel.value));
       }
   };

   void example() {
       Views views;
       gf::DockWorkspace workspace("ide");
       const gf::Delegate<gf::DockPanelCreation&> factory =
           gf::Delegate<gf::DockPanelCreation&>::bind<Views, &Views::make>(views);
       gf::DockPanelRegistration files{};
       files.descriptor.id = gf::DockPanelId("files");
       files.descriptor.default_placement.side = gf::DockSide::left;
       files.descriptor.minimum_size = {120.0, 80.0};
       files.title = "Files";
       files.factory = factory;
       gf::DockPanelRegistration readme{};
       readme.descriptor.id = gf::DockPanelId("readme");
       readme.descriptor.kind = gf::DockPanelKind::document;
       readme.title = "README";
       readme.factory = factory;
       static_cast<void>(workspace.register_panel(files));
       static_cast<void>(workspace.register_panel(readme));
       static_cast<void>(workspace.show_panel(gf::DockPanelId("readme")));
       static_cast<void>(workspace.show_panel(gf::DockPanelId("files")));
       std::shared_ptr<gf::DockSite> site = gf::make_control<gf::DockSite>(gf::StableId("ide.site"), workspace);
       gf::Window window(site, {640.0, 400.0});
       window.perform_layout();
       // Two stacks and one seam; both panels' views were made on layout.
       (*site).set_document_area_maximized(true);
       window.perform_layout();
       // The Files stack is hidden; the document fills the site.
       (*site).set_document_area_maximized(false);
       static_cast<void>((*site).run_position_command(gf::DockPanelId("files"), gf::DockPositionCommand::auto_hide));
       window.perform_layout();
       // Files is behind a side tab on the left edge.
       static_cast<void>((*site).open_flyout(gf::DockPanelId("files")));
       // Its flyout slides out over the document, with focus inside.
   }
