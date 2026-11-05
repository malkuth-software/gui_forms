The base class of every visual element: geometry, visibility, input and a place in the tree.

Every stock control derives from `Control`, so the properties, events and
methods on this page apply to all of them. A control has a permanent
`StableId`, a parent and children, requested bounds that a layout turns into
arranged bounds, visibility and enablement, focus and tab order, and paint
and hit-test routines. Create controls with `make_control`, arrange them
under a root, and hand the root to a `Window`, which lays them out, routes
input to them and paints them. Derive from `Control` yourself to make a
custom control; override `measure`, `arrange` and `on_paint`, and call
`invalidate` from your setters.

Parameters
----------
stable_id
    Identity of the control, unique within its window. Logs, automation and
    semantic actions address the control by this ID.

Properties
----------
auto_size
    Whether measurement sizes the control to its content. Off by default;
    a write (`auto_size = true`) changes it and raises `auto_size_changed`.
stable_id
    The identity given at construction. It never changes.
runtime_id
    A process-local number, unique among the controls of one process run.
    Use it for fast lookup within a run, for example with `remove_child`;
    use `stable_id` for anything that must survive a restart.
name
    A mutable display name for designers and inspection. It starts as the
    stable ID's string. Setting it raises `name_changed`.
tag
    Any value the application wants to attach, stored as `std::any` with its
    exact type. The toolkit never reads it; it is released when the control
    is disposed.
parent
    The control this one is a child of, or null. A child refers to its
    parent weakly, so the parent's lifetime is decided by its own owners.
children
    The children, in paint order: the first child is painted first and is
    the backmost; the last is the frontmost. Each child is owned by this
    control.
attached
    Whether the control is in a window's tree.
attached_window
    The window whose tree contains this control, or null.
requested_bounds : default ``{0, 0, 0, 0}``
    The position and size the application asks for, relative to the parent's
    top-left corner. `left`, `top`, `width`, `height`, `right` and `bottom` read
    parts of it, and `set_bounds` changes some fields only. Size is clamped
    to `minimum_size` and `maximum_size`. Negative or non-finite values throw
    `std::invalid_argument`.
left
    `requested_bounds` x. The other edge properties read the other fields.
minimum_size : default ``{0, 0}``
    The smallest size layout may give the control. A minimum larger than a
    nonzero `maximum_size` throws `std::invalid_argument`.
maximum_size : default ``{0, 0}``
    The largest size layout may give the control. A zero dimension is
    unbounded; a nonzero dimension below `minimum_size` throws
    `std::invalid_argument`.
arranged_bounds
    Where layout actually put the control, relative to its parent. Reading it
    on an attached control first runs any pending layout, unless an
    `UpdateScope` is open; then it returns the last committed geometry, as
    `committed_arranged_bounds` always does.
committed_arranged_bounds
    The result of the last layout pass, without running pending layout.
absolute_bounds
    The arranged bounds in window coordinates. Like `arranged_bounds`, it
    runs pending layout first when attached and no update scope is open.
client_rectangle
    The control's own area in local coordinates: `{0, 0, width, height}` of
    the committed arranged size. Painting and hit testing use this space.
margin : default ``{3, 3, 3, 3}``
    Space that flow and table layout panels leave around the control. The
    base dock and anchor layout ignores it; auto-size measurement and scroll
    extents include its right and bottom parts. Values must be finite and
    nonnegative.
padding : default ``{0, 0, 0, 0}``
    Space inside the control's edges that docked children leave free.
    Anchored children keep their distances to these padded edges when the
    control resizes.
dock : default ``DockStyle::none``
    Attach the control to an edge of its parent, or `DockStyle::fill` the
    remaining space. Docked children take their space in paint order, so the
    backmost docked child takes the outermost edge.
anchor : default ``AnchorStyles::top | AnchorStyles::left``
    For an undocked control, the parent edges whose distance stays fixed
    when the parent resizes. Anchored to both left and right, the control
    stretches; anchored to neither, it keeps its centre offset.
auto_size_mode : default ``AutoSizeMode::grow_only``
    With `auto_size` on, whether measurement may shrink the control below
    its requested size (`AutoSizeMode::grow_and_shrink`) or only grow it.
layout_direction : default ``LayoutDirection::inherit``
    The inline direction the control lays its children out in;
    `LayoutDirection::inherit` takes the parent's, and a control with no
    parent is left to right. Right to left mirrors docking, anchoring and
    requested bounds, as Windows Forms' `RightToLeftLayout`: the layout is
    computed left to right and mirrored about the control's viewport, so
    `DockStyle::left` docks at the right, an anchor to the left keeps the
    distance to the right edge, a requested x is measured from the right
    edge, and padding and margins mirror too. The CSS containers keep their
    own start and end model.
auto_scroll_offset
    An offset a scrolling parent uses when it scrolls this control into
    view.
layout_transaction_state
    The depth of `suspend_layout` calls and whether layout is pending, with
    revision counters for tests.
visible : default ``true``
    Whether the application wants the control shown. A hidden control and
    its children are not laid out, painted, hit or focused. Setting it
    raises `visible_changed`.
effectively_visible
    Whether the control and all its ancestors are visible, not collapsed by
    a parent layout, and alive.
layout_collapsed
    Whether a parent layout has collapsed this control, for example a closed
    section of an expandable list. It hides the control like `visible`
    false, without changing `visible`.
enabled : default ``true``
    Whether the control accepts input. A disabled control is painted
    disabled and receives no pointer, key or text input; its children are
    disabled with it. Setting it raises `enabled_changed`.
effectively_enabled
    Whether the control and all its ancestors are enabled and alive.
eligible_for_input
    Whether input can reach the control now: attached, effectively visible
    and enabled, and neither it nor an ancestor is between `begin_init` and
    `end_init`.
focusable : default ``false``
    Whether the control can take keyboard focus. Interactive stock controls
    such as buttons and text boxes turn it on.
tab_stop : default ``true``
    Whether Tab and Shift+Tab stop at the control. A focusable control with
    `tab_stop` false can still be focused by clicking or by
    `Window::request_focus`.
tab_index : default ``0``
    The order of Tab traversal among siblings. Ties keep child order; the
    traversal visits a control before its children.
causes_validation : default ``true``
    Whether moving focus to this control validates the control losing focus,
    and whether this control raises `validating` when it is validated.
allow_drop : default ``false``
    Whether the control is offered drag-and-drop data.
hit_test_transparent : default ``false``
    Whether pointer input passes through the control to what lies beneath.
    The control is still painted, and its children are still hit tested.
classes
    The style classes a theme's class rules select (T2 cascade), in the order
    they were added, each once. A name is 1 to 64 lowercase ASCII letters,
    digits, '-' and '_', starting with a letter. `set_classes` replaces them
    and throws `std::invalid_argument`, changing nothing, for an invalid
    name; `add_class` and `remove_class` change one. A change repaints
    the control.
ceremonial : default ``false``
    Whether the application marks the control's surface ceremonial, as an
    installer's or a first run's world (T2, decision B3-1). On it and its
    descendants the theme engine's ambient effects may cover more than the
    everyday 10 % of the window; nothing else changes. A theme never sets
    it. A change repaints the control and its descendants.
on_ceremonial_surface
    Whether this control or one of its ancestors is marked `ceremonial`.
cursor
    The pointer shape over the control, or none to use the parent's.
    `effective_cursor` resolves it; the default at the root is the arrow.
effective_cursor
    The cursor in use: the `cursor` of this control or of its nearest
    ancestor that sets one, else `CursorKind::arrow`.
accessible_name
    A name for assistive technology that overrides the control's own text.
    Empty by default.
accessible_description
    A longer description for assistive technology. Empty by default.
tool_tip_text
    Short help text about the control, shown by its window's one tooltip
    presenter when the pointer rests on the control, or below the control
    when keyboard focus reaches it with the focus cue showing. Controls you
    type into show it as grey hint text in their empty field while they
    have focus instead, unless a placeholder is set. Empty by default, and
    empty text never shows. It must be valid UTF-8, or
    `std::invalid_argument` is thrown. It is bindable and published as the
    semantic field ``tool_tip``. Data items such as list rows have none.
invoke_required
    Whether the calling thread must go through `begin_invoke` or `invoke`
    to touch the control: true when the control is attached and the caller
    is not on its window's UI thread. A detached control always reports
    false.
dirty
    The invalidation flags pending on this control itself.
subtree_dirty
    The invalidation flags pending on this control or any descendant.
paint_plane : default ``PaintPlane::control``
    The compositing plane the control paints into. Panels use
    `PaintPlane::backplane`.
initializing
    Whether the control is between `begin_init` and `end_init`.
initialization_depth
    How many `begin_init` calls are still unmatched.
has_pointer_capture
    Whether the window currently routes pointer input to this control
    regardless of position. `set_pointer_capture` changes it.
styles
    The `ControlStyles` flags kept for Windows Forms compatibility;
    `set_style` changes them.
double_buffered
    Whether both buffering styles are set. GUI.Forms always presents whole
    composited frames, so the flag changes nothing visible.
effective_text_scale
    The attached window's presentation text scale, or 1 when detached.
    `effective_font` applies it to a font; custom controls use the result for
    both measuring and painting text.
effective_theme
    The theme in use: the nearest `theme_override` on this control or an
    ancestor, else the window's theme, else the default theme.
theme_override
    A theme for this control and its subtree instead of the inherited one,
    or null. Setting null throws `std::invalid_argument`; use
    `clear_theme_override`. `effective_theme` resolves the theme in use.
visual_status : default ``ControlVisualStatus::normal``
    `ControlVisualStatus::pending` or `ControlVisualStatus::invalid` change
    how stock controls paint and what state they report to assistive
    technology.

Events
------
visible_changed
    Raised after `visible` changes, with the new value. Hiding or showing an
    ancestor does not raise it on the descendants.
enabled_changed
    Raised after `enabled` changes, with the new value.
name_changed
    Raised after `name` changes, with the new name.
auto_size_changed
    Raised after `auto_size` changes, with the new value.
causes_validation_changed
    Raised after `causes_validation` changes, with the new value.
validating
    Raised when the control is validated, usually because focus is leaving
    it. Set `ControlValidationEvent::cancel` to reject the change; by default
    focus then stays where it was. Not raised when `causes_validation` is
    false.
validated
    Raised after `validating` when no handler cancelled.
tool_tip_text_changed
    Raised after `tool_tip_text` changes, with the new text.
focus_observed
    Raised with `true` when the control gains focus and `false` when it loses
    it. When focus moves, the control losing it is told first.
pointer_observed
    Raised after the control has handled a pointer event as its target,
    including the enter and leave events the window synthesizes as the
    pointer moves. It carries the event.
pointer_preview_observed
    Raised for each control on the route from the root to the target, during
    the preview pass, after that control's `on_pointer_preview`.
arranged_bounds_changed
    Raised during layout when arranging the control changes where it is
    painted. It carries the new bounds in window coordinates, widened by
    `visual_outsets`.
initialization_completed
    Raised at the end of the outermost `end_init`, after the coalesced
    state events, with the invalidation flags collected during
    initialization and whether they applied to the subtree.
help_requested
    Raised when a `HelpProvider` requests help for this control. Set
    `handled` on the event to stop the provider's own event.

Methods
-------
has_class
    Whether the control has the style class.
add_child
    Makes a control the frontmost child of this one, detaching it from its
    previous parent first. If this control is attached, the new subtree is
    attached to the window. Throws `std::logic_error` for a disposed child, a
    cycle, a child from another window or another window's root, a stable ID
    already in the window, or a call during an attach or detach notification;
    `std::invalid_argument` for a null child. Adding a control that is
    already a child does nothing.
remove_child
    Detaches the child with the given `RuntimeId` and returns it, or returns
    null if there is no such child. The child is not disposed: it lives on
    as long as someone holds it.
clear_children
    Removes every child, frontmost first, without disposing them.
child_index
    The child's z-order index, where 0 is the frontmost child, or none.
set_child_index
    Moves a child to a z-order index, where 0 is frontmost. An index past
    the end makes it the backmost. Returns false if it is not a child.
bring_to_front
    Makes the control its parent's frontmost child.
send_to_back
    Makes the control its parent's backmost child.
contains
    Whether the given control is a descendant of this one.
get_child_at_point
    The frontmost direct child whose arranged bounds contain a point in this
    control's client coordinates, optionally skipping invisible, disabled or
    hit-test-transparent children.
get_next_control
    The next or previous control in this subtree's Tab order, ignoring
    focusability.
set_bounds
    Changes the fields of `requested_bounds` selected by `BoundsSpecified`,
    all of them by default.
point_to_window
    Converts a local point to window coordinates.
point_from_window
    Converts a point in window coordinates to local coordinates.
rectangle_to_window
    Converts a local rectangle to window coordinates.
rectangle_from_window
    Converts a rectangle in window coordinates to local coordinates.
effective_font
    Scales an authored font's size and letter spacing by
    `effective_text_scale`.
clear_theme_override
    Removes `theme_override`, so the subtree inherits its theme again.
has_style
    Whether every flag of a `ControlStyles` mask is set.
suspend_layout
    Defers layout of this control's subtree until the matching
    `resume_layout`. Calls nest. While suspended, the subtree keeps its last
    committed geometry; the rest of the window still lays out.
resume_layout
    Ends one `suspend_layout`. The outermost call runs pending layout when
    `perform_layout` is true. An unmatched call does nothing.
perform_layout
    Marks the control for layout and, unless layout is suspended, runs the
    window's pending layout now.
invalidate
    `invalidate(Dirty)` records what a change invalidated, for this control
    and the path to the root; the window acts on it at its next flush or
    paint. `invalidate(Rect)` asks for a repaint of part of the client area
    only.
invalidate_subtree
    Like `invalidate(Dirty)`, but marks every descendant too, for changes
    such as a font or theme that affect how children measure and paint.
invalidate_declared
    For custom controls: forwards a nonempty mask to `invalidate`. An empty
    mask counts an undeclared mutation in `MetricsSnapshot` when attached,
    then throws `std::logic_error` in builds without `NDEBUG`, or invalidates
    the whole subtree conservatively in builds with it.
begin_init
    Starts an initialization transaction: until the matching `end_init`, the
    control's change events and invalidation are held back, and neither it
    nor its children receive input. Calls nest.
end_init
    Ends one `begin_init`. The outermost call applies the collected
    invalidation, raises each held-back event once with its latest value,
    then raises `initialization_completed`. An unmatched call throws
    `std::logic_error`.
begin_invoke
    Queues a callback to run on the window's UI thread in a later dispatch
    turn, from any thread, and returns a `DispatchOperation`. The callback is
    cancelled if the control is detached or disposed before its turn. Throws
    `std::logic_error` if the control is not attached.
invoke
    Runs a callback on the window's UI thread and waits for it. On the UI
    thread it runs inline; from another thread it blocks until the host runs
    it, then rethrows its exception, or throws `DispatchCancelledError` if
    the control is detached or the window shuts down first.
set_pointer_capture
    `true` captures the pointer to this control; `false` releases it if this
    control holds it. Capturing requires an attached control that is
    eligible for input.
set_style
    Sets or clears `ControlStyles` flags, retained for Windows Forms
    compatibility. Buffering flags never enable partial painting.
measure
    Override to report the size the control wants within the available
    size. The base version returns the requested size, or with `auto_size`
    the extent of the visible children, limited to the available size and
    then clamped to the minimum and maximum sizes.
arrange
    Override to place the control and its children. The base version stores
    the final bounds and lays out children by `dock` and `anchor`. A layout
    container that places its own children calls `arrange_self` and
    `set_child_layout` instead.
get_preferred_size
    Measures the control with a proposed size, where a zero dimension means
    unconstrained.
on_paint
    Override to draw the control in local coordinates. The window records
    what you draw and replays it in later frames until the control is
    invalidated again, so draw the whole control each time; the rectangle
    it passes is the whole local area. The base version draws nothing.
hit_test_local
    Override to refine which local points hit the control. The base version
    accepts the whole client rectangle.
on_pointer
    Override to handle a pointer event when this control is the target. Set
    `handled` to stop it bubbling. `on_pointer_preview` runs before it from
    the root down, and `on_pointer_bubble` after it from the parent up.
on_key
    Override to handle a key while this control has focus. Set `handled` to
    stop it bubbling and to keep it from accelerators, dialog keys and Tab
    traversal. `on_key_preview` and `on_key_bubble` run around it.
on_text_input
    Override to receive committed or composing text while this control has
    focus.
on_focus_changed
    Override to react when the control gains or loses focus. It runs before
    `focus_observed` is raised.
on_activate
    Called by the window when the primary button is pressed and released
    over the control. Buttons raise `clicked` from here.
semantic_descriptor
    Override to describe the control to assistive technology: role, name,
    value and states. The base version reports only `accessible_name` and
    `accessible_description`.
on_attached_to_window
    Override to acquire window resources when the control joins a window.
    Parents are notified before children, after the whole subtree is bound;
    throwing undoes the whole attachment.
on_detached_from_window
    Override to react after the control has left its window. Children are
    notified before parents. It must not throw.
require_mutable
    Throws `std::logic_error` if the control is disposed or the caller is not
    on the window's UI thread. Call it first in a custom setter.
data_bindings
    The control's `ControlBindingsCollection`, created on first use.

See Also
--------
Window : Lays out, routes input to and paints a tree of controls.
Component : The lifetime and ownership base.
StableId : The permanent identity a control is constructed with.
UserControl : A control composed of other controls, with a `loaded` event.
Panel : A plain container with a background and border.

Notes
-----
A parent owns its children; a child refers to its parent weakly. Keeping your
own `std::shared_ptr` to a control is allowed and keeps it alive after it is
removed. Create controls with `make_control<T>(StableId, ...)`, which returns
a `std::shared_ptr`; tree operations need the control to be owned by one.

A control that is attached to a window belongs to that window's UI thread.
Every setter and tree operation on it from another thread throws
`std::logic_error`, changes nothing, and counts in
`MetricsSnapshot::rejected_wrong_thread_operations`. A detached control has no
UI thread yet, so the check applies only once it is attached. Disposed
controls reject every setter with `std::logic_error`; their getters still
work.

Setters store the value, record what it invalidates, and then raise the
change event. Setting the current value again does nothing and raises
nothing. Input events such as `ButtonBase::clicked` are raised only by input
and by explicit `perform_click`, never by setters.

Hiding or disabling a control, or one of its ancestors, removes focus from
the subtree (nothing is focused afterwards), releases its pointer capture and
press, and closes overlays and focus scopes it owns. The window's
`control_availability_changed` is raised, then the control's own
`visible_changed` or `enabled_changed`.

Between `begin_init` and `end_init`, change events are coalesced by event:
each is raised once, in the order of its first change, with the latest value.
A property changed and changed back inside the transaction still raises its
event once. Use the transaction to configure a control in several steps
without handlers seeing intermediate states.

Disposing a control revokes its own connections and queued work, releases
its tag, bindings and theme override, detaches it from its window and its
parent, and then disposes its children. Disposing a control inside one of
its own event handlers is allowed. Removing a child does not dispose it.

Examples
--------
Lay out a button anchored to the bottom-right corner, and see it follow the
corner when the window grows:

.. code-block:: cpp

   std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("dialog"));
   std::shared_ptr<gf::Button> ok = gf::make_control<gf::Button>(gf::StableId("dialog.ok"), "OK");
   (*ok).set_bounds({300.0, 200.0, 80.0, 28.0});
   (*ok).anchor = gf::AnchorStyles::right | gf::AnchorStyles::bottom;
   (*root).add_child(ok);

   gf::Window window(root, gf::Size{400.0, 240.0});
   gf::Rect before = (*ok).arranged_bounds();   // {300, 200, 80, 28}
   window.resize(gf::Size{500.0, 300.0});
   gf::Rect after = (*ok).arranged_bounds();    // {400, 260, 80, 28}
   static_cast<void>(before);
   static_cast<void>(after);

A custom control with its own property. The setter rejects disposed or
wrong-thread use, ignores equal values, and asks only for a repaint:

.. code-block:: cpp

   class Swatch final : public gf::Control {
   public:
       explicit Swatch(gf::StableId stable_id) : gf::Control(std::move(stable_id)) {}

       gf::Color color() const { return color_; }
       void set_color(gf::Color color) {
           require_mutable();
           if (color == color_) return;
           color_ = color;
           invalidate(gf::Dirty::paint);   // no layout is needed
       }

       void on_paint(gf::Painter& painter, gf::Rect) override {
           painter.fill_rect(client_rectangle(), color_);
       }

   private:
       gf::Color color_ = gf::Color::rgba(0, 120, 215);
   };

   void example() {
       std::shared_ptr<Swatch> accent = gf::make_control<Swatch>(gf::StableId("theme.accent"));
       (*accent).set_bounds({0.0, 0.0, 48.0, 24.0});
       gf::Window window(accent, gf::Size{48.0, 24.0});
       window.perform_layout();
       (*accent).set_color(gf::Color::rgba(200, 40, 40));
   }
