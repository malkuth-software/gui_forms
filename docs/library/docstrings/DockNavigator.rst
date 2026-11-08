The two-list window switcher of docking: the active tool windows beside the active files.

A navigator shows two lists side by side, "Active Tool Windows" and "Active
Files", each in most-recently-active order, with one current row. A
`DockSite` opens it over itself on Ctrl+Tab (on the documents) or Alt+F7
(on the tool panels); releasing the key's modifier chooses the current row
and the site activates that panel. It can also stand on its own.

Parameters
----------
stable_id
    Identity of the control, unique within its window. A row's semantic
    identity is ``<stable_id>.tools.<panel>`` or
    ``<stable_id>.documents.<panel>``.

Properties
----------
current_list : default ``DockNavigatorList::documents``
    The list holding the current row.
current_index : default none
    The current row's index in `current_list`; none while both lists are
    empty.
current_panel : default none
    The current row's panel.
release_modifier : default ``Modifier::none``
    The modifier whose release chooses the current row: ``control`` after
    Ctrl+Tab, ``alt`` after Alt+F7, ``meta``, or none to choose only with
    Enter or a press. Any other value throws `std::invalid_argument`.

Events
------
chosen
    Raised with the current row's panel and list when it is chosen: by the
    release modifier's release, Enter, a press released on the row, or a
    row's semantic press.
cancelled
    Raised on Escape.

Methods
-------
set_entries
    Replaces both lists and makes the first row of the given list current
    (or of the other list when that one is empty). An entry without a panel
    identity throws `std::invalid_argument`, changing nothing.
entries
    One list's rows.
select
    Makes a row current; false for an index past the list's end.
step
    Moves the current row forward or back within its list, wrapping; false
    when the list is empty.
switch_list
    Moves to the other list, keeping the row index where it exists there
    (else its last row); false when the other list is empty.
choose
    Raises `chosen` with the current row; false when there is none.
cancel
    Raises `cancelled`.
entry_identity
    The semantic identity of a row.

See Also
--------
DockSite : Opens the navigator and activates the chosen panel.
DockWorkspace : Keeps the most-recently-active histories the lists follow.

Notes
-----
Keys while it has focus: Tab and Down step to the next row and Shift+Tab and
Up to the previous, wrapping within the list, with or without the release
modifier held; Left goes to the tool list and Right to the documents; Enter
chooses; Escape cancels. The release is recognized as a key-up whose
modifiers no longer hold the release modifier, which is how every host
reports it.

The site's lists: the panels that have been active, most recent first, then
the other panels shown, in tree order; panels in a collapsed stack are left
out. The navigator opens with the second row current (the panel before the
active one) or, with Shift, the last.

To accessibility the navigator is a group named "Active windows" whose value
is the current row's title, holding two lists named by their headings; each
row is a ``list_item`` with its position in the list, selected while
current, with select (makes it current) and press (chooses it).

Every part is drawn by the by-role paint path: ``region``, ``face`` (a
panel face), ``tool_heading`` and ``document_heading`` (caption text) and
``focus``; each row is the ``entry`` item system, ``entry_face`` and
``entry_label`` in the ``menu_item`` recipe family, with hot and selected.

Examples
--------
A navigator on its own, stepping with the keys:

.. code-block:: cpp

   std::shared_ptr<gf::DockNavigator> navigator =
       gf::make_control<gf::DockNavigator>(gf::StableId("switcher"));
   std::vector<gf::DockNavigatorEntry> tools{{gf::DockPanelId("output"), "Output"}};
   std::vector<gf::DockNavigatorEntry> files{{gf::DockPanelId("b.txt"), "b.txt"},
                                             {gf::DockPanelId("a.txt"), "a.txt"}};
   (*navigator).set_entries(std::move(tools), std::move(files));
   static_cast<void>((*navigator).step(1));        // a.txt is current
   static_cast<void>((*navigator).switch_list());  // Output is current
   (*navigator).release_modifier = gf::Modifier::control;
