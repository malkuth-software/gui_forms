A horizontal row of command groups that hides whole groups, in an order you set, when the row is too narrow.

A command overflow panel lays its children out left to right as command
groups, usually panels of buttons. When the row cannot fit every group, the
groups you gave a collapse priority are hidden one at a time, lowest
priority first, and an overflow actuator, such as a "More" drop-down button,
appears at the right end. The panel only decides which groups are hidden and
reports them with `layout_snapshot`; your application puts the hidden
groups' commands into the actuator's menu. Use a `ResponsiveTrackPanel`
instead for general regions that collapse, and a `CommandBar` for a single
row of commands without overflow.

Parameters
----------
stable_id
    Identity of the panel, unique within its window.

Properties
----------
group_gap : default ``0.0``
    Space between neighbouring groups that are shown, and between the last
    group and the actuator. It must be finite and between 0 and 256.
overflow_actuator
    The child that opens the overflow menu, or null. `set_overflow_actuator`
    makes a direct child the actuator: it is taken out of the row, hidden
    while no group is collapsed, and shown at the right end when one is. It
    must be a live direct child without a group spec, or
    `std::invalid_argument` is thrown.
group_spec : child property, default none
    A direct child's sizing and collapse rule, a property the panel provides
    to its children and stores for each: `(*shelf).group_spec[*group] =
    CommandOverflowGroupSpec{176.0, 176.0, 176.0, 2U};` gives a `minimum`,
    `preferred` and `maximum` width (a `maximum` of 0 means unbounded) and
    an optional `collapse_priority`. The group is given its preferred width
    and shrinks toward its minimum before any group collapses; it never
    grows past its preferred width. Writing `std::nullopt` removes the spec,
    and the child then keeps its measured width and never collapses. In a
    `.form` the value is text: the three widths, then optionally `priority`
    and the priority, as in `group-spec: "176 176 176 priority 2";`; the
    empty text is none. It throws `std::invalid_argument` if the extents are
    not finite, nonnegative and ordered, if the text has another shape, if
    the control is not a live direct child, if it is the actuator, if
    another group already has the same priority, or if 32 groups already
    have specs. The value is dropped when the child leaves.
layout_snapshot
    The result of the last layout: the available width, the gap, whether
    the actuator is shown, whether the groups overflow the row even after
    collapsing, and for each group its stable ID, measured width, allocated
    width, whether it is collapsed and its priority. The revision number
    increases only when the result changes. Reading it brings the layout up
    to date first.

Methods
-------
clear_overflow_actuator
    Turns the actuator back into an ordinary child.

See Also
--------
ResponsiveTrackPanel : General tracks that shrink and collapse in an authored order.
CommandBar : A single row of commands bound to `Command` objects.
DropDownButton : A button that asks the application to open a menu.
ContextMenu : The overlay menu an actuator can show for the hidden groups.

Notes
-----
Every visible child other than the actuator is a group, placed in child
order and stretched to the row's height minus its margins. A child without a
spec keeps its measured width plus margins and never collapses. A group's
width shrinks before groups collapse; groups then collapse in increasing
priority order, each only while the remaining minimum widths still do not
fit. Once a group collapses, the actuator's width and one gap are reserved
at the right end and the groups are fitted again into the space that is
left. Without an actuator, collapsed groups are simply hidden and
`layout_snapshot` reports overflow.

Collapse never changes a group's `visible` property. A collapsed group and a
hidden actuator are marked `layout_collapsed` and leave painting, hit
testing, focus traversal and the accessibility tree.

The group that holds the focused control is protected from collapse. When
other groups have collapsed and keeping it would still make the row overflow,
focus moves to the actuator, if it is focusable, and the focused group
collapses as well. When no other group could collapse, the focused group
stays and `layout_snapshot` reports overflow.

At most 32 visible groups are supported; a layout pass with more throws
`std::logic_error`.

With `auto_size` on, the panel measures to the sum of the groups' preferred
widths and gaps, limited by the space available, and takes the available
height. Otherwise it takes its requested size, or all available space when a
requested dimension is zero.

The panel publishes the accessibility role `group`, always exposed, with its
accessible name and description.

Examples
--------
Two groups and a "More" button. At 600 pixels both groups are shown; at 440
the Arrange group, which has the lower priority, moves into the overflow:

.. code-block:: cpp

   std::shared_ptr<gf::CommandOverflowPanel> shelf =
       gf::make_control<gf::CommandOverflowPanel>(gf::StableId("shelf"));
   std::shared_ptr<gf::Panel> selection =
       gf::make_control<gf::Panel>(gf::StableId("shelf.selection"));
   std::shared_ptr<gf::Panel> arrange =
       gf::make_control<gf::Panel>(gf::StableId("shelf.arrange"));
   std::shared_ptr<gf::DropDownButton> more = gf::make_control<gf::DropDownButton>(
       gf::StableId("shelf.more"), "More", gf::DropDownButtonMode::menu);
   (*selection).set_requested_bounds({0.0, 0.0, 176.0, 60.0});
   (*arrange).set_requested_bounds({0.0, 0.0, 306.0, 60.0});
   (*more).set_requested_bounds({0.0, 0.0, 72.0, 43.0});
   (*shelf).add_child(selection);
   (*shelf).add_child(arrange);
   (*shelf).add_child(more);
   (*shelf).group_gap = 4.0;
   (*shelf).group_spec[*selection] = gf::CommandOverflowGroupSpec{176.0, 176.0, 176.0, 2U};
   (*shelf).group_spec[*arrange] = gf::CommandOverflowGroupSpec{306.0, 306.0, 306.0, 1U};
   (*shelf).set_overflow_actuator(*more);

   gf::Window window(shelf, {600.0, 66.0});
   window.perform_layout();          // both groups shown, "More" hidden
   window.resize({440.0, 66.0});
   window.perform_layout();

   gf::CommandOverflowSnapshot snapshot = (*shelf).layout_snapshot();
   for (const gf::CommandOverflowGroupResult& group : snapshot.groups) {
       if (group.collapsed) {
           // group.stable_id is "shelf.arrange": put its commands in the
           // menu that "More" opens.
       }
   }
