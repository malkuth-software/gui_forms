The retained model of one window: a tree of controls with its layout, focus, input, painting and UI thread.

A `Window` owns a root control and everything that belongs to the window as
a whole: which control has focus, which holds the pointer, what needs to be
laid out and repainted, the queue of work posted to the UI thread, popups,
keyboard accelerators and the instrumentation counters. It is a complete
model on its own; a native host presents it, and `Application::run` is the
usual way to do that. Without a host the same object runs in tests and
headless tools: call `perform_layout`, feed it input with `dispatch_pointer`
and `dispatch_key`, run posted work with `drain_posted_work`, and read the
results.

Parameters
----------
root
    The root control. The window holds it for its whole life and attaches
    the tree under it. It must be alive and must not have a parent: a null
    root throws `std::invalid_argument`, a root with a parent or a disposed
    root throws `std::logic_error`, and so does a stable ID used twice in the
    tree.
client_size
    The logical size of the client area, `{0, 0}` by default. A native host
    keeps it in step with the native window through `resize`.
image_limits
    Quotas for the window's image registry. The two-argument constructor
    uses the default quotas.
resources
    An `ApplicationResources` to share with other windows: one image
    registry and one theme for all of them, so an `ImageId`, an `ImageList`
    and the theme hold in each and a control subtree moved between them
    keeps its images. The window takes the resources' current theme. Null
    resources throw `std::invalid_argument`; resources made on another
    thread throw `std::logic_error`. The other constructors make resources
    of the window's own.

Properties
----------
focus_regions
    The window's ordered list of focus regions (a ribbon, a work pane, side
    panels, a status bar) that F6 and Shift+F6 cycle through, wrapping
    around, when the focused control leaves the key unhandled. Entering a
    region focuses the control that last had focus inside it, or else its
    first Tab stop. `set_focus_regions` throws `std::invalid_argument` for a
    region that is not attached to this window, a repeated region or regions
    nested in one another, and keeps the previous list; an empty list turns
    F6 cycling off.
root
    The root control given at construction.
client_size
    The logical client size. `resize` changes it.
scale : default ``1.0``
    Device pixels per logical unit. The host sets it from the display; a
    value that is not finite and positive throws `std::invalid_argument`.
presentation_settings
    Text scale, high contrast, reduced motion and sound preferences. The
    text scale must lie between 0.5 and 4.0, and changing it closes open
    popups. Any change remeasures the tree and raises
    `presentation_changed`. Turning reduced motion on puts every motion
    value of the window at its end in the same call.
theme
    The theme every control inherits unless it has its own override.
    Setting it sets the theme of every window sharing this window's
    `resources` and raises `theme_changed` in each; a null theme throws
    `std::invalid_argument`.
resources
    The `ApplicationResources` this window uses, its own unless it was
    constructed with shared ones. Pass it to another window's constructor to
    share the image registry and the theme with that window.
active : default ``true``
    Whether the window is the active top-level window. Controls paint their
    inactive state when it is false. Setting it raises `active_changed`.
focused_control
    The control with keyboard focus, or null.
focus_cue_visible : default ``false``
    Whether focus rectangles are shown. A key press turns them on; a primary
    pointer press turns them off without moving focus.
captured_control
    The control holding pointer capture, or null. While a control holds
    capture, pointer input goes to it wherever the pointer is.
pressed_control
    The control on which the primary button went down and has not yet been
    released, or null.
accept_button
    The control activated by Enter when the focused control does not handle
    it. It must be a live button (any `ButtonBase`) in this window, or
    `std::invalid_argument` is thrown. A `Button` assigned here becomes the
    default button and is drawn as one.
cancel_button
    The control activated by Escape when the focused control does not handle
    it. Like `accept_button` it must be a live button in this window.
    Assigning a `Button` whose `dialog_result` is none gives it
    `DialogResult::cancel`.
dialog_result : default ``DialogResult::none``
    The result a dialog-style window reports. Clicking a `Button` with a
    `dialog_result` sets it. Setting it raises `dialog_result_changed`.
check_access
    Whether the calling thread is the window's UI thread: the thread that
    constructed it.
invoke_required
    The opposite of `check_access`: whether the caller must post work with
    `begin_invoke` or `invoke` instead of touching the window directly.
host_services
    The services of the attached native host, or null when none is attached.
key_map
    The keys of the named keyboard commands (`KeyCommand`), from the
    window's `key_bindings`: the host's defaults with the application's
    rebindings over them. Until a host seeds them, the defaults are the PC
    convention (`pc_key_map`: Ctrl+F4 closes a document, Shift+F10 or the
    context-menu key opens a context menu); the first host session seeds
    them with its host's (`HostCapabilities::key_map`), so under the macOS
    host Cmd+W closes a document. Controls ask it through
    `Control::effective_key_map`.
key_bindings
    The window's `KeyBindings`, which an application shares among all its
    windows (decision DK-44): `Application::run` gives every window the
    application's one set, and a floating window takes its main window's.
    A window made on its own has a set of its own. `set_key_bindings` shares
    another set; a null set throws `std::invalid_argument`.
needs_frame
    Whether painting is pending.
motion_clock
    The window's `MotionClock`: one frame request, armed only while some
    control's `MotionValue` is moving, however many are. Once every value
    has settled it schedules nothing. Tests install a `MotionTimeSource` on
    it so that motion is sampled from their own time.
occluded : default ``false``
    Whether the host reports the window as hidden from view. While occluded,
    painting and paint wake-ups are suspended, and motion values jump to
    their ends without scheduling frames.
metrics_snapshot
    A copy of the window's instrumentation counters: layout passes, flushes,
    mutations, damage, input, focus, disposals, wrong-thread rejections and
    more. `MetricsSnapshot::to_json` serializes it.
dispatcher_snapshot
    Counters of the posted-work queue: posted, invoked, cancelled and
    faulted callbacks, pending work and wake-ups.
validation_snapshot
    Counters of validation attempts, cancellations and focus moves blocked by
    validation.
semantic_generation
    A number that changes whenever the semantic (accessibility) tree may
    have changed.
tool_tips_shown : default follows ``Application::tool_tips_shown()``
    Whether the window's tooltip presenter shows tooltips when the pointer
    rests or keyboard focus arrives. `set_tool_tips_shown` takes an
    optional: unset, the window follows the program switch,
    `Application::set_tool_tips_shown`. Turning it off closes a tooltip that
    shows. `show_tool_tip` shows one regardless.
tool_tip_settings
    The window's `ToolTipSettings`: the initial, reshow and auto-pop delays,
    the maximum width, and whether hover, focus and disabled controls show
    tooltips. Every `ToolTip` of the window reads and writes them. A
    negative delay, or a width that is not finite and between 80 and 2048,
    throws `std::invalid_argument`, changing nothing.
tool_tip_visible
    Whether a tooltip shows; `tool_tip_target`, `tool_tip_item_id` and
    `tool_tip_text` name what it describes (the item ID is empty for a whole
    control).

Events
------
presentation_changed
    Raised after `presentation_settings` changes, with the new settings.
theme_changed
    Raised after `theme` changes, with the new theme.
active_changed
    Raised after `active` changes, with the new value.
dialog_result_changed
    Raised after `dialog_result` changes, with the new result.
control_availability_changed
    Raised when a control in the window is shown, hidden, enabled or
    disabled, or a parent layout collapses or reveals it. It carries the
    control's identity and its effective visibility and enablement.
pointer_capture_changed
    Raised when pointer capture moves to a control or is released. A primary
    button press captures the pointer to its target, and the release frees
    it.
focus_scope_changed
    Raised when a focus scope opens or closes.
tool_tip_visibility_changed
    Raised when a tooltip opens and when it closes, with a `ToolTipEvent`:
    the target's stable ID, the item ID, the text, whether it now shows and
    whether keyboard focus showed it.
focused_control_changed
    Raised after `request_focus` moved focus to another control (or to
    none), once the controls' own focus notifications ran, with the old and
    new controls' runtime identities and the new one's stable identity. It is
    not raised when focus is dropped because the focused control left the
    window. A handler may request focus again; that move raises its own
    event afterwards. A `DockSite` follows it to keep its workspace's active
    panel.

Methods
-------
begin_update
    Opens an update scope and returns its `UpdateScope` token. Until the
    outermost scope closes, geometry reads do not run layout; closing it
    runs one layout flush.
perform_layout
    Runs pending measure and arrange passes now, even inside an update
    scope. Each flush runs at most four passes; a layout that keeps
    invalidating itself is cut off and counted in
    `MetricsSnapshot::bounded_pass_limit_hits`.
flush
    The same as `perform_layout`.
resize
    Changes the client size and marks the whole tree for layout, which runs
    at the next flush or geometry read. A negative size throws
    `std::invalid_argument`.
find
    The attached control with the given stable ID, or null.
hit_test
    The frontmost control eligible for input at a point in window
    coordinates, searching open popups first, or null.
request_focus
    Moves keyboard focus to a control, or clears it with null. Returns true
    if the control has focus afterwards. Returns false, without moving focus,
    if the control is not focusable or not eligible for input, lies outside
    the active focus scope, or validation of the control losing focus
    refused the move.
move_focus
    Moves focus to the next Tab stop, or with `false` the previous one,
    within the active focus scope or the whole window, wrapping around.
    Returns false when there is nothing to focus.
add_focus_region
    Appends one region to `focus_regions`, as a Ribbon does for itself.
    Returns false, changing nothing, for a control that is not attached to
    this window, is already a region, or nests with one.
move_focus_region
    Moves focus as F6 does, or with `false` as Shift+F6. From outside every
    region, forward enters the first region and backward the last. Regions
    that are detached, hidden, disabled or without a Tab stop are skipped.
    Returns false, leaving focus alone, when no other region can take focus
    or a popup or dialog focus scope is active.
capture_pointer
    Routes pointer input to a control until `release_pointer`, wherever the
    pointer goes. Throws `std::logic_error` if the control is not eligible
    for input in this window.
release_pointer
    Releases pointer capture.
dispatch_pointer
    Delivers a pointer event, as the host does for mouse, pen and touch
    input, and returns whether a control handled it. See the Notes for the
    routing order.
dispatch_key
    Delivers a key event and returns whether anything handled it: the
    focused control, an accelerator, a dialog key or Tab traversal.
snapshot_control
    A picture of a control and its descendants as last laid out: their
    retained painting is replayed, every paint plane in order, into an
    offscreen framebuffer of the host's own CPU raster at the given scale,
    and the pixels are registered as one of the window's images
    (premultiplied BGRA). Areas the subtree does not paint stay
    transparent. Returns the image, its logical size, scale and pixel size,
    or none without a framebuffer painter (headless windows), during a
    paint, for a control of another window or not shown, or a scale outside
    (0, 8]. It costs one replay of the subtree and one copy of its pixels;
    the image lives until `release_control_snapshot` or the window's end.
    The docking drag's stamp uses it.
release_control_snapshot
    Removes a snapshot's image; false when it is not there.
dispatch_text
    Delivers text input to the focused control and returns whether it was
    handled.
rebind_key_command
    Binds a `KeyCommand` to a `KeyBinding` (up to two chords) in the
    window's key bindings, so in every window sharing them, over the host's
    default; an empty binding leaves the command without keys. The
    rebinding is kept when a host attaches later. A
    command outside the enumeration, or a chord with unknown modifier bits
    or modifiers without a key, throws `std::invalid_argument` and binds
    nothing.
restore_key_command
    Returns a command to the host's default.
begin_invoke
    Queues a callback to run on the UI thread in a later dispatch turn and
    returns a `DispatchOperation`; it may be called from any thread. The
    overload that takes a control cancels the work if that control is
    detached or disposed before its turn.
invoke
    Runs a callback on the UI thread and waits for it. On the UI thread it
    runs inline; from another thread it blocks until the host runs it and
    rethrows its exception. It requires a running host when called from
    another thread.
drain_posted_work
    Runs one turn of posted work on the UI thread: the callbacks that were
    queued when it started, up to the given bound. Hosts call it; tests call
    it to run posted work without a host.
shutdown_dispatcher
    Stops accepting posted work and cancels everything still queued. The
    destructor calls it.
verify_access
    Throws `std::logic_error` unless the caller is on the UI thread, naming
    the operation, and counts the rejection.
register_accelerator
    Installs a window-wide keyboard shortcut owned by a `Component`. The
    callback returns true when it handled the key. Accelerators are tried
    after the focused control declines a key, unless
    `AcceleratorOptions::before_focused_route` is set. The returned token,
    or disposing the owner, removes it.
offer_accelerators
    Offers a key press to the accelerators that wait for the focused route,
    as a key the window's own route declined; true when one took it. A
    floating docking window passes the keys it did not use to its main
    window this way (stage DK phase D4), so shortcuts work from it.
set_command_target
    Names the `CommandTarget` of one stage of the command routing chain:
    the active panel, the active document or the main window, or clears it
    with null. The window only observes the target; once its owner
    destroys it, it drops out of the chain. The focused route has no
    single target, since controls carry its targets
    (`Control::set_command_target`), and naming it throws
    `std::invalid_argument`. `command_target(stage)` returns the named
    target while it lives.
resolve_command
    Where the chain would run a command with a given identity now, without
    running it: a `CommandRouteResult` whose outcome is handled, with the
    stage and target name, or unhandled.
route_command
    Offers an invocation to the chain in order (the focused control and its
    ancestors, the active panel, the active document, the main window) and
    runs the first handler for its command identity. Exactly one handler
    runs, or none; a routing nested more than eight deep inside handlers is
    refused. `Command::execute_routed` is the usual caller.
open_popup
    Shows a detached control as an overlay owned by another control, such as
    a menu or a drop-down, and returns a `PopupToken` that closes it. The
    popup closes on its own when its owner is hidden, disabled or detached.
show_tool_tip
    `show_tool_tip(control)` shows the control's tooltip at once, below it,
    for the auto-pop delay; `show_tool_tip(control, duration)` for
    `duration`, where zero means until hidden. It shows even with the
    switches off. A control without tooltip text throws
    `std::invalid_argument`.
hide_tool_tip
    Closes the tooltip that shows and cancels one waiting to show.
begin_focus_scope
    Confines keyboard focus to a subtree, as a modal panel or menu does,
    until `end_focus_scope`. Scopes nest up to 32 deep and by default restore
    the previous focus when they close.
validate_control
    Raises `Control::validating` and, if it is not cancelled,
    `Control::validated` on a control, and returns whether it was valid.
validate_children
    Validates the descendants of a container that match the given
    constraints, and returns whether all of them were valid.
schedule_ui_timer
    Calls an owned callback on the UI thread at a frame-time interval, until
    the returned token or its owner is disposed. `Timer` is the usual way to
    get this.
paint
    Paints pending damage with a `Painter`. Native hosts call it; tests can
    call it with their own painter.
take_damage
    Runs pending layout, then returns and clears the accumulated damage of
    every paint plane.
semantic_snapshot
    Builds the semantic tree that assistive technology and automation see:
    roles, names, values, states and bounds, keyed by stable ID.
perform_semantic_action
    Performs an automation action, such as focus or press, and returns
    whether it was performed. One overload names the control by stable ID.
    Focus on a control that is not focusable itself, such as a composite
    over an inner editor, is left to the control, and is performed only if
    focus then lies inside it.
    The overload native accessibility hosts use names a published node by
    its control's runtime ID and, for a virtual child, the child's stable
    ID; a disposed or replaced control's runtime ID is refused.
metrics
    The live instrumentation counters, for hosts and renderers that record
    into them.
reset_activity_metrics
    Zeroes the activity counters, keeping the population and depth values,
    so a test can measure one workload.
publish_value
    Publishes a named number for the theme engine, such as a meter's
    `level`. A theme's expressions read the values its rule sheet names; a
    theme never writes them. A changed value damages only the parts whose
    expressions read it, within their extents
    (`MetricsSnapshot::engine_input_damaged_parts`). Returns false, changing
    nothing, for a name that is not 1 to 64 lowercase ASCII letters, digits,
    '-' and '_' starting with a letter, or a value that is not finite.
withdraw_value
    Withdraws a published value; its readers then read it as not published
    and give their expressions' fallbacks.
published_value
    The value published under a name, or none.

See Also
--------
Control : The elements of the tree a window owns.
UpdateScope : Batches changes into one layout flush.
Application : Presents windows on the native host.
MetricsSnapshot : The counters a window records.
DispatchOperation : The handle returned by `begin_invoke`.

Notes
-----
The thread that constructs a window is its UI thread, and every control in
its tree belongs to that thread. Mutations, layout, input dispatch and
painting from any other thread throw `std::logic_error` without changing
anything, and count in `MetricsSnapshot::rejected_wrong_thread_operations`.
Only `begin_invoke`, `invoke`, `check_access`, `invoke_required` and
`dispatcher_snapshot` are meant to be called from other threads. Destroy the
window on its UI thread too: its destructor detaches the tree, and doing that
from another thread terminates the program.

Destroying a window cancels its queued work and removes its accelerators,
popups, focus scopes and frame requests, then detaches the root. The root
and its children are not disposed; dispose the root yourself if nothing else
should use it.

The posted-work queue holds at most 4096 callbacks; `begin_invoke` throws
`std::length_error` beyond that, `std::invalid_argument` for an empty
callback and `std::logic_error` after `shutdown_dispatcher`. One dispatch
turn runs at most 1024 callbacks, and work posted from a callback waits for
the next turn. A callback that throws keeps its exception on its
`DispatchOperation`; later callbacks still run. Dropping the operation does
not cancel the work; call `DispatchOperation::cancel`. `invoke` from another
thread throws `std::logic_error` when no host is running, and
`DispatchCancelledError` if the work is cancelled before it runs.

A pointer event goes first to `Control::on_pointer_preview` on each control
from the root down to the target; a handler that sets `handled` stops it
there. A primary press then focuses the target if it is focusable; if that
focus change is refused, for example by validation, the press goes no
further. Otherwise the target captures the pointer. The target's
`Control::on_pointer` runs next, then `Control::on_pointer_bubble` on its
ancestors up to the root.
Releasing the primary button over the control it was pressed on calls that
control's `Control::on_activate`, which is how a button raises `clicked`.
The target is the control holding capture, or else the result of `hit_test`.
Pointer moves raise enter and leave events on the control under the pointer.

A key event goes first to accelerators registered to run before the focused
control (when no focus scope is open), then through the focused control's
route: `Control::on_key_preview` from the root down, `Control::on_key` on the
focused control, and `Control::on_key_bubble` back up. If nothing handled
it, the window tries the other accelerators; then Alt with a letter or digit
activates the matching mnemonic, Enter the `accept_button` and Escape the
`cancel_button`; finally Tab and Shift+Tab move focus.

Input dispatched while the window is painting is queued, in order, and
delivered after the paint; the dispatch functions then return true if the
event was queued.

An open `UpdateScope` defers only layout. Every change still records its
paint damage immediately, and `take_damage` and `paint` inside a scope work
with the last committed geometry.

Examples
--------
Click a button by dispatching pointer input, without a native host:

.. code-block:: cpp

   struct Dialog final : gf::Component {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("dialog"));
       std::shared_ptr<gf::Button> ok = gf::make_control<gf::Button>(gf::StableId("dialog.ok"), "OK");
       int clicks = 0;

       Dialog() {
           (*ok).set_bounds({20.0, 20.0, 80.0, 28.0});
           (*root).add_child(ok);
           gf::on((*ok).clicked(), *this, &Dialog::on_ok);
       }
       void on_ok() { ++clicks; }
   };

   void example() {
       Dialog dialog;
       gf::Window window(dialog.root, gf::Size{240.0, 120.0});
       window.perform_layout();
       gf::PointerEvent down{gf::PointerAction::down, gf::PointerButton::primary, {40.0, 30.0}};
       gf::PointerEvent up{gf::PointerAction::up, gf::PointerButton::primary, {40.0, 30.0}};
       window.dispatch_pointer(down);   // focuses and captures the button
       window.dispatch_pointer(up);     // released over it: dialog.clicks == 1
   }

Post work to the UI thread and run it. On a worker you would only call
`begin_invoke`; the host runs the turn. Here the example runs it itself:

.. code-block:: cpp

   struct ShowDone final {
       std::shared_ptr<gf::Label> status;
       void operator()() const { (*status).set_text("Done"); }
   };

   void example() {
       std::shared_ptr<gf::Panel> root = gf::make_control<gf::Panel>(gf::StableId("root"));
       std::shared_ptr<gf::Label> status =
           gf::make_control<gf::Label>(gf::StableId("status"), "Working");
       (*root).add_child(status);
       gf::Window window(root, gf::Size{320.0, 40.0});

       gf::DispatchOperation posted = window.begin_invoke(ShowDone{status});
       gf::DispatchDrainResult turn = window.drain_posted_work();
       // turn.invoked == 1, and the label now reads "Done"
       static_cast<void>(posted);
       static_cast<void>(turn);
   }
