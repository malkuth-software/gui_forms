# Content surfaces

Some of a program's window is not interface. A document page, a map, a
game view or a preview is *content*. Your program renders it with its own
engine and its own fonts, on its own thread, and sometimes in its own
process. GUI.Forms draws the interface around that content. Its
interface text uses only the fonts bundled with GUI.Forms (see
[Your own HarfBuzz and FreeType](consume-the-sdk.md#your-own-harfbuzz-and-freetype)).
Content brings whatever it needs.

The content boundary is an **integrity boundary, not a security
boundary**. If content stalls, fails or publishes nonsense, it cannot stop
the interface or corrupt it. Content in your process still runs with
direct access to that process. A hosted program still runs with your
application's rights. Neither is confined, and nothing on this page
sandboxes untrusted code.

The headers are `<gui_forms/content.hpp>` (all of the below) and
`<gui_forms/content/hosted/hosted_client.hpp>` (the hosted program's end).
The examples are in `examples/content_boundary/`.

## Publishing frames

A `ContentSurface` is a control with a region whose pixels come from you.
`open_content(opaque)` returns a `ContentFrameSink`. Any thread may hold
the sink, copy it and call it.

- `publish(frame)` hands over one finished frame: premultiplied BGRA rows,
  with the frame's width, height, row stride and an optional damage
  rectangle. The frame is validated before a byte is read: its shape, its
  limits (16384 pixels per axis, 256 MiB), its format, its stride, how far
  the span must reach, and its damage. A refused frame leaves the last good
  frame on screen. The result tells you what happened.
- `request()` and `wait_for_request(seen, timeout)` tell you what to draw:
  the region's device size and the window's scale, with a generation number
  that grows whenever either changes.
- `heartbeat()` tells the surface you are alive while nothing has changed.
- `publish_semantics({name, text, description})` gives the surface the text
  that assistive technology reads. Your pixels carry no text, so supply
  this.

The interface thread never waits for you. It shows the newest finished
frame and moves on. Once the surface revokes the channel (`close_content`,
new content, the surface leaving its window, its destruction), every call
on the sink reports `revoked` and touches nothing the interface reads.

You can also let the surface run your renderer on a thread it owns.
Derive from `ContentProgram`, implement `run(ContentContext&)`, and call
`surface.start_program(std::make_unique<MyRenderer>(), true)`. The
context gives you the sink and a stop token.

## When content freezes

The surface runs a watchdog. If content publishes no frame and sends no
heartbeat for longer than `set_stale_interval` (one second by default), its
status becomes `stale`, and the surface does three things:

- keeps the last good frame under a veil, with a caption;
- publishes the busy state to assistive technology;
- raises `status_changed`, so your window can offer a way out.

The next frame or heartbeat makes the surface `live` again. The watchdog
runs on a timer that rests while the window is hidden. A gap longer than
two periods restarts the interval instead of counting as silence, so a
minimised window does not make healthy content look frozen.

`terminate()` asks the content to stop and returns at once. It never
waits. What happens next depends on where the content runs:

- A program the surface started gets its stop token. If it honours the
  token, its status becomes `ended`, or `failed` if it threw. If it is still
  running when the termination grace passes (`set_termination_grace`,
  250 ms by default), the surface **abandons** it: the channel is revoked
  and the thread is detached. The status becomes `abandoned` and the last
  frame stays, veiled.
- Content on your own threads is revoked at once, because the surface
  cannot see your threads end.

Closing the window, destroying the surface and shutting down the
application all follow the same bounded path. They never join a hung
content thread.

An abandoned thread keeps running until its code returns or the process
exits. It keeps alive, by shared ownership, only its program object and
its channel. Content that may hang must not share static state with the
rest of the program. `abandoned_content_workers()` counts the abandoned
threads that are still running, for a shutdown report.

`examples/content_boundary/freezing_renderer.cpp` is a renderer that draws
a few frames and then hangs without ever checking its stop token again.
While it is hung:

- a counter button keeps counting;
- the surface turns stale;
- the window enables Terminate.

Pressing Terminate abandons the renderer after the grace. The program
exits normally with the hung thread still running.

## Hosted content

If you cannot trust content to stop, run it in a process of its own,
where GUI.Forms can force it to end. `make_hosted_content(options)` takes
the program's absolute path, its arguments, the frame slot size, a start
timeout and a stop grace. It returns a `HostedContent`, which holds:

- `program`: a `ContentProgram` for `start_program`;
- `input`: a `HostedInput`, which forwards keys, text and pointer events to
  the process.

The surface's status, watchdog, termination and veil work as they do for
content in your process.

`ContentSurface::start_hosted_content(options, opaque)` does both steps in
one call: it starts the program on the surface and keeps the input
channel. From then on the surface forwards its own input to the program
(below). `hosted_input()` returns the channel, for its `dropped()` count
and `connected()` state.

The program connects with `HostedClient::connect()`, then loops:

1. `acquire_slot` gets a frame slot the application has released.
2. It draws into `slot_pixels`.
3. `publish` hands the frame over.
4. `pump` handles the application's messages: size changes, slot
   releases, input and stop.

`pump` returns false when the application asks the program to stop or goes
away. The client library is `gui_forms_hosted_client`. It links the
protocol and nothing of the interface. The SDK installs it on macOS and
Linux as the `HostedClient` component:

```cmake
find_package(GUIForms CONFIG REQUIRED COMPONENTS HostedClient)
target_link_libraries(my_hosted_program PRIVATE GUIForms::HostedClient)
```

`tests/hosted_client_consumer` is an installed consumer with both ends: a
parent that links `GUIForms::Controls`, and a program that links only
`GUIForms::HostedClient`. `tools/verify_install.py <prefix> --hosted-client`
builds and runs it.

### Input to the hosted program

A surface started with `start_hosted_content` forwards its pointer,
keyboard, text and focus input to the program. The program reads it with
`HostedClient::take_input` after each `pump`. The order follows the input
rules in `manifests/abi/interaction_order.json`:

- **GUI.Forms sees each event first.** The surface raises its own event
  (`key_down`, `mouse_down`, and so on) and runs its rules. Only an event
  that nobody handled is forwarded. A subscriber that sets `handled`, or
  `suppress_key_press` for a key, keeps that event from the program.
- **Only physical events are forwarded:** key down and key up, committed
  and composing text, pointer press, release, move and wheel. Derived
  events, such as click, double click and key press, stay in GUI.Forms;
  the program derives its own.
- **Focus goes after GUI.Forms' focus events.** The program receives focus
  on after `got_focus`, and focus off after `lost_focus`. A surface that
  already has the focus when the program starts sends focus on at once.
- **Pointer positions are in device pixels,** relative to the surface,
  like the frames.
- **Input never blocks the interface.** Messages are sent without waiting.
  When the program does not read, they queue, and above 1 MiB of backlog,
  key downs, text and pointer events other than releases are dropped and
  counted in `dropped()`. Key ups, pointer releases and focus changes are never
  dropped, so the program never sees a key or button stuck down.
- **New content ends the forwarding.** `open_content`, `start_program` and
  `close_content` stop it; only `start_hosted_content` starts it.

The application is promised four things:

- **A crash ends only the program.** The surface becomes `failed`, and its
  failure text names the signal or exit status. The application keeps
  running and can start the program again.
- **A hang leaves the interface responsive.** The watchdog marks the
  surface stale, and `terminate()` ends the process: first a stop message,
  then, after the stop grace, a kill. Keep the stop grace below the
  surface's termination grace, so the process ends before the surface stops
  waiting.
- **Closing the window, destroying the surface or shutting down** ends the
  process the same way.
- **Everything the program sends is checked before use**: message kinds and
  sizes, frame records against their slot, and text. A program that breaks
  the protocol is ended with status `failed`.

Pixels travel through a shared region of two slots: the program draws one
slot while the application copies the other. Messages travel through a
socket pair. Every integer on the wire is little-endian and written byte by
byte, so the two ends need not share a compiler. The protocol is in
`hosted_protocol.hpp`.

`examples/content_boundary/hosted_crash.cpp` starts `crashing_program`,
which shows frames and then aborts. The surface reports the crash, the
application's counter keeps counting, and Restart runs a new copy on the
same surface. That copy crashes too, and the application is still
unaffected.

Hosted content works on macOS and Linux, and the hosts report it as
`HostCapability::hosted_content`
([Hosts and capabilities](hosts-and-capabilities.md)). The Windows
implementation is not written yet. On Windows, `hosted_content_support()`
returns `not_reported` and `make_hosted_content` throws. What it must
provide is listed in [What Windows hosted content must
provide](#what-windows-hosted-content-must-provide).

## Editing text you lay out yourself

`ContentTextEditor` is for text your own engine shapes and draws. It is a
text control: GUI.Forms keeps the text and handles the caret, the
selection, the editing keys, input methods, undo and redo, the clipboard
and the accessibility publication, through the same editing engine as
`TextBox`. You supply the layout and the pixels:

1. On `layout_requested(revision)`, lay out `text()`.
2. Call `submit_layout` with a `ContentTextLayout` (lines, clusters and
   optional caret stops) made for that revision.
3. Publish a frame through the sink that `open_frames` returns.

The layout is validated against the text: its ranges must be ordered, lie
on grapheme boundaries and cover each line, and its boxes must be finite.
A layout for an older revision is refused as stale.

Until a current layout arrives, editing still works, but the caret and
selection are not drawn. The editor draws the selection and caret over
your frame, unless `set_draws_selection(false)` leaves them to you
(`caret_rectangle`, `selection_rectangles`).

## From C and C#

The C ABI's `GF_TABLE_CONTENT` (since 1.4, `gf_content_table` in
`gf_abi.h`) covers all of this: a content surface's channel, termination,
the status and its captions, the statistics and semantic text, content
programs, hosted content and the content editor. The C# bindings negotiate
it as `Abi.content`.

- **A sink is a handle** (`gf_content_sink`) that any thread may call.
  `open_content`, `editor_open_frames` and, inside a program,
  `program_sink` hand one out; `sink_release` ends it. The channel lives on
  while a handle or the surface holds it.
- **A refusal is a result, not a failed call.** `sink_publish`,
  `sink_publish_semantics` and `editor_submit_layout` return `GF_STATUS_OK`
  with the outcome in `result`, as the C++ functions return theirs.
- **A content program is a callback.** `start_program` takes a
  `gf_content_program` whose `run` is invoked once, on the program's own
  thread. Inside it, `program_sink` gives the program's sink and
  `program_fail` names its failure. The library releases the callback on
  that thread after `run` returns. When `start_program` fails, the caller
  keeps it.
- **Hosted content** starts with `start_hosted`. It gives
  `GF_STATUS_UNSUPPORTED_CAPABILITY` where `hosted_support` answers
  `GF_HOSTED_SUPPORT_NOT_REPORTED`, and the host reports
  `GF_HOST_CAPABILITY_HOSTED_CONTENT` where it is available. Input is
  forwarded as described above; `hosted_input` reads the channel's state.
- **Times are milliseconds,** at most `GF_CONTENT_MAXIMUM_WAIT_MS` (an hour).
  The UI thread must not call `sink_wait_for_request`.

The conformance kit's `gf_conformance_content.c` drives every slot.

## What Windows hosted content must provide

The Windows implementation replaces `src/host/windows/hosted/hosted_content_windows.cpp`
and adds a Windows hosted client. It must keep the portable contract in
`hosted_content.hpp`, `hosted_client.hpp` and `hosted_protocol.hpp`
unchanged, and provide exactly this:

1. **Support and capability.** `hosted_content_support()` returns
   `available`. The Win32 host reports `HostCapability::hosted_content`,
   the `win32-dib` row of `manifests/abi/host_capabilities.json` lists
   `hosted_content`, and `start_hosted` stops answering
   `GF_STATUS_UNSUPPORTED_CAPABILITY`.
2. **Starting the program.** `make_hosted_content` validates the options
   as the POSIX version does: an absolute executable path, at most
   `maximum_hosted_slot_bytes` per slot, and timeouts that are not negative. It
   refuses before starting anything. The program starts with
   `CreateProcessW` and an explicit handle list
   (`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`), so it inherits only the channel
   and the frame region. The arguments are quoted by the Windows rules.
3. **The channel.** A byte stream carrying the same messages: an
   anonymous pipe pair, or a named pipe opened with
   `FILE_FLAG_OVERLAPPED`. The parent's sends never block. They use the
   same backlog: whole messages only, never interleaved, and droppable
   input dropped above 1 MiB of backlog. Key down, text and every pointer
   event but release are droppable; key up, pointer release and focus are
   not.
4. **The frame region.** A pagefile-backed section
   (`CreateFileMappingW`) of `hosted_slot_count` slots of `slot_bytes`
   each, sized once. The parent maps it with `FILE_MAP_READ` only, so the
   program cannot make the parent's reads fault by shrinking it.
5. **The environment.** The program finds both handles through the same
   variables: `GUI_FORMS_HOSTED_CHANNEL`, `GUI_FORMS_HOSTED_FRAMES`,
   `GUI_FORMS_HOSTED_SLOT_BYTES` and `GUI_FORMS_HOSTED_SLOT_COUNT`. On
   Windows the first two hold handle values in decimal.
6. **Lifetime.** A job object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`,
   so the program ends with the application even when the application
   crashes.
7. **The worker loop.** `ContentProgram::run` waits with
   `WaitForMultipleObjects` over the channel's event, the process handle
   and a stop event. It validates every message as the POSIX loop does:
   hello first and once, within the start timeout; frames against their
   slot; semantic text as UTF-8 within the limit. It copies each frame out
   of the region and then sends `release` for the slot. It answers size
   changes with `configure`. Any protocol break ends the program as
   `failed`, with the reason as its failure text.
8. **Stopping.** On a stop request it sends `stop`, waits the stop grace,
   then calls `TerminateProcess`. A program that closes its channel
   without exiting is killed after the stop grace and reported as such.
9. **Exit reports.** Exit code 0 after goodbye is `ended`. Any other exit
   code is `failed` with "exited with status N". Exit codes of
   `0xC0000000` and above are crashes, reported with their NTSTATUS in
   hexadecimal (for example `0xC0000005`, an access violation).
10. **Input.** `HostedInput` sends key, text, pointer and focus messages
    with the same encodings (`encode_hosted_key`, `encode_hosted_text`,
    `encode_hosted_pointer`, `encode_hosted_focus`). `connected()` and
    `dropped()` behave as on POSIX.
11. **The hosted client.** `HostedClient::connect()` reads the variables,
    duplicates nothing it does not own, maps the region read-write, and
    sends hello. `channel_descriptor()` has no Windows meaning; the
    Windows client needs a handle accessor in its place, added beside the
    portable members rather than changing them. `gui_forms_hosted_client`
    is then built and installed on Windows too, the `HostedClient`
    component's diagnostic in `GUIFormsConfig.cmake.in` drops "macOS and
    Linux", and `tests/hosted_client_consumer` runs there.
12. **The tests.** `tests/hosted_content_tests.cpp` runs on Windows with
    `tests/hosted_child.cpp` built for it: the same cases (frames, input
    order, crash, hang, protocol break, start timeout), with the crash
    case expecting an NTSTATUS instead of a signal name.

