# Retained tree

GUI.Forms is a retained toolkit. You construct controls once, arrange them in
a tree under a `Window`, and then change them by writing their properties. The toolkit
keeps the tree, its state and its last painted output between frames; nothing
is rebuilt per frame and there is no immediate-mode API. This page describes
the objects that make up that tree and who owns what.

## Controls, components and windows

`Component`
: The base for anything with a lifetime the toolkit tracks: controls,
  nonvisual providers (`ToolTip`, `ErrorProvider`, `Timer`), binding objects
  and your own handler owners. `dispose()` is synchronous and idempotent;
  `is_alive()` and `is_disposed()` expose the state. Explicit `dispose()`
  revokes owned subscriptions and work before calling `on_dispose()`.
  Natural C++ destruction revokes them in the base destructor, after derived
  members have been destroyed, and does not call virtual disposal hooks.

`Control`
: A component with geometry, visibility, enablement, focusability, a paint
  routine, a hit-test routine and a place in the tree. Every control has an
  immutable, nonempty `StableId` and a process-local `RuntimeId`.

`ComponentContainer`
: Ordered ownership of components that are not visual children of anything,
  the way a form owns its tooltip provider. Disposing the container disposes
  its components once.

`Window`
: Owns one root control and binds to its constructing thread. It holds focus,
  pointer capture, damage, update depth, the frame scheduler, the dispatcher and the metrics.
  A `Window` is a complete model without a native window; the native host
  presents it.

## Ownership

Ownership is explicit and runs one way:

- A parent owns its attached children strongly (`std::shared_ptr`).
- A child refers to its parent weakly.
- Your own `shared_ptr` to a control keeps it alive after detachment; the
  tree does not destroy what you still hold.
- Handler owners do not own the controls they observe. `on(event, owner,
  &Owner::method)` stores the connection token on the owner `Component`, so
  disposing the owner revokes the handler without touching the control.
- `on(...)` is the only recommended way to observe an event. The older
  `Event::subscribe(...)`, which returns a caller-held `SubscriptionToken`, is
  deprecated: it stays compiled for existing code that must cancel one
  connection independently, and nothing new should be written against it.

Nothing is reference-counted across the host boundary. Native adapters hold
weak handles and ask the retained window for state; a retained identity is
never cast to or returned as a native handle.

## Stable identity

`StableId` is the name a control keeps for its whole life. It must be unique
within a window, and the tree rejects a duplicate at attach time. Stable IDs
are what the instrumentation snapshot, the semantic (accessibility) tree, the
automation channel and the structured logs use to refer to controls, so a
test can say "press `commands.run`" without coordinates. `RuntimeId` is the
process-local counterpart for fast lookup and is not meaningful across runs.

The mutable `name` property is separate from `StableId` and has its own
tokenized change event.

## Tree operations

`add_child`, `remove_child`, `clear_children`, `parent()`, `children()` and
`attached()` form the tree API. The tree rejects cycles, duplicate stable
IDs, reparenting across windows, attaching a disposed child and structural
mutation from inside a lifecycle notification. Z-order is index zero topmost
for hit testing and child lookup; paint runs back to front. `bring_to_front`
and `send_to_back` adjust it.

Attachment is its own state machine, separate from the host window:
`detached → attaching → attached → detaching → detached`, with `disposed`
terminal. Attachment callbacks are transactional. The subtree is fully bound
before any `on_attached_to_window` hook observes it (parent first), fully
unbound before any `on_detached_from_window` hook runs (child first), and the
non-throwing `on_attachment_committed` hook runs only after every throwing
attach hook succeeded. A failed attach rolls the whole subtree back before
the exception escapes. A control may be reparented without creating or
recreating anything native.

`UserControl` adds a one-shot `loaded()` event and an `attachment_count()`
so composed controls can tell first presentation from re-attachment.

## State is retained, events report change

A property write stores the new value, marks the dirtiness that value
implies and emits a change event. Setting an equal value does nothing. State events
describe every actual change, whether the user or your code caused it; input
events (`clicked`, `scroll`, `item_activated`) describe input actions only and
are never synthesized by writes. That split lets you bind a `Value<T>` to two
controls without a "quiet" flag: each control's write fires its state event,
the model deduplicates equal writes, and no input event is invented.

Application-owned storage lives on the control too. `Tag` holds any
`std::any` with exact type retention and is released during disposal.

## Disposal

Disposing a control revokes its owned subscriptions and dispatcher work,
detaches it, disposes its children and releases its display chunks. Revocation
is synchronous, but it does not cancel an unowned `Window::begin_invoke`
callback merely because that callback captures the control; use
`Control::begin_invoke` for cancellation tied to the control's lifetime.
Disposing a control from inside its own
event handler is allowed; the active emission finishes its current callback
and skips the rest.

The metrics snapshot counts `disposals`, `subscriptions_connected` and
`subscriptions_disconnected`, so a leak shows up as a number rather than a
hunch. See [Lifecycle](lifecycle) for how disposal fits into the window and
host sequence.
