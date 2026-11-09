A ribbon: the File tab and core tabs over a command plane of groups.

A ribbon (stage RB, phases 1 and 2b) shows a tab row that is always visible
(the File tab, then the core tabs) over the command plane of the selected
tab. The plane holds groups side by side, each with its commands, a caption
beneath and, when authored, a dialog launcher in the caption's corner.
Commands are `RibbonCommandSpec` records bound to shared `Command` objects:
buttons, toggles, split buttons and drop-down buttons at a large or small
size, hosted combo boxes, numeric spinners, check boxes and text boxes, and
in-ribbon `RibbonGallery` controls, `RibbonFontControl`s and
`RibbonColorPicker`s, which are real child controls the ribbon places. The
File tab opens the application menu (in two panes beside recent items) or,
when the ribbon has one, a `BackstageView`. As the ribbon narrows its groups shrink through their layouts in the
tab's reduction order, down to collapsed group buttons that open the group in
an overlay, and below the narrowest layout the plane scrolls. Every part is
painted through the by-role paint path.

Parameters
----------
stable_id
    Identity of the ribbon, unique within its window. Its menus are shown by
    a `ContextMenu` named ``<stable_id>.menu``; hosted controls are named
    ``<stable_id>.<command id>``, so a hosted command's ID follows the
    `StableId` segment rule.

Properties
----------
tabs
    The tabs, each with its groups and their commands: the core tabs, then
    the contextual tabs, each set's together in the sets' order.
    `set_tabs(tabs, sets)` replaces them and the tab sets, keeps each set's
    availability by ID, keeps the selection on a shown tab with the same ID
    (else the first core tab shown), keeps a
    hosted control while a command of the same ID and kind remains, and
    throws `std::invalid_argument` for a repeated ID or one that breaks the
    `StableId` segment rule (one namespace across tabs, groups and commands),
    invalid UTF-8, a button, toggle or split without a `Command`, a menu on a
    kind that has none or a split or drop-down without one, an icon-only
    command that is not small or has no image, choices on a command that is
    not a combo box, a width outside 0 to 1024, an invalid menu, a gallery
    command without an in-ribbon gallery (or with one used twice or owned
    elsewhere), two galleries in a group, an invalid layout (a collapsed
    layout that is not last or in a group of one command, sizes that grow, a
    size count other than the command count, rows outside 1 to 3, an icon-only
    command made large), a reduction step naming a group not on its tab or
    shrinking a group past its last layout or its gallery past its minimum
    (owner decision 29), an authored tab width outside 0 (exclusive) to
    `Ribbon::maximum_tab_width` (1024), or more than the development bounds.
    A refused definition leaves the previous tabs. A tab's `width`, when
    set, replaces its measured width; a tab that is not `enabled` shows in
    the disabled state and cannot be selected by the pointer, the keyboard,
    a mnemonic or accessibility, and the selection passes over it.
selected_tab
    The selected tab, or none while no tab shows. Assigning a tab past the
    end throws `std::out_of_range`; a tab not shown or disabled, or no value,
    throws `std::invalid_argument`.
tabs_activate_on_release : default ``false``
    Whether a tab is selected when the pointer is released over the tab it
    was pressed on, instead of on the press (Office's way, the default).
tab_sets
    The contextual tab sets (`RibbonTabSetSpec`: ID and caption).
application_modes : default ``1``
    The application modes shown, one bit per mode. Tabs and groups whose
    `modes` share a bit with it show. `set_application_modes` throws
    `std::invalid_argument` for zero.
minimized : default ``false``
    Whether the command plane is hidden, leaving the tab row.
document_minimum_height : default ``0``
    The minimum height, in logical units, the document area under the ribbon
    declares (owner decision 34); zero declares none.
    `set_document_minimum_height` throws `std::invalid_argument` for a
    negative or non-finite height.
plane_hidden
    Whether the document-area rule hides the command plane now.
ladder_step
    The selected tab's ladder entry the plane shows, or none while it is
    hidden or minimized.
scrolling
    Whether the plane scrolls: from when even the narrowest entry is wider
    than the plane until the plane is `Ribbon::scroll_hysteresis` (8 units)
    wider than that entry (owner decision 45).
first_visible_group
    The first group shown while the plane scrolls.
scaling_counters
    `RibbonScalingCounters`: ladders built, the last ladder's entries and
    folded steps, ladder lookups and the ribbon's own text measurements.
overlay_group
    The ID of the group whose overlay is open, or none.
group_overlay
    The overlay's content control (null before the first overlay); the overlay's
    parts are in its local coordinates.
application_text : default ``"File"``
    The File tab's text, with an optional mnemonic.
application_menu
    The File tab's menu. `set_application_menu` throws
    `std::invalid_argument` for an invalid menu, or for items other than
    commands and separators beside recent items.
recent_items
    The application menu's recent pane (`RibbonRecentItem`: ID, text and a
    description such as its folder), numbered under `recent_caption`. While
    there are recent items the File tab opens a two-pane menu: the
    application menu's commands on the left, the recent items on the right.
    `set_recent_items` throws `std::invalid_argument` for a repeated ID or one
    that breaks the `StableId` segment rule, invalid or multi-line UTF-8, more
    than `Ribbon::maximum_recent_items` (50), or an application menu holding
    other kinds than commands and separators.
recent_caption : default ``"Recent documents"``
    The recent pane's caption.
application_menu_overlay
    The two-pane menu's content control, null before its first opening.
backstage
    The `BackstageView` the File tab opens instead of the application menu,
    or null. `set_backstage` throws `std::invalid_argument` for a view that
    already has a parent.
backstage_open
    Whether Backstage is open.
menu_open
    Whether the application menu (either form) or a command's menu is open.
focus_region : default ``true``
    Whether the ribbon adds itself to its window's F6 regions when it is
    attached.
font
    The font of tab, caption and command text. A theme may give a tab's
    label its own font in each state (`ribbon::part(tab-label)`; the default
    theme's selected label is bold and a little larger); a tab is as wide as
    its label at the wider of its resting and selected fonts, so selecting a
    tab never moves the others.
realized_tabs
    The tabs' parts and states.
realized_groups
    The selected tab's groups and their parts.
realized_commands
    The selected tab's commands, their parts and states.
realized_overlay_groups
    The open overlay's group and its parts, or nothing.
realized_overlay_commands
    The open overlay's commands, their parts and states.

Events
------
command_clicked
    Raised after a button, toggle or split face runs its command, by
    pointer, keyboard, mnemonic or accessibility.
drop_down_item_invoked
    Raised when the user chooses from a split or drop-down command's menu.
dialog_launcher_clicked
    Raised when a group's dialog launcher is clicked or pressed.
selected_tab_changed
    Raised after the selected tab changes.
minimized_changed
    Raised after the command plane is hidden or shown.
application_menu_item_invoked
    Raised when the user chooses from the application menu (either form),
    after the item's command ran.
recent_item_invoked
    Raised when the user chooses a recent item.
backstage_open_changed
    Raised when Backstage opens or closes.

Methods
-------
show_application_menu
    Opens the File tab's file area: Backstage when the ribbon has one,
    otherwise the application menu over the ribbon, below the File tab (in
    two panes beside recent items). Returns false without items (or
    Backstage) or a window.
show_backstage
    Opens Backstage over everything below the tab row, with focus in it.
    Returns false without a view or a window.
close_backstage
    Closes Backstage; focus returns to where it was.
show_drop_down
    Opens a split or drop-down command's menu below it. Returns false for
    another kind, a disabled command or one not on the selected tab.
close_menus
    Closes any open menu.
show_group_overlay
    Opens a collapsed group of the selected tab in an overlay below the ribbon at
    its largest layout; its hosted controls move into the overlay while it is
    open. Returns false for a group that is not collapsed or not on the
    selected tab, or without a window.
close_group_overlay
    Closes the group overlay; focus returns to the group's button.
group_layouts
    A group's layouts, largest first: as authored, or generated. Throws
    `std::out_of_range` for a tab or group past the end.
reduction_ladder
    A tab's `RibbonLadderEntry` list from the widest to the narrowest, built
    if needed. Throws `std::out_of_range` for a tab past the end.
tab_set_availability
    A set's `RibbonTabSetAvailability`. `set_tab_set_availability` changes it
    and applies the selection rules (see Notes). Both throw
    `std::out_of_range` for an unknown set.
tab_set_accent
    The `AccentIndex` a shown set is tinted with, or none while it is hidden.
    Throws `std::out_of_range` for an unknown set.
tab_shown
    Whether a tab shows now, by its modes and its set's availability. Throws
    `std::out_of_range` for a tab past the end.
combo_box
    The `ComboBox` a combo box command holds. `numeric_up_down`, `check_box`
    and `text_box` return the other hosted controls. Each throws
    `std::out_of_range` for an ID that names no command of that kind.

See Also
--------
ToolStrip : A strip of records and hosted controls.
Command : The shared action a command carries.
ContextMenu : The overlays the ribbon opens.

Notes
-----
Clicking the selected tab minimizes the command plane, and clicking it again
shows it; clicking another tab selects it and shows the plane (owner
decision 19). There is no minimize button. The minimized ribbon's temporary
overlay comes in a later phase.

A large command shows a 32-unit icon over its label and fills the group's
height; small and hosted commands take one of three rows, stacked in
columns. Labels are shown at every size (decision 6); a small command with an
image may be authored icon-only (decision 30). A large split's face is its
icon, and its arrow is the label and glyph beneath; a small split's arrow is
the zone at its right end.

Scaling (the WPF/MFC model, owner decision 5). Each group has layouts
(`RibbonGroupLayout`: a size, large, medium, small or collapsed, the image
size of each command and the small commands per column, 1 to 3). A group
that authors none gets generated layouts: its commands' own sizes, then, one
layout per step, the rightmost run of up to three large commands made small,
then collapsed, except a group of one command, which never shrinks. Labels
never drop (decision 6). A tab's reduction ladder starts with every group at
its largest layout and its gallery at its most columns; each step shrinks one
group, first in the tab's authored `reduction_order`, then by WPF's cycle
(decision 32): from the rightmost group leftwards, one step per group,
wrapping, where a group with a gallery above its minimum gives up one column
(decision 8) before it changes layout. A step that does not narrow the tab is
folded into the next entry, so widths strictly decrease. The plane shows the
widest entry no wider than itself. Below the narrowest entry it scrolls at
any width (decision 35), and it keeps scrolling until it is 8 units wider
than that entry (decision 45; in that band the right button covers the last
group): scroll buttons at the ends move by one group, a
group that does not fit whole before the right button is not shown (the
first is clipped), and the keyboard's stop scrolls its group into view.

A collapsed group is one button the plane's height with the group's icon
(`RibbonGroupSpec::image`, else its first command image), its caption and a
chevron (decision 27). Pressing it, Enter, Space or Down opens the group at
its largest layout in an overlay below the ribbon, casting the overlay shadow; a
command run there, its launcher, a press elsewhere or Escape closes it, and it
closes when the group grows back. In the overlay the arrows and Tab move
through its commands.

The hide rule (decisions 28, 34 to 36). The height the ribbon's parent offers
it holds the ribbon and the document area beneath. The command plane, never
the tab row, hides when that area (with the plane shown) would be shorter
than `document_minimum_height`, or below zero when none is declared; a hidden
plane shows again once the area is at least the minimum plus
`Ribbon::hide_hysteresis` (8 units). Width never hides it.

Measured widths (labels, captions, tabs, galleries) are cached per tab and
rebuilt only when the tabs, a command's text or visibility, the font, the
text scale, the theme, the text provider or a gallery's column range or item
size change; a resize only looks up the ladder.

The ribbon publishes ``region``, ``tab_rail``, ``application_button``,
``application_label``, ``command_plane``, ``scroll_left``,
``scroll_left_glyph``, ``scroll_right``, ``scroll_right_glyph`` and
``focus``, and three item systems: ``tab`` (``tab``, ``tab_label``,
``tab_focus``; hot, selected, focus_visible), ``group`` (``group_region``,
``group_caption``, ``group_divider``, ``group_launcher``,
``group_launcher_glyph``, ``group_launcher_focus`` and the collapsed
``group_button``, ``group_button_icon``, ``group_button_label``,
``group_button_glyph``, ``group_button_focus``) and ``command``
(``command_face``, ``command_icon``, ``command_label``, ``command_arrow``,
``command_arrow_glyph``, ``command_focus`` and the hosted
``command_combo_box``, ``command_numeric_up_down``, ``command_check_box``,
``command_text_box``, ``command_gallery``, ``command_font_control`` and
``command_color_picker``; hot, pressed, checked, open, disabled,
focus_visible).
Command faces and tabs use the menu-item family (flat at rest), the plane
and tab row the panel family and the File tab the accent-button family.
Every part is painted under its anatomy name, an item's parts also under
the item's ID, so a theme's ``ribbon::part(...)`` rules reach each of them,
and a face a theme authors stands at rest over the built-in flat face.
The theme also sets the tab row's metrics: ``height`` on ``tab-rail`` and
``command-plane``, ``padding-inline``, ``gap`` and ``min-width`` on
``tab`` and ``application-button``, and ``margin-top`` and
``margin-bottom`` per state (a negative bottom margin runs the selected tab
into the plane, which it is painted over, so that it joins it); and a tab's
shape: ``taper`` (a folder edge on its right), ``flare`` (a lip at its foot)
and ``edge-shadow`` (a soft shadow beyond its right edge), none in the
stock themes. The tabs paint from the last to the first, the selected tab
last. A command whose enabled or checked state changes repaints only its
own bounds.

Keyboard: the ribbon is one Tab stop holding its own stops: the File tab,
the tabs, then each group's commands and launcher. Left and Right move along
the tab row (selection follows), Down enters the plane, the arrows move
through it, Up or Left from its first stop returns to the tab row, Down opens
a split or drop-down command's menu, Enter and Space activate, Escape closes
a menu or returns to the tab row, and Tab and Shift+Tab move through every
stop and then leave the ribbon. Hosted controls are ordinary Tab stops after
the ribbon. Mnemonics in the File tab's, tabs' and the selected tab's
command texts (``&Home``) open, select or activate them. The ribbon adds
itself to its window's F6 regions.

To accessibility the ribbon is a group holding the File tab (a
`menu_button` with an overlay), the tab row (a `tab_group` of the shown tabs:
core tabs positioned among the core tabs, a contextual tab among its set's,
described by the set's caption)
and the command plane (a group named after the selected tab, expanded while shown and collapsed while minimized or hidden; its expand and collapse actions restore and minimize it as a click on the selected tab does, owner decision 46)
with its groups, named by caption, and their commands: `button`,
`split_button` or `menu_button`, with positions in their group and
has-popup, and each launcher as a button. A collapsed group is a
`menu_button` with an overlay (expanded while open), and the scroll buttons are
buttons. The File tab, the tabs and the commands publish their mnemonics
as access keys. Hosted controls describe themselves as children of the
ribbon, or of the overlay while it is open.

Contextual tab sets (owner decisions 14, 22 and 25). A tab whose
`tab_set` names a set shows while the application makes the set available
or active (for example while a picture is selected); the ribbon never infers
it. A shown set is marked by a tint on its tabs' faces and on the command
plane of its selected tab, from the theme's accent ramp: each `tab` part and
the `command_plane` part carry the set's `AccentIndex`. There is no captioned
band over the set (T4-2). A newly shown set takes an accent no other shown
set has, keeping its previous one when that is free. The built-in selection
rules (F3-1): a set becoming active selects the set's tab the user chose
last, else its first tab; becoming available changes no selection; a set
leaving while one of its tabs is selected selects the core tab chosen last,
else the first core tab shown. The remembered set tab is forgotten when the
user chooses a core tab or another set's tab.

Application modes (F2-1, after the Windows Ribbon Framework). Every tab and
group belongs to the modes in its `modes` bit set (every mode by default),
and shows while one of them is in `application_modes`. A mode switch
replaces the tabs shown at once; a selected tab that no longer shows gives
way as above, and groups that leave take no room and no ladder steps.

The file area (phase 6; owner decisions 3, 23, 26). Without recent items or
Backstage the File tab opens a `ContextMenu`. With recent items it opens a
two-pane menu below the File tab, over the ribbon: Up and Down move within a
pane, Left and Right between them, Enter, Space or a click activates (after
which the menu closes), and Escape or a press away closes it. With a
`BackstageView` it opens Backstage over everything below the tab row: Escape
or a definitive command closes it, a press on the File tab closes it, a
press on a tab closes it and selects that tab, and it follows the window's
size.

Not yet: the Quick Access Toolbar, the minimized overlay, icons in the
two-pane menu, right-to-left mirroring of the cycle, two-line large labels
and ellipsized tab labels (decision 30 allows them; tabs past the row's end
are clipped).

Examples
--------
A ribbon with one tab and one group that collapses first as it narrows:

.. code-block:: cpp

   std::shared_ptr<gf::Command> paste = std::make_shared<gf::Command>("edit.paste", "&Paste");
   std::shared_ptr<gf::Ribbon> ribbon = gf::make_control<gf::Ribbon>(gf::StableId("main.ribbon"));
   gf::RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard", .dialog_launcher = true};
   clipboard.commands.push_back({.stable_id = "paste", .command = paste});
   clipboard.commands.push_back({.stable_id = "copy", .size = gf::RibbonCommandSize::small, .command = paste});
   clipboard.layouts = {{.size = gf::RibbonGroupSize::large}, {.size = gf::RibbonGroupSize::collapsed}};
   gf::RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
   home.groups.push_back(std::move(clipboard));
   home.reduction_order = {{.group = "clipboard"}};
   (*ribbon).set_tabs({std::move(home)});
   (*ribbon).document_minimum_height = 120.0;
