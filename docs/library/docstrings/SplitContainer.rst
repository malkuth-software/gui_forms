Two panels separated by a seam that the user drags to share the space between them.

A split container owns two `SplitterPanel` children and the seam between
them. With the default vertical orientation the panels sit side by side;
with `Orientation::horizontal` one sits above the other. The user resizes
them by dragging the seam or by focusing it and pressing the arrow keys, and
the container keeps each panel within its minimum and maximum. Either panel
can be collapsed, and an optional collapse tab on the seam lets the user
toggle one panel. Put your content into `first_panel` and `second_panel`.
Use a `MasterDetailView` instead for a list beside its details that turns
into one pane at a time on narrow windows.

Parameters
----------
stable_id
    Identity of the container, unique within its window. The panels and the
    seam take the identities ``<stable_id>.panel1``, ``<stable_id>.panel2``
    and ``<stable_id>.splitter``.

Properties
----------
first_panel
    The left or top panel. Add the first pane's content to it.
second_panel
    The right or bottom panel.
splitter_control
    The seam control. It is focusable, receives the pointer and keyboard
    input that resizes the panels, and draws the grip and collapse tab.
orientation : default ``Orientation::vertical``
    `Orientation::vertical` puts a vertical seam between a left and a right
    panel; `Orientation::horizontal` puts a horizontal seam between a top and
    a bottom panel.
splitter_distance
    The width, or height, of the first panel: the distance from the left or
    top edge to the seam, in logical pixels. Until a distance is set, each
    layout centers the seam, unless `fixed_panel` is `second`. A set distance
    is kept within the panels' minimums and maximums at every layout; when a
    layout has to clamp it, the clamped value becomes the distance, so the
    seam does not move back when the container grows again. It must be
    finite and nonnegative, or `std::invalid_argument` is thrown.
splitter_width : default ``3.0``
    The visible thickness of the seam, which neither panel covers. It must be
    finite and positive. Setting it also keeps the pointer target at least
    `splitter_hit_width` thick, split evenly on both sides of the seam.
splitter_hit_width : default ``9.0``
    The thickness of the seam's pointer target, never less than
    `splitter_width`. It must be finite and positive.
splitter_geometry : default ``SplitSeamGeometry{}``
    The full seam geometry: the visible thickness, the hit extents before and
    after it, the minimum hit target, and the thickness policy.
    `SplitSeamThicknessPolicy::device_pixel_hairline` draws the seam as one
    physical pixel at any display scale and snaps it to the device grid,
    while the hit target stays in logical pixels. The geometry is validated
    as a whole: the visible thickness must be above 0 and at most 64, each hit
    extent between 0 and 128, the minimum target above 0 and at most 256,
    and the hit extents, plus the visible thickness for the logical policy,
    must add up to at least the minimum target. Otherwise
    `std::invalid_argument` is thrown and nothing changes.
splitter_transition_duration : default ``90 ms``
    How long the grip takes to grow or shrink when the pointer approaches,
    presses or leaves it. Zero switches instantly. Values below zero or above
    1000 ms throw `std::invalid_argument`.
splitter_seam_snapshot
    The seam as last laid out: its state, its visible, hit and collapse-tab
    bounds, the display scale and the progress of its transition.
first_minimum : default ``25.0``
    The smallest extent the first panel is given while the space allows. It
    must be finite, nonnegative and not above `first_maximum`.
second_minimum : default ``25.0``
    The smallest extent of the second panel, with the same rules.
first_maximum : default ``std::nullopt``
    The largest extent of the first panel, or no limit. It may not be below
    `first_minimum`.
second_maximum : default ``std::nullopt``
    The largest extent of the second panel, or no limit.
first_collapsed : default ``false``
    Whether the first panel is collapsed. Collapsing hides the panel, gives
    the second panel all the space and remembers the distance; expanding
    brings the remembered distance back. If focus was inside the panel, it
    moves to the seam. `set_first_collapsed(collapsed, origin)` records who
    asked, by default `SplitCollapseOrigin::programmatic`. Collapsing both
    panels throws `std::logic_error`.
first_collapse_origin : default ``SplitCollapseOrigin::none``
    Who collapsed the first panel: the program, the user, or automatic
    accommodation of a small container. `none` while it is expanded.
second_collapsed : default ``false``
    Whether the second panel is collapsed, with the same behaviour as
    `first_collapsed`.
second_collapse_origin : default ``SplitCollapseOrigin::none``
    Who collapsed the second panel.
splitter_fixed : default ``false``
    Whether the user is prevented from resizing the panels with the pointer,
    the keyboard or accessibility actions. The collapse tab keeps working.
fixed_panel : default ``SplitFixedPanel::none``
    Which panel keeps its extent when the container is resized.
    `SplitFixedPanel::second` keeps the second panel's extent and moves the
    seam. Otherwise the seam keeps its distance once one has been set, so the
    first panel keeps its extent.
collapse_panel : default ``SplitFixedPanel::none``
    The panel that the seam's collapse tab, Enter, Space and the
    accessibility expand and collapse actions toggle. With `none` the seam
    has no collapse action.
automatic_collapse_threshold : default ``0.0``
    When positive, `collapse_panel` collapses by itself while the container's
    width (or height, for a horizontal split) is below this value, and comes
    back when the container grows to the threshold again. Only a panel that
    collapsed automatically comes back; if the user restores it while the
    container is still small, it stays open until the container next grows
    past the threshold. Setting the threshold to 0 restores an automatically
    collapsed panel. It must be finite and nonnegative.
keyboard_increment : default ``4.0``
    How far one arrow key press moves the seam; Shift multiplies it by ten.
    It must be finite and positive.

Events
------
splitter_changed
    Raised after `splitter_distance` changes, with the old and new distance
    and the reason: `SplitChangeReason::programmatic` for
    `set_splitter_distance`, `pointer` for each step of a drag and its
    release, `keyboard` for arrow keys, `semantic` for accessibility actions,
    `cancel` when Escape ends a drag and restores the starting distance, and
    `container_resize` when a layout pass moves the seam: to center it at
    first, to respect the panels' limits, or to keep a fixed second panel's
    extent. Collapsing or restoring a panel, by any means, always
    raises it with `SplitChangeReason::collapse` and the collapse origin, even
    when the distance stays the same.

Methods
-------
initialize_control_tree
    Adds the two panels and the seam as children. `make_control` calls it;
    you do not need to.

See Also
--------
SplitterPanel : The panel type that holds each pane's content.
MasterDetailView : A list and its details that become one pane on narrow windows.
GridPanel : Fixed rows and columns without a draggable seam.

Notes
-----
Create the container with `make_control`, which adds the panels and the seam;
a container constructed any other way has no children. Content belongs in
`first_panel` and `second_panel`, which place their children by Dock and
Anchor like a `Panel`.

Input: pressing the primary button on the seam's hit area starts a drag,
focuses the seam and captures the pointer, and the panels follow the pointer
until release. While the seam has focus, Left and Right (for a vertical
seam) or Up and Down (for a horizontal seam) move it by
`keyboard_increment`, Escape during a drag cancels it, and, when
`collapse_panel` is set, Enter and Space toggle that panel, as does pressing
and releasing the collapse tab. Dragging, the arrow keys and the
accessibility value actions do nothing while `splitter_fixed` is true or a
panel is collapsed.

The distance is limited to keep the first panel between `first_minimum` and
`first_maximum` and the second between `second_minimum` and
`second_maximum`. When the container is too small for both minimums, the
space is divided in proportion to them. Maximums that cannot both be met
divide it in proportion to the maximums, or in half when only one is set.

Collapsing a panel sets that panel's `visible` property to false, and
restoring it sets it back to true, so do not show or hide the panels
yourself.

The container publishes the accessibility role `split_pane`, always exposed,
with the distance as its value and the allowed range as its minimum and
maximum. It offers focus, and increment, decrement and set-value actions
while resizing is allowed. With a `collapse_panel` it also offers expand or
collapse and reports the expanded state, and its description is replaced by
a sentence that says whether the pane is collapsed.

The container measures to its requested size, or to all available space when
a requested dimension is zero. Persistent side-panel layouts in shipped
applications other than File Manager are ruled out by ADR-013; such
applications use owned dialogs for secondary tools.

Examples
--------
A folder pane beside a message pane, with minimum widths for both:

.. code-block:: cpp

   std::shared_ptr<gf::SplitContainer> split =
       gf::make_control<gf::SplitContainer>(gf::StableId("mail"));
   (*split).set_first_minimum(80.0);
   (*split).set_second_minimum(120.0);
   (*split).set_splitter_distance(140.0);

   std::shared_ptr<gf::Panel> folders =
       gf::make_control<gf::Panel>(gf::StableId("mail.folders"));
   std::shared_ptr<gf::Panel> messages =
       gf::make_control<gf::Panel>(gf::StableId("mail.messages"));
   (*folders).dock = gf::DockStyle::fill;
   (*messages).dock = gf::DockStyle::fill;
   std::shared_ptr<gf::SplitterPanel> left = (*split).first_panel();
   std::shared_ptr<gf::SplitterPanel> right = (*split).second_panel();
   (*left).add_child(folders);
   (*right).add_child(messages);

   gf::Window window(split, {500.0, 240.0});
   window.perform_layout();
   // The first panel is 140 wide. The second starts after the 3-pixel seam,
   // at x = 143, and is 357 wide.

   (*split).set_splitter_distance(490.0);
   window.perform_layout();
   // The second panel's minimum holds the distance at 377.

An inspector on the right that keeps its width when the window is resized
and can be collapsed from the seam. The owner hears every change:

.. code-block:: cpp

   struct Editor final : gf::Component {
       std::shared_ptr<gf::SplitContainer> split =
           gf::make_control<gf::SplitContainer>(gf::StableId("editor.split"));
       gf::SplitChangeReason last_reason = gf::SplitChangeReason::programmatic;
       double inspector_start = 0.0;

       Editor() {
           (*split).set_fixed_panel(gf::SplitFixedPanel::second);
           (*split).set_collapse_panel(gf::SplitFixedPanel::second);
           gf::on((*split).splitter_changed(), *this, &Editor::on_split);
       }
       void on_split(const gf::SplitChangeEvent& change) {
           last_reason = change.reason;
           inspector_start = change.new_distance;
       }
   };

   void example() {
       Editor editor;
       gf::Window window(editor.split, {400.0, 180.0});
       window.perform_layout();                     // the seam starts centered
       (*editor.split).set_second_collapsed(true);  // last_reason is collapse
       window.perform_layout();
       (*editor.split).set_second_collapsed(false); // the seam returns
   }
