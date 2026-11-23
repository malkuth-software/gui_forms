# GUI.Forms Correspondence

A mail-client mockup built from public GUI.Forms seams only: the
Modern.Forms "Outlaw" equivalent named in [`planning/POLISH_STAGE_PLAN_2026-10-11.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/POLISH_STAGE_PLAN_2026-10-11.md)
(W5.3). Owner decision F.2 admits its three-pane layout as a *gallery
demonstration of reusable panel controls*; it is not a consumer product and
ADR-013 recorded that classification (ADR-013 was retired on 2026-10-13).

## What it demonstrates

- A retained three-pane shell: folder tree, message list, reading pane, with
  a menu strip above, a command row below it and a status line at the bottom,
  all composed with `DockStyle` on one `Panel` and two nested `SplitContainer`s.
- One `Command` per action shared by three surfaces, so every route executes
  the same object and the shell subscribes once per command:
  the `CommandBar` buttons (with keyboard shortcuts registered through the
  bar), the `MenuStrip` menus, and the `ContextMenu` opened by a secondary
  click on a message row.
- Command enablement and checked state projected from the model (nothing is
  enabled without a selection; Archive is disabled inside Archive; Flag shows
  its checked state; the View menu checks the current folder).
- Imperative, stateful mutation of a virtual list: the `CorrespondenceView`
  receives a fresh item vector on every projection and never owns a control per
  message; folder unread counts are projected into `TreeView` item text.
- Hover `ToolTip`s on the command buttons and the search field.
- Bounded flushes: every projection runs inside one `Window::begin_update()`
  scope and re-entrant control notifications are ignored while it runs.
- A headless-friendly About surface: Help > About asks `HostServices` for a
  message dialog and falls back to the status line, where it reports the live
  `MetricsSnapshot` (retained control count, paint passes, mutations).

Window: 1120x720 by default, minimum 600x420 (logical). The outer split keeps
the folder tree at least 140 wide and the content at least 320; the inner split
keeps the list at least 120 tall and the reading pane at least 140.

## Fixture rule

There is no real mail. Six folders (Inbox, Drafts, Sent, Archive, Junk, Trash)
and forty messages live in `correspondence_model.cpp` as a constant table for a
fictional observatory society whose addresses use the reserved `.example`
domain. The program:

- reads no filesystem, opens no network connection, and never reads a clock
  (new drafts carry a fixed authoring timestamp);
- sends nothing: "New message", "Reply" and "Forward" create Drafts fixtures
  and show them in the reading pane. The public `Application` API can host
  several windows only when they are constructed before `run()`, and the
  portable host dialogs are message/file/colour dialogs, so no compose window
  is opened; this is deliberate and documented rather than worked around;
- "Delete" moves to Trash, and deleting from Trash removes the fixture for the
  rest of the session;
- is deterministic: two `CorrespondenceModel` instances start identical.

## Files

| File | Role |
|---|---|
| `correspondence_model.hpp/.cpp` | Pure C++ fixtures and state (`CorrespondenceModel`): folder/message selection, read marks, flags, archive, delete, search, compose. No controls. |
| `correspondence_controls.hpp/.cpp` | `CorrespondenceShell`, the retained tree, commands, menus, projection and event handlers. |
| `correspondence.hpp/.cpp` | `make_correspondence()`: builds the shell, the `Window`, tooltips and the first layout, like `gallery::make_gallery()`. |
| `correspondence.cmake` | All CMake for the program and its tests; the root `CMakeLists.txt` includes it with one line. |
| `src/host/{macos,windows,linux}/correspondence_main.*` | Host mains using `demo/program_options.hpp` (`--mcp`, `--capture`, `--size`, `--scale`). |
| `tests/correspondence_tests.cpp` | Model-only tests. |
| `tests/correspondence_interaction_tests.cpp` | Headless window tests: pointer clicks by stable ID, text input, accelerator, context menu, 600x420 layout. |

## Stable IDs

Every control carries a `correspondence.` prefix for automation and tests:
`correspondence.shell`, `.menu`, `.commands`, `.command-bar` (buttons are
`.command-bar.item.1` New message, `.2` Reply, `.3` Forward, `.4` Archive,
`.5` Delete, `.6` Flag), `.search`, `.split`, `.folders` (rows
`correspondence.folder.<inbox|drafts|sent|archive|junk|trash>`),
`.content-split`, `.messages` (rows `correspondence.message.<n>`),
`.reading`, `.reading.subject`, `.reading.correspondent`, `.reading.date`,
`.reading.body`, `.status`, and the context menu `correspondence.messages.menu`.
Commands are `correspondence.command.<new|reply|forward|archive|delete|flag|mark-unread|search|about|view.<folder>>`.

Shortcuts: Ctrl+N, Ctrl+R, Ctrl+Shift+F, Ctrl+E, Delete, Ctrl+L. Window
accelerators run only after the focused control declines the key, so Delete
inside the search box edits text.

## Registry hint

Control registry ids from `manifests/controls.json` exercised by this program,
so the per-control docs can link to it:

`menu-strip`, `context-menu`, `command`, `command-bar`, `button` (bar items),
`text-box` (search field and read-only wrapped body), `split-container`,
`splitter-panel`, `tree-view`, `correspondence-view`, `panel`, `label`,
`tool-tip`.

Why these controls: `CorrespondenceView` is the toolkit's variable-height
virtual list of correspondence records (title, secondary text, metadata,
excerpt, metric, information tags) and matches the message list exactly, so it
was preferred to `ObjectView` (a columnar object collection) and `ListBox` (a
flat string list). `TreeView` holds the folders because its items are stable-ID
rows whose text can carry the unread count. `CommandBar` was chosen over a
hand-placed button row because it owns the item buttons, binds each to its
`Command`, and registers the shortcuts. `MasterDetailView` was not used: it is
a two-role shell with its own compact navigation, while this layout needs two
independent splits whose panels are ordinary containers.

## Build and run

Configured together with the Gallery (`GUI_FORMS_BUILD_GALLERY`):

```text
cmake --build build --target gui_forms_correspondence          # macOS bundle
cmake --build build --target gui_forms_correspondence_windows  # Windows host
cmake --build build --target gui_forms_correspondence_linux    # X11 host
ctest --test-dir build -R correspondence --output-on-failure
```

The faces come from the font pack compiled into GUI.Forms. Only their licences
and the attribution are copied, into `fonts/` next to the executable (Windows,
Linux) or into `Contents/Resources/fonts` (macOS), exactly as for the Gallery.
The macOS bundle runs the same font policy check, which also fails if the
bundle holds a face file.
