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

The program connects with `HostedClient::connect()`, then loops:

1. `acquire_slot` gets a frame slot the application has released.
2. It draws into `slot_pixels`.
3. `publish` hands the frame over.
4. `pump` handles the application's messages: size changes, slot
   releases, input and stop.

`pump` returns false when the application asks the program to stop or goes
away. The client library is `gui_forms_hosted_client`. It links the
protocol and nothing of the interface.

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
returns `not_reported` and `make_hosted_content` throws.

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
