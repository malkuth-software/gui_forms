# Input and focus

Input in GUI.Forms behaves the way a Forms application expects: focus is
explicit, Tab order is authored, mnemonics and accept/cancel buttons work,
pointer capture is synchronous, and every decision the window makes is
counted. All of it is portable; the native host only translates platform
events into `PointerEvent`, `KeyEvent` and text, and the `Window` does the
rest. Tests drive the same entry points with no host attached.

## Entry points

| Call | Carries |
|---|---|
| `Window::dispatch_pointer(PointerEvent)` | pointer ID, position, button, wheel delta, modifiers |
| `Window::dispatch_key(KeyEvent)` | `PhysicalKey`, `Modifier` set, down/up |
| `Window::dispatch_text(...)` | committed UTF-8 text |
| `Window::dispatch_drag(...)` | typed drag payload and position; returns the negotiated effect |
| `Window::perform_semantic_action(stable_id, action, value)` | an accessibility or automation request by stable ID |

Pointer, key, text and semantic dispatch return a boolean. Drag dispatch
returns a `DragDispatchResult` with `handled`, `accepted_effect`, `deferred`
and `capacity_rejected` fields. Events that arrive while a paint
lease is held are queued and delivered after the lease releases; see
[Painting and damage](painting-and-damage.md#input-during-paint).

## Pointer routing

Hit testing walks children front to back (index zero is topmost), skipping
invisible controls, disabled controls and controls marked hit-transparent;
`get_child_at_point` exposes the same lookup with independent skip options.
A control can refine its own shape with `hit_test_local`. The hit control
receives enter/move/leave, down and up through its route hooks; `pressed`
state is retained between down and up.

`Window::capture_pointer(control, pointer_id)` routes every later event for
that pointer to the control until release, regardless of position. Capture is
synchronous; a window holds one captured control/pointer-ID pair at a time.
Capturing another pair replaces it. `pointer_capture_changed` reports changes.
Disabling, hiding, detaching or entering initialization on the captured
control revokes capture and increments `capture_revocations`. On macOS the
host maps capture onto the native implicit drag sequence rather than a global
event tap.

Nonvisual providers observe pointer traffic without subclassing a target
through the tokenized `pointer_observed` event. That is how `ToolTip` knows
when to appear.

Every control also raises the Windows Forms mouse events (`MouseDown`,
`Click`, `MouseClick`, `DoubleClick`, `MouseUp`, `MouseHover` and the rest)
and context-menu requests. The host supplies the click count. See
[Mouse events and context requests](mouse-events.md) for their order, the
click styles and what each host offers.

## Keyboard routing

A key event goes first to the focused control's route hook, then up through
its ancestors, then to the window's command layer: accelerator gestures
bound with `Command::bind_shortcut(window, KeyGesture)`, mnemonics, Tab
traversal, and the accept and cancel targets. Editors and open menus get
first refusal, so a text box keeps its arrow keys and a menu keeps Escape.

Mnemonics use the ampersand contract: `&File` names `F`, `&&` displays one
ampersand, and matching preserves Unicode identity with ASCII case folding.
`process_mnemonic` snapshots the eligible controls in stable tab order before
any callback runs, confines the search to the active focus scope, and cycles
duplicate winners after the previous match rather than always choosing the
first. `DialogKeySnapshot` reports attempts, handled commands, candidates,
collisions and completed cycles.

## Key events

Controls and windows raise key events in the Windows Forms shape:

- **On a control:** `preview_key_down`, `key_down`, `key_press` and
  `key_up`.
- **On a window:** `key_down`, `key_press` and `key_up`. These reach the
  window before the focused control, like Windows Forms' KeyPreview
  (always on for a subscriber). A handler that sets `handled` keeps the key
  or its text from the control.
- **`key_state_changed`:** the window's raw press and release of each
  physical key, raised once (no auto-repeats) before anything can consume
  the key. It includes the releases the window makes when it loses
  activation. `Window::is_key_down(physical_key)` says whether a key is
  down now.

`KeyEventArgs` carries:
- `key_code`, the Windows Forms `Keys` code;
- `physical_key` and `scan_code`;
- the modifiers;
- `repeat`;
- `released_by_deactivation`;
- `handled` and `suppress_key_press`.

`PreviewKeyDownEventArgs::is_input_key` claims a navigation key for the
control. `KeyPressEventArgs` carries committed text only: input-method
marked text passes untouched, and `suppress_key_press` drops a key's text.

A key travels in this order:
1. `key_state_changed`;
2. key rules;
3. `preview_key_down`;
4. accelerators (ProcessCmdKey);
5. the window's `key_down`;
6. the control's `key_down`;
7. the bubble to its ancestors;
8. accelerators, dialog keys and Tab traversal (ProcessDialogKey), unless
   the control claimed the key as an input key.

Every handler runs on the UI thread, and a handler may close the window,
which ends the key's route. In the C ABI, `windows.key_state` reads a key's
state, and the key interactions and their payloads are in the schema.

## Focus

`Window::request_focus(control)` moves focus if the control is focusable,
effectively visible and enabled, and the current control's validation allows
it. `focused_control()` returns the current owner. Focus is revoked, and
`focus_revocations` incremented, when the focused control is disabled, hidden,
detached or begins initialization.

Tab order
: `tab_index` orders siblings; `tab_stop` removes a control from Tab traversal
  without preventing explicit focus. `get_next_control` returns the
  preceding or following live descendant in nested tab order without wrapping
  or moving focus. Containers participate: `ContainerControl::active_control`
  remembers the focused descendant so returning to a group box restores the
  field you left.

Focus scopes
: `begin_focus_scope(options)` opens a nested scope for an overlay, a menu or a
  modal panel. While the scope is open, traversal and mnemonics are confined
  to its subtree; `end_focus_scope` restores the previously focused control.
  Scopes nest to `maximum_focus_scope_depth` (32) and every open, close,
  restoration and rejection is counted.

Validation
: `causes_validation` defaults to true. Before focus leaves a control,
  `validating` receives a cancellable event; under the container's
  `AutoValidate` policy a cancel keeps focus where it was, and `validated`
  follows only an accepted validation. The window rejects nested focus
  mutation during a validation callback and rechecks both endpoints after
  the callbacks return. `ValidationSnapshot` reports attempt, success, cancel,
  block and reentrancy counts.

Composite fields
: A combo box with an edit field, NumericUpDown, DomainUpDown,
  MaskedTextBox, TokenBox and SearchField give focus to their edit part
  (`Control::focus_target`). Focusing the composite puts the caret in the
  edit, as in Windows Forms. The window's active control is the composite,
  and the composite reports itself focused while its edit holds the caret.
  `focusable` is a schema property.

Focus restore
: A window remembers its focused control when its own root is disabled or
  hidden, and gives focus back when the root is enabled or shown again,
  when the window is reactivated, and when a modal window over it closes.
  A control disabled inside the window still loses focus for good, as in
  Windows Forms.

From C
: `controls.focus` gives a control focus, and refuses with
  `GF_STATUS_INVALID_STATE` when the control cannot take it.
  `controls.focus_state` reads Focused and ContainsFocus.
  `windows.focused_control` reads a window's focused control.

## Activation and dialog keys

The window holds retained accept and cancel targets. Return activates the
accept target and Escape the cancel target when the focused control does not
consume the key; each target sets an exact `DialogResult`. `Button::
perform_click` is the programmatic equivalent and goes through the same
validation and enablement checks as a pointer click, so a test that calls it
proves what a user would see.

Window activation and deactivation arrive from the host as lifecycle events.
A deactivated window keeps its focused control; it stops painting a focus
ring and resumes on reactivation.

## Semantic actions

Every stock control publishes a semantic node (role, name, state, available
actions) into a renderer-free snapshot. `perform_semantic_action` lets an
accessibility bridge, an automation client or a test invoke "press",
"increment", "set value" or "expand" on a control by stable ID. The native
accessibility publication on each platform is built on this same tree; there
is no separate automation-only control model. The Gallery's automation
channel is this call behind a length-limited, versioned command string.

## Drag and drop

The portable drag protocol supports text, file lists and media-typed byte
payloads, negotiated per session with a returned effect. Native inbound drag
destinations are advertised on macOS and X11, but not on Windows.
Outbound drag initiation is not yet available on any host.

## Counters

`MetricsSnapshot` carries `input_events`, `focus_transitions`,
`focus_scopes_opened`, `focus_scopes_closed`, `focus_scope_restorations`,
`focus_scope_rejections`, `maximum_focus_scope_depth`, `activations`,
`focus_revocations`, `capture_revocations` and `press_revocations`. A focus
bug usually shows as a transition count that disagrees with what the test
expected, long before it shows on screen.
