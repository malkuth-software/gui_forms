# Relative pointer mode

A game, a 3-D view or a precise drag wants the pointer's *motion*, not its
position: every sample the device produced, as a delta, with the host's
time, fractions kept. A control asks its window for that with
`Control::request_relative_pointer`
(`gui_forms/events/relative_pointer/relative_pointer.hpp`), and hears each
sample as its `relative_pointer` event until `relative_pointer_ended`.

The design follows what game engines and platforms already agree on: SDL3's
relative mouse mode, GLFW's raw motion and disabled cursor, Unity's and
Unreal's pointer lock, Windows Raw Input with `ClipCursor`, macOS's
disassociated cursor with raw deltas, Wayland's relative-pointer and
pointer-constraints, and the browser's Pointer Lock with
`unadjustedMovement`.

## Modes

| Mode | Pointer | Buttons and wheel | Use |
|------|---------|-------------------|-----|
| `free` | shown, moves and clicks as usual | routed as usual, beside the samples | precision with the pointer visible |
| `confined` (the default) | kept inside the control (shown unless `hide_cursor`) | samples of the owner; no click is derived | orbit, aim, drag-to-turn |
| `locked` | hidden and held still | samples of the owner | first-person look |

`free` is the **visible-cursor variant**: raw (unaccelerated) deltas where the
host has them and every sample, while the pointer stays shown and unlocked.
Applications that must keep the pointer visible use it.

`raw_motion` asks for unaccelerated deltas; the grant says whether the host
gave them (`RelativePointerGrant::raw`). A host that cannot do the mode asked
does the nearest one below it and says so (`RelativePointerResult::degraded`):
locked, then confined, then free with deltas the window derives from
positions (`host_relative` false: those deltas stop at the screen's edge and
are accelerated). Nothing is refused for a missing capability alone.

## The shell stays reachable

The window, not the game, declares **release regions**
(`SurfaceRoot::set_pointer_release_regions`, at most eight): the shell's
capsule and its kin. A confined pointer's area always covers them, and a
locked pointer's virtual position can reach them. Over a region a confined or
locked mode is **suspended**: the ordinary pointer reappears there (a locked
one is warped to where its virtual position arrived) and clicks the region's
control as usual; a press there may take focus without ending the mode. The
mode resumes when the pointer moves back off every region.

The mode also **ends**, whatever the owner does, and raises
`relative_pointer_ended` once with the reason:

- Escape (the key is taken: no control sees it, nor its release);
- focus moving to a control outside the owner;
- the window's deactivation;
- a modal window, dialog, menu or any focus scope opening;
- the owner disposed, detached, hidden or disabled;
- the release of the press the mode began under, for `until_release`;
- `release_relative_pointer`, or another control's request (`superseded`).

## Order

`manifests/abi/interaction_order.json`, sequence `relative_pointer`: one
`relative_pointer` per host sample, in the host's order, never merged per
frame, each with its timestamp; then `relative_pointer_ended` once. In
confined and locked modes buttons and the wheel are samples and the derived
events (`click`, `mouse_click`, `double_click`, a button's activation) do not
apply; a granted confined or locked mode also ends the pointer sequences in
progress and releases the pointer capture, so nothing derives across it. In
free mode the routed mouse events go on beside the samples.

## Timestamps and scroll phases

Every `PointerEvent`, `MouseEventArgs` and `RelativePointerEventArgs` carries
`timestamp_nanoseconds`: nanoseconds of the library's monotonic clock
(`FrameClock`), the same unit and clock on every host, so the difference
between two events is their real spacing. A host stamps an event with its own
time where it has one (macOS `NSEvent.timestamp`, X11 server time through
`HostEventClock`); otherwise the event carries its host event's receipt time,
and an event handed to a window unstamped is stamped at dispatch.

A precise wheel (a trackpad) also carries `wheel_phase` (`WheelPhase`:
began, changed, ended, cancelled, momentum, momentum_ended), beside
`wheel_delta` in `wheel_unit` (pixels for a precise device, lines for a
detented wheel). macOS reports it; a detented wheel reports `none`.

## Hosts

| Host | relative_pointer | raw_pointer_motion | pointer_confinement | pointer_warp |
|------|------------------|--------------------|---------------------|--------------|
| macOS (AppKit) | yes | no | yes | yes |
| Linux X11 | with XI 2 | with XI 2 | yes | yes |
| Windows | contract below | contract below | contract below | contract below |
| Web (Emscripten) | interface note below | | | |

**macOS.** Locked disassociates the pointer from the mouse
(`CGAssociateMouseAndMouseCursorPosition(false)`) and hides it; confined is
kept inside its rectangle by warping (`CGWarpMouseCursorPosition`), as AppKit
has no native confinement; every mode turns AppKit's mouse coalescing off
while it lasts. Each moved or dragged event becomes one sample (`deltaX`,
`deltaY`). AppKit's deltas are accelerated and unaccelerated ones need IOHID,
so raw motion is not reported.

**Linux X11.** XI 2 raw motion on the root window gives the deltas
(`raw_values` unaccelerated, the valuators accelerated), one sample per raw
event, with the server's time unwrapped and mapped onto the steady clock. A
core pointer grab confined to an InputOnly child covering the area confines
the pointer; locked confines it to the window and warps it back after each
sample; XFixes hides it. Without libXi in the build or XI 2.0 on the server,
the host drops `relative_pointer` and `raw_pointer_motion` from its report.

### Windows contract (for the Windows host)

What the Windows adapter implements to report the four capabilities; until
then it reports none and the window derives deltas from positions.

- `set_relative_pointer_impl`:
  - any active mode registers the mouse for Raw Input
    (`RegisterRawInputDevices`, usage page 1, usage 2, `RIDEV_INPUTSINK` off,
    the window as target) and removes the registration on release;
  - confined calls `ClipCursor` with the confine rectangle in screen
    pixels (client to screen at the window's scale) and locked with a 1x1
    rectangle at the pointer's position; release calls `ClipCursor(nullptr)`;
  - hiding uses `ShowCursor(FALSE)` once and is balanced by one
    `ShowCursor(TRUE)`;
  - re-apply the clip on `WM_ACTIVATE`, `WM_SIZE`, `WM_MOVE` and
    `WM_DPICHANGED` while active, since Windows drops a clip on those.
- Each `WM_INPUT` (`GetRawInputData`, `RIM_TYPEMOUSE`, relative motion
  without `MOUSE_MOVE_ABSOLUTE`) becomes one `HostRelativeMotionEvent`: `lLastX`
  and `lLastY` are unaccelerated counts, scaled to logical units by the
  window's scale, `unaccelerated` true. A `MOUSE_MOVE_ABSOLUTE` device (a
  tablet, a remote desktop) is turned into deltas against its previous
  sample and reported with `unaccelerated` false.
- Timestamps: `GetMessageTime` (32-bit milliseconds) unwrapped and mapped
  through `HostEventClock`, as X11 does; until then the receipt time.
- In confined and locked modes the host sends no `WM_MOUSEMOVE` as a
  `PointerEvent`; buttons and the wheel stay `PointerEvent`s.
- `warp_pointer_impl`: `SetCursorPos` at the client point in screen pixels.
- Report `relative_pointer`, `raw_pointer_motion`, `pointer_confinement`
  and `pointer_warp`, and add them to the `win32-dib` row of
  `manifests/abi/host_capabilities.json`.

### Web interface note (for the Emscripten host)

The browser's Pointer Lock maps onto the same seam; the web host implements
it.

- `set_relative_pointer_impl` with locked (or confined, which the browser
  cannot do: report `pointer_confinement` false, so the window asks for
  free or locked only): `canvas.requestPointerLock({unadjustedMovement:
  raw_motion})`. The request needs a user gesture: call it from the press
  that asked, and treat a rejected promise or `pointerlockerror` as
  `backend_failure` (the window then falls back to free). A browser
  without `unadjustedMovement` rejects that option: retry without it and
  report `raw_pointer_motion` false.
- Free mode needs no lock: `pointermove` events with
  `getCoalescedEvents()` deliver every sample, each as one
  `HostRelativeMotionEvent` from `movementX` and `movementY` (CSS pixels
  are logical units) with its `timeStamp`.
- While locked, each `pointermove` (and each of its coalesced events) is one
  sample; no absolute `PointerEvent` moves are sent; buttons and the wheel
  stay `PointerEvent`s.
- The browser's own Escape releases the lock (`pointerlockchange` with no
  locked element): end the mode as `escape` (dispatch the Escape
  `KeyEvent`, or have the window end it) so the core and the page agree.
- `timeStamp` is milliseconds on `performance.now()`'s clock: map it with
  `HostEventClock` from `performance.now()` at receipt.
- `warp_pointer_impl` is unsupported (no `pointer_warp`).

## C ABI and C#

`gf_controls_table` (since 1.5): `request_relative_pointer`,
`release_relative_pointer`, `relative_pointer_state`, `warp_pointer` and
`set_pointer_release_regions`; the samples and ends are the control's
`relative_pointer` and `relative_pointer_ended` events, whose fields the
interaction catalog lists (`manifests/abi/interaction_fields.json`). The
host capabilities are `GF_HOST_CAPABILITY_RELATIVE_POINTER` to
`GF_HOST_CAPABILITY_POINTER_WARP`. In C#, `GuiForms.Forms.RelativePointer`
wraps the slots and `RelativePointerEventArgs` and `RelativePointerEnded` read
the events.

The example `examples/relative_pointer` (an orbit toy with the shell's capsule
as its release region) runs natively and, headless, in
`tests/relative_pointer_example_tests.cpp` in [gui_forms_tests](https://github.com/malkuth-software/gui_forms_tests).
