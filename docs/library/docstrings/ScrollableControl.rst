The base class for controls whose children scroll inside a viewport.

ScrollableControl gives `Panel`, `ContainerControl` and the containers built
on them a two-axis viewport with built-in scroll bars. With `auto_scroll` on,
the control measures its children, shows a bar on each axis where the content
is larger than the viewport, and moves the children as the user scrolls. With
`auto_scroll` off you drive each axis yourself through `horizontal_scroll` and
`vertical_scroll`. You rarely create one directly: use a `Panel` for a
scrolling surface (`GroupBox` and `TabPage` inherit the same behaviour), and a
standalone `ScrollBar` when your own control does the scrolling.

Parameters
----------
stable_id
    Identity of the control, unique within its window.

Properties
----------
auto_scroll : default ``false``
    Whether the control sizes its scroll bars to its content. When on, a bar
    appears only on an axis where the content is larger than the viewport,
    and the manual settings (`hscroll`, `vscroll`, and each axis's visible,
    enabled, minimum and maximum) are ignored. Turning it off scrolls back to
    the origin.
auto_scroll_margin : default ``{0.0, 0.0}``
    Extra space added beyond the right-most and bottom-most child when the
    content size is computed, and kept around a control that
    `scroll_control_into_view` reveals. Writing it rejects
    a negative or non-finite size with `std::invalid_argument`.
auto_scroll_min_size : default ``{0.0, 0.0}``
    A minimum content size, so the control can scroll over an area larger
    than its children. Setting it also turns `auto_scroll` on. A negative or
    non-finite size throws `std::invalid_argument`.
auto_scroll_position
    The position in the Windows Forms convention: the getter returns negated
    offsets (scrolled 40 pixels down reads ``{0, -40}``), while the setter
    takes positive offsets, clamps negative ones to zero and scrolls without
    raising `scroll`.
scroll_position
    How far the content is scrolled, as positive offsets from the origin.
    Change it with `scroll_to`, `scroll_by` or a write of `auto_scroll_position`.
viewport_rectangle
    The area in which children are painted and hit-tested: the client
    rectangle less the space taken by visible bars.
horizontal_scroll
    The horizontal axis as `ScrollProperties`. A new axis is enabled, hidden,
    ranges from 0 to 100, and has a large change of 10 and a small change of
    1. In automatic mode the control sets the range from the content, and
    sets the large change to the viewport width and the small change to a
    tenth of it (between 1 and 48), unless you have set them yourself. The
    effective large change is at most the range plus one, and the small
    change at most the large change. The last reachable value is maximum
    minus large change plus one, so a full page stays in view.
    `ScrollProperties::set_value` throws `std::out_of_range` outside minimum
    to maximum; a negative minimum or large or small change throws
    `std::invalid_argument`.
vertical_scroll
    The vertical axis; see `horizontal_scroll`.
hscroll : default ``false``
    Whether the horizontal bar is visible. In manual mode the setter shows or
    hides it; while `auto_scroll` is on the setter is ignored.
vscroll : default ``false``
    Whether the vertical bar is visible; see `hscroll`.
last_scroll_event
    The most recent event raised by `scroll`, or empty if there has been none.
scroll_event_revision : default ``0``
    The number of events `scroll` has raised.
scroll_snapshot
    A copy of the whole scrolling state: mode, position, margin, minimum
    size, display and viewport rectangles, and both axes.

Events
------
scroll
    Raised when the user scrolls with the built-in bars or through an
    accessibility action, once for each axis that moved. It carries a
    `ScrollEvent` with the type, the orientation, and the old and new value.
    Pressing an arrow raises a small decrement or increment, pressing the
    track a large one, dragging the thumb raises
    `ScrollEventType::thumb_track` on each move, and releasing the thumb
    raises `ScrollEventType::thumb_position` with equal old and new values.
    The mouse wheel, `scroll_to`, `scroll_by` and writes of `auto_scroll_position`
    move the content without raising it, unless you pass ``notify = true``.

Methods
-------
horizontal_scroll
    The non-const overload returns the axis for changing it, for example
    ``(*panel).horizontal_scroll().set_maximum(500.0)``.
vertical_scroll
    The non-const overload of `vertical_scroll`; see `horizontal_scroll`.
get_scroll_state
    Whether one of the ``scroll_state_`` flags is set:
    `scroll_state_auto_scrolling`, `scroll_state_hscroll_visible`,
    `scroll_state_vscroll_visible`, `scroll_state_user_has_scrolled` (set by
    a scroll that raised `scroll`, cleared by `scroll_control_into_view`) and
    `scroll_state_full_drag` (set by default).
set_scroll_state
    Sets or clears flags. The first three are the same as setting
    `auto_scroll`, `hscroll` and `vscroll`; the last two are only stored.
    Zero or an unknown bit throws `std::invalid_argument`.
scroll_to
    ``scroll_to(position, type, notify)`` scrolls to the given positive
    offsets, clamped on each axis to its reachable range, and returns whether
    anything moved. An axis whose bar is hidden does not move. With
    ``notify`` true it raises `scroll` with ``type`` for each axis that
    moved. A non-finite position throws `std::invalid_argument`.
scroll_by
    ``scroll_by(delta, horizontal_type, vertical_type, notify)`` scrolls by
    an offset, with the same clamping, result and notification rules as
    `scroll_to`.
scroll_control_into_view
    Scrolls the shortest distance that brings a descendant, plus
    `auto_scroll_margin`, into the viewport, then subtracts the descendant's
    `auto_scroll_offset`. It does nothing unless `auto_scroll` is on, a bar
    is visible and the control is a descendant, and it never raises `scroll`.
arrange
    Lays out docked and anchored children as `Control` does, inside the
    viewport, then moves every anchored child by the scroll position.
arrange_scroll_viewport
    For container authors. A container that places its own children calls
    it from `arrange` with its client size and its measured content size,
    so that the viewport, the bars and the hit-test clipping stay in step.
on_scroll_position_changed
    For container authors. Called after the position changes for any
    reason, including clamping during layout. It raises no event.

See Also
--------
Panel : The usual scrolling surface.
ContainerControl : Adds focus and validation scope to the same viewport.
ScrollBar : A standalone scroll bar for content you scroll yourself.

Notes
-----
The content size is the furthest right and bottom edge of the visible
children that are not docked, using each child's requested bounds (or its
preferred size when it auto-sizes) plus its margin, `auto_scroll_margin` and
the control's padding, and at least `auto_scroll_min_size`. Children keep
their requested bounds in content coordinates while they scroll; only their
arranged bounds move, so repeated layouts never drift. Docked children are
placed inside the viewport and do not scroll.

The bars are drawn over the control, 16 logical pixels thick, in the theme's
colours, and children are painted and hit-tested only inside
`viewport_rectangle`, so nothing covers the bars. No bar is visible before
the first layout, so a `scroll_to` before then has nothing to move.

Pointer input: a primary press on an arrow scrolls by the small change and on
the track by the large change, one step per press; dragging the thumb
captures the pointer. A disabled axis ignores presses. The wheel scrolls the
control under the pointer, or reaches it by bubbling from a child that did
not use it. It moves the vertical axis first; when that cannot move, the
horizontal axis takes the horizontal delta, or the vertical delta if there is
none. A delta of at most 8 counts as lines of 48 logical pixels; a larger one
counts as pixels. An inner scrolling control that is already at its end lets
the wheel pass to the one around it. There is no keyboard scrolling.

Each visible bar is published to accessibility as a virtual child with the
role `scroll_bar`, the ID ``<stable id>.horizontal-scroll`` or
``<stable id>.vertical-scroll``, and its value and range. When enabled it
accepts increment and decrement (by the small change) and set-value
actions, which raise `scroll`.

Controls that arrange their own children, such as `Card`,
`ExpandableSections`, `ScaledGroupBox` and `TabControl`, do not run this
viewport. Leave `auto_scroll` off on them: turning it on, or setting
`auto_scroll_min_size`, leaves an empty viewport that hides their children.
To scroll the content of a tab, scroll the `TabPage`.

Examples
--------
Show a large child in a smaller panel. After layout both bars are visible,
and scrolling moves the child without changing its requested bounds:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> viewer = gf::make_control<gf::Panel>(gf::StableId("viewer"));
   std::shared_ptr<gf::Label> page =
       gf::make_control<gf::Label>(gf::StableId("viewer.page"), "A long page");
   (*page).set_requested_bounds({0.0, 0.0, 600.0, 900.0});
   (*viewer).add_child(page);
   (*viewer).auto_scroll = true;

   gf::Window window(viewer, {300.0, 200.0});
   window.perform_layout();               // both bars are now visible
   (*viewer).scroll_to({0.0, 120.0});     // scroll_position() is {0, 120}
   window.perform_layout();               // page is arranged at {0, -120, 600, 900}

Follow the user's scrolling with an owned handler. A programmatic scroll is
silent; an accessibility increment raises `scroll`:

.. code-block:: cpp

   struct Viewer final : gf::Component {
       std::shared_ptr<gf::Panel> view = gf::make_control<gf::Panel>(gf::StableId("viewer"));
       double top = 0.0;

       Viewer() { gf::on((*view).scroll(), *this, &Viewer::on_scroll); }
       void on_scroll(gf::ScrollEvent& event) {
           if (event.orientation == gf::ScrollOrientation::vertical) {
               top = event.new_value;
           }
       }
   };

   void example() {
       Viewer viewer;
       std::shared_ptr<gf::Label> page =
           gf::make_control<gf::Label>(gf::StableId("viewer.page"), "A long page");
       (*page).set_requested_bounds({0.0, 0.0, 200.0, 900.0});
       (*viewer.view).add_child(page);
       (*viewer.view).auto_scroll = true;
       gf::Window window(viewer.view, {300.0, 200.0});
       window.perform_layout();

       (*viewer.view).scroll_to({0.0, 50.0});   // silent: viewer.top is still 0
       window.perform_semantic_action("viewer.vertical-scroll",
                                      gf::SemanticAction::increment);
       // scroll was raised with a small increment of 20; viewer.top is now 70
   }
