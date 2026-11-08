A vertical stack of sections that the user expands and collapses.

Each section has a header button showing its label and a content control
that is shown below the header while the section is expanded. Clicking a
header, pressing Space or Enter on it, or pressing its mnemonic toggles the
section. A section's expanded state can be shared with the rest of the
application through a `Value`, and in single-open mode opening one section
closes the others, as in an accordion. Use a `TabControl` when only one
page should ever be visible and the pages need tabs, and a `CheckBox` with
your own layout for a single optional area.

Parameters
----------
id
    Identity of the control, unique within its window. Each header's ID is
    derived from it as ``<id>.header.<section id>``.

Properties
----------
items
    The sections as `SectionItem` records: a section ID, a label, the
    content control, and an optional borrowed `Value` holding the expanded
    state. `set_items` replaces the whole list. It throws
    `std::invalid_argument`, before changing anything, when a record has no
    content, a label that is not valid UTF-8, content that already belongs
    to another parent, or an ID, content or model that another record also
    uses. Calling it again from inside a notification it raised throws
    `std::logic_error`. Headers are kept for IDs that remain, so an unbound
    section keeps its state across replacements; headers for removed IDs are
    removed, and so is content that is no longer listed. A bound section
    takes its state from its `Value`. No `toggled` events are raised during
    the replacement.
single_open : default ``false``
    Whether at most one section is open at a time. Turning it on closes
    every open section after the first, raising `toggled` for each; at a
    replacement the first open section wins.

Events
------
toggled
    Raised after a section opens or closes, whether through its header,
    `set_expanded` or its bound `Value`. It carries the section's index in
    `items` and the new state. In single-open mode, opening a section first
    closes the others, so their `toggled(index, false)` events come before
    the opened section's `toggled(index, true)`.

Methods
-------
expanded
    Whether the section at an index is open. An index outside `items`
    throws `std::out_of_range`.
set_expanded
    Opens or closes the section at an index, as if the user had toggled its
    header. The bound `Value`, if any, follows. An index outside `items`
    throws `std::out_of_range`.
part
    ``part("header", index)`` returns the section's header control and
    ``part("body", index)`` its content; any other name returns null. An
    index outside `items` throws `std::out_of_range`. Checking or
    unchecking the header is the same as calling `set_expanded`.
arrange
    Stacks the sections from the top: each header is 28 logical pixels tall
    and spans the full width, and an open section's content follows it at
    the full width and the height of its requested bounds.

See Also
--------
SectionItem : The record that describes one section.
TabControl : One page at a time, chosen from a row of tabs.
ChoiceGroup : A row of exclusive choices bound to a `Value`.

Notes
-----
The headers and contents are children of the control; the contents remain
yours to keep or share. The control shows and hides each content control
itself, so do not set its `visible` yourself. Of a content control's
requested bounds only the height is used. The control does not scroll and
does not compute a preferred size from its sections, so give it bounds or a
`dock`, and leave `auto_scroll` off.

A header is a `CheckBox` drawn as a toggle button, so `&` in a label marks
its mnemonic and the header's own `clicked` is raised after `toggled`. The
control itself never takes focus. Headers get the tab indexes 0, 2, 4 and so
on in the order of `items`, but content controls keep their own tab index, 0
by default, so with the defaults Tab visits the first header, then the
focusable controls of every open section, then the other headers. To keep
each section's controls after its header, give each content control its
header's tab index plus one by writing `tab_index`.

The control is published to accessibility as a `group`. Each header is a
`button` named by its label, with the expanded state while its section is
open and an expand or collapse action.

Examples
--------
Share one section's state with a setting and keep at most one section open:

.. code-block:: cpp

   std::shared_ptr<gf::ExpandableSections> inspector =
       gf::make_control<gf::ExpandableSections>(gf::StableId("inspector"));
   std::shared_ptr<gf::Label> general =
       gf::make_control<gf::Label>(gf::StableId("inspector.general"), "Name, size and type");
   std::shared_ptr<gf::Label> sharing =
       gf::make_control<gf::Label>(gf::StableId("inspector.sharing"), "Shared with 3 people");
   (*general).set_requested_bounds({0.0, 0.0, 0.0, 60.0});   // only the height is used
   (*sharing).set_requested_bounds({0.0, 0.0, 0.0, 40.0});

   gf::Value<bool> general_open(true);
   (*inspector).single_open = true;
   (*inspector).set_items({
       {.id = 1, .label = "&General", .content = general, .expanded = &general_open},
       {.id = 2, .label = "&Sharing", .content = sharing},
   });
   (*inspector).set_expanded(1, true);   // opens Sharing and closes General
   // general_open.get() is now false

React to sections opening and closing with an owned handler:

.. code-block:: cpp

   struct Inspector final : gf::Component {
       std::shared_ptr<gf::ExpandableSections> sections =
           gf::make_control<gf::ExpandableSections>(gf::StableId("inspector"));
       int last_index = -1;
       bool last_expanded = false;

       Inspector() { gf::on((*sections).toggled(), *this, &Inspector::on_toggled); }
       void on_toggled(int index, bool expanded) {
           last_index = index;
           last_expanded = expanded;
       }
   };

   void example() {
       Inspector inspector;
       gf::Control::Ptr details =
           gf::make_control<gf::Label>(gf::StableId("inspector.details"), "Details");
       (*inspector.sections).set_items({{.id = 1, .label = "Details", .content = details}});
       (*inspector.sections).set_expanded(0, true);   // toggled(0, true)
   }
