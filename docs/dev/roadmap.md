# Roadmap

GUI.Forms is pre-1.0. This page says what is done, what is being worked on
now, and what has to be true before the API is declared stable. It is a
summary of the planning documents in the [`planning/`](https://github.com/malkuth-software/notes/tree/gui_forms/planning) directory of the project's notes repository,
which remain the detailed source; where they disagree with the code, the code
and its tests are the authority.

```{image} ../_static/roadmap.svg
:alt: Releases toward 1.0: 0.1 Shipped, 0.2 Verified, 0.3 Themeable, 0.4 Flexible and 0.5 ABI seal are done; 1.0 Backend is in progress; then Tools (the designers), then 1.0 Stable.
:width: 100%
:class: roadmap-figure
```

## 0.1 Shipped

What GUI.Forms is today, tested in CI on macOS, Windows and Linux. The
details are under [Done and tested](#done-and-tested).

- A retained core with explicit ownership, typed dirtiness and nested update
  scopes, painted on the CPU only where something changed.
- One text engine with identical output on every platform.
- More than 70 stock Forms controls, with menus, commands and data binding.
- Native hosts for macOS, Windows and X11, which also runs under XWayland.
- An installed CMake SDK with fast, reproducible cached builds.

## 0.2 Verified

0.2 shipped as v0.2.0. Its last gate, font substitution, was recorded on
2026-10-23; the [0.2 release notes](../release/0.2) list what it delivers.

- **Accessibility verified on every host**. Client tests read the
  Gallery through each platform's own accessibility API (AX on macOS, MSAA
  and UI Automation on Windows, AT-SPI on Linux); Windows has a UI Automation
  provider; text fields expose editable text ranges on every host; and every
  native host advertises the capability.
- **Font substitution**. Families that applications ask for map to
  bundled faces with matching or nearest metrics, measured on every host
  ([`results/2026-10-23-f-font-substitution`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-23-f-font-substitution)).
- **Articulated controls**. Every control is built from named parts
  with states and public geometry, described by one schema that the C++ API,
  form documents, themes and the visual tools share.
- **Owned compression and image codecs on every host**: one zlib, an
  owned PNG codec and CONV interpolation, all GPL-free.

## 0.3 Themeable and 0.4 Flexible

These two stages are finished and were not tagged: their content shipped in
the published builds as it landed ([0.3 notes](../release/0.3),
[0.4 notes](../release/0.4)). What they delivered:

- **The theme engine**: themes are data (properties, layered effects,
  transitions, state machines and bounded expressions), and an idle window
  still draws nothing. Its gate is recorded: identical pixels on every host
  and no frames once a window settles.
- **The theme language**: CSS-syntax sheets addressed to named parts, with
  selectors, colour and text roles, `@media`, motion, packages and imports,
  custom properties, user themes and effects named from a registry the
  compiler checks.
- **The GUI.Forms theme**, light and dark, written in the theme language
  over a floor of `:family()` blocks, and **familiar themes** recreating the
  looks of Windows 2000, XP, 7 and macOS.
- **Compositor cooperation**: window backdrops as a chain (`blur`,
  `saturate`, `tint`), glass and reflections, on Windows, macOS and X11.
- **Universal Skia**: one CPU renderer on every host, `stx_skia`, the
  pruned and converted CPU derivative of Skia, with every third-party source
  now in the stx repository.
- **Flex, grid and block layout** checked against a browser's layout (211
  fixtures), with first-class grid spans.
- **Form documents** (`.form`) that round-trip hand edits byte for byte,
  bind their controls when they are created, and generate a window class.
- **Damage-preserving painting** on every host.
- **More Windows Forms controls**: the data grid, the list view, the tool
  and status strips, the common dialogs, TaskDialog and the small controls
  of the control superset (badges, banners, toasts, toggle switches, search
  fields, spinners and progress rings).

## 0.5 ABI and API seal (done)

0.5 was declared done on 2026-11-08, and its last checkpoint merged on
2026-11-09. It finalised the C ABI (version 1.1, with a blocking
breaking-change audit), properties as capabilities, the application, its
windows and rules, the Window, Popup and Splash kinds, the form language,
the theme language and effects, sub-surfaces and the creation-cost
contract. It also brought smooth scrolling, the window band, theme
variants and the reference application. After 0.5, a change to a sealed
interface needs a recorded owner decision. The
[0.5 release notes](../release/0.5) list it in full.

## 1.0 (backend): in progress

1.0 (backend) is the library, its C ABI and its C# bindings. Work goes in
checkpoints: coders write in parallel without building, an integrator folds
their branches into one tree, builds it once and runs the gate (the whole
suite, the sanitized tree and a libstdc++ compile of the changed files), and
the checkpoint merges as one pull request.

### Done since the seal

On `main` (pull requests #474 to #482; see [Unreleased](../release/unreleased)):

- **Checkpoints 24 to 26:** `gui-forms` as the default theme; C ABI
  additions for keys, NumericUpDown's range, modal and owned windows,
  anchored overlays, window properties and keyboard focus; styled text runs
  in TextBox; a public SHA-256; Finder open-files on macOS and Linux dialog
  filters that ignore case; the data grid in the C ABI; the ribbon gaps.
- **Checkpoint 27:** menus and drop-downs in Popup windows with the menu
  look; the invalidation overlay; the first look-integration rows; the final
  controls (seven-segment display, linear and radial gauges, dial, step
  indicator, wizard, callout, modal layer, lookup combo box, About dialog);
  the help system (help records, HelpBook, the CHM-shaped viewer, search,
  F1 and What's This); the declarative drag engine; the value bar; batch and
  polyline drawing and a conic gradient; the plots package; the C ABI
  docking table.
- **Checkpoint 28:** C ABI 1.2 (owner decision D9: window, focus,
  decoration, anchored overlay, data grid, docking and font surfaces, as
  additions), a data-provider binding and a help table; a much cheaper
  window tree; canvas scroll copies; TreeListView virtual rows; fail-fast
  native boundaries with expectation telemetry; hardening; runtime theme
  sheets.
- **Checkpoints 29 to 31:**
  - flat control headers (`include/gui_forms/controls/<name>.hpp`), the lab
    controls out of the installed library, and export lists generated from
    `GUI_FORMS_API`;
  - C ABI 1.3: generated class tables, so C and C# code can override a
    control's `on_*` hooks (additions only);
  - per-event raisers in the owner's input order, `Control::RightToLeft`,
    borrowed views with a checked build (`GUI_FORMS_CHECKED_BORROWS`) and
    the member manifest;
  - VirtualRowsView, DragTracker, NodeGraph undo and its save format,
    paragraph layout, ImageAtlas and allocation statistics;
  - row heights indexed by `stx_btree` from stx_auxlib, with fixed-point
    heights;
  - industrial surfaces: paint finishes, baked light planes, the GOST
    paint palette, the IndicatorLamp control and the danger key
    (`Button::danger`);
  - custom states in theme selectors, the canvas theme, the public
    NodeCanvas and dithered gradient fills;
  - the package version 0.6.0.

The first build under the 0.6 number is `v0.6.1541`.

### In progress: the Mac side, to the Windows hand-back

The current plan (started 2026-11-13) finishes 1.0 (backend) without its
Windows-only parts on macOS and Linux, then hands back to the Windows side.
It runs in streams. Much of it is already written in branches of
checkpoints 32 and 33 that wait to be folded:

Font identity
: A registry of the bundled families (`FontFamilyId`), family stacks with a
  theme fallback that warns instead of refusing, and the family across the
  C ABI and C#. Then the font pack: all bundled faces in one first-party
  file embedded in the library, so the interface faces cannot be missing.

Content boundary
: A content surface on its own execution context, with stale frames, a
  watchdog and termination; a content editing mode, where the program lays
  out and draws the text and GUI.Forms provides the caret, input, undo and
  accessibility; a program's own HarfBuzz and FreeType
  link beside GUI.Forms, which contains neither since 2026-11-16; and a hosted mode in which the content program
  runs in its own process (macOS and Linux first).

Engine and controls
: Stock icons (Heroicons), curved paths in the Painter and the drawing ABI,
  the locale from the host; the ribbon's minimized popup, Quick Access
  Toolbar on the tab row and help button; menu first-letter jumps, file
  dialog checks and the DataGridView breadth (frozen columns and rows,
  clicks, tooltips, hit testing); DataGridView rebuilt on VirtualRowsView's
  engine (the row index, the shared column placement and scroll copies);
  VoiceOver following keyboard focus; the window band's per-edge
  reservations, colour roles, cycling and textures in C ABI 1.3, with the
  control border as schema properties; effects taken from Dear ImGui and
  the active border on controls.

Demonstrators
: A node-graph editor (D1), a flame graph and profiler viewer (D2), an
  ImGui-class (D3) and an ImPlot-class (D4) program and 3D plotting (D5)
  exercise the drawing ABI. The drawing ABI (`gd_*`) is sealed only after
  all five exist, with owner review.

STXlib swap-in
: STXlib, a separate library that replaces stx_auxlib, gives GUI.Forms
  owned types at its boundaries instead of `std::` types, a platform layer
  and, in development builds, exception shadowing at every ABI boundary. It
  lands before 1.0; the row index moves to it in one fold.

1.0 conditions, Mac share
: macOS and X11 normative traces; Cmd-Q, live resize and smooth-scroll
  checks; a ThreadSanitizer run.

Consumers
: The conformance consumers are rebased once, after the STXlib swap-in:
  template_harness, SwiftEdit, PlaySuite, Plan Paint and Pocket Library.
  Product pins do not move before then.

The order is: fold checkpoint 32, then 33 (the font pack, the grid engine,
the warning cleanup); build the demonstrators and seal the drawing ABI; the
STXlib swap-in; the Mac 1.0 conditions; the conformance rebases; then the
hand-back. On the Windows side, the hand-back covers the C# package and
NuGet, Visual Studio integration, the Windows normative traces, the
Windows 11 border shaping of the window band, the Windows part of the
hosted mode and the 1.0 seal.

### Owner decisions that shape the plan

The owner recorded these choices on 2026-11-13:

- **Interface and content fonts.** Interface text uses only the families
  GUI.Forms bundles, and programs cannot register fonts with its engine.
  Content may use any font, drawn by the program's own text engine into its
  own sub-surfaces. Registered families through themes wait for a formally
  verified FreeType, after 1.0 (decided while GUI.Forms read fonts with
  FreeType; since 2026-11-16 STXlib's glyph library reads them).
- **The content boundary is an integrity boundary,** not a security
  boundary: failing content cannot crash or corrupt the interface, which
  stays responsive. Plan Paint's text tool is the first user of the content
  editing mode.
- **STXlib before 1.0.** Release builds of GUI.Forms without exceptions,
  formal verification and freestanding builds come after 1.0 (GUI.Forms
  2.0). A native C++ callback that throws into GUI.Forms is caught where it
  is invoked, recorded, and ends the process through the fail-fast report.
- **Locale from the operating system.** GUI.Forms takes dates, times and
  number formats from the host where it can and keeps no culture tables of
  its own.
- **Drawing.** GUI.Drawing keeps its bold and oblique synthesis and gains a
  path type with curves. GUI.Drawing may still reach host faces for a
  program's own drawing; the interface engine never sees them.
- **The window band and control border in the C ABI,** as additions.
- **3D plotting (D5)** uses the 3D engine of the Utah teapot demonstrator,
  which the owner licensed under MIT.
- **Conformance** is the full suite plus SwiftEdit, PlaySuite, Plan Paint
  and Pocket Library.
- **File Manager** is its own project with its own seat. GUI.Forms provides
  the file-picker interface it needs.
- **Emscripten** needs a support shim and small behaviour changes; it is
  planned separately and demonstrated with Plan Paint.
- **The theme vocabulary** is sufficient for most purposes; terms are added
  as needed.

## Tools (after 1.0 backend)

The visual tools are closed source and separate from the library, which
stays MIT. Binaries come from the website. There is no nag screen and no
splash: each tool shows the line "MALKUTH SOFTWARE CONSULTING" in its title
bar and one line along the top. The licence is freeware, single seat. The
tools are:

- the theme editor, which captures every control for reference and lets
  you click a control to see and edit the rules that affect it (started);
- the WYSIWYG designer, with flex and grid snapping and in-line theme
  overrides;
- Visual Studio integration for C++ and C#;
- the Windows Forms conversion program;
- the mirrored-mode launcher, which runs an existing Windows Forms
  program on GUI.Forms without recompiling it, through a Windows Forms
  facade over the C ABI that logs what is not yet supported. It is in
  progress: its proof of concept runs SDR#, the software-defined radio
  receiver, with a live radio.

Then a cleanup round, documentation and tutorials, and reproducible,
signed packaging lead to **1.0 Stable**.

After 1.0:
- HTML import (converting static web pages into GUI.Forms documents);
- a light FreeType fork, and registered font families through themes once
  FreeType is formally verified (planned while GUI.Forms read fonts with
  FreeType; since 2026-11-16 STXlib's glyph library reads them);
- identical floating-point results on every architecture;
- a proposal, timing open: once STXlib runs on FreeBSD, build and test the
  X11 host there so that GUI.Forms can claim BSD support.

GUI.Forms 2.0 is planned for release builds without exceptions, formal
verification from STXlib up through GUI.Forms, freestanding builds, and a
FreeBSD appliance whose supervisor is a GUI.Forms application.

## 1.0 Stable

The API is declared stable only when all of the following hold:

- The C function tables and error model pass a breaking-change audit, and the
  C++ wrappers are RAII conveniences over them rather than a second contract.
- Two independent native consumers pass the ownership, thread and callback
  conformance tests against the installed SDK. The owner chose the full
  suite as that condition, with SwiftEdit, PlaySuite, Plan Paint and Pocket
  Library as the consumers rebased onto the release.
- A machine-readable manifest lists every control and member as supported,
  deferred or excluded, and the documentation is generated from it.
- Windows, macOS and the X11 host pass the same normative traces,
  including lifecycle, input order, focus and paint receipts.
- Native accessibility publication of the semantic tree exists on every
  host and is exercised by a test, not only the deterministic snapshot.
- Packaging is reproducible: licences, integrity metadata and a software bill
  of materials ship with every artefact.

The full plan, with a measured gate for each stage, is
[`planning/FORWARD_PROGRAMME.md`](https://github.com/malkuth-software/notes/blob/gui_forms/planning/FORWARD_PROGRAMME.md).

Future note, not a stage: distribution near 1.0 is expected to move to
rolling releases with pinnable caches, likely hosted off GitHub.

## Milestones

The implementation programme is a chain of independently testable contracts.
The table lists them in dependency order, with the paint-pipeline closure
gates as their own rows. "Done" means the deliverable exists and a test in
`tests/` exercises it; a row's open remainder is named in its status.

| Milestone | What it delivers | Status |
|---|---|---|
| Retained kernel and headless reference host | Component ownership apart from the visual tree, stable identity, transactional attach and detach, tokenized events, UI-thread enforcement, typed dirtiness, and a deterministic headless host whose trace is the ordering reference | done |
| Display chunks, damage and the CPU renderer | Retained display chunks, bounded multi-rectangle damage per paint plane, a bounded PNG registry with a fuzz target, the renderer-free drawing core (paths, gradients, transforms, clips, images), and the private CPU-only Skia adapter behind symbol audits | done |
| Portable host protocol and the macOS host | A versioned, capability-reported protocol; on AppKit: lifecycle and close cancellation, monitors, occlusion, capture, cursors, clipboard text and images, inbound typed drops, the five common dialogs, sound cues and IME composition | done; outbound drag and native menus are open |
| Unicode text, shaping and editing | A strict UTF-8 text store with distinct position types, Unicode grapheme boundaries, STXlib's shaping over bundled faces with script fallback, bidirectional analysis, and single and multiline editing with selection, clipboard and undo | in progress: preedit on X11 remains |
| Layout, scrolling and virtualization | Measure and arrange with Dock/Anchor, flow, table, split and responsive-track containers; scrollable containers with real scrollbars; read barriers and bounded passes; lists and trees that realize only visible rows | in progress: FlexPanel, GridPanel and BlockPanel pass a box and text gate against a browser layout oracle (211 fixtures); VirtualRowsView and TreeListView index row heights with `stx_btree` (fixed-point heights); DataGridView virtual mode is tested with a million rows; DataGridView on the same row engine is in checkpoint 33 |
| Basic Forms controls | Panel, GroupBox, Label, LinkLabel, Button, CheckBox, RadioButton, TextBox, ListBox, ComboBox, NumericUpDown, scroll bars, TrackBar, ProgressBar, PictureBox, ContainerControl, UserControl, Timer, ToolTip, ErrorProvider, HelpProvider, ImageList and commands, with initialization scopes, mnemonics and accept/cancel targets | done |
| Collection, command and data controls | TreeView, ObjectView, CheckedListBox, TabControl, SplitContainer, MasterDetailView, MenuStrip, ContextMenu, CommandBar, DateTimePicker and a metadata-driven PropertyGrid; `Value<T>` and `Command` bindings with record currency and validation | in progress: DataGridView (with link and sparkline columns), ListView, StatusStrip and MessageBox are implemented; menu first-letter jumps and the DataGridView breadth (frozen columns and rows, click events, cell tooltips, hit testing) are in checkpoint 32; MDI (with docking) is open |
| Semantic tree | A retained semantic graph with stable-ID snapshots, default adapters for stock controls, virtual children and semantic actions, independent of authored metadata | done |
| Native accessibility on every host | Publication of the semantic tree through each platform's accessibility API, exercised by tests | done: macOS (NSAccessibility), Windows (MSAA and UI Automation) and Linux (ATK/AT-SPI) publish the tree and editable text ranges, client tests read each through its own API, and every native host advertises the capability |
| Themes, assets and localization | Immutable theme records with named visual roles and high-contrast recipes, atomic theme replacement, layered surface materials, OKLab/OKLCH colour and a fixed bundled font set | in progress: the theme language is complete (packages, imports, user themes, effects), with the GUI.Forms theme and four familiar themes whose gate records are open; language files carry each control's translatable text and help records; the industrial theme and the GOST paint palette ship as Gallery packages; the font family registry and the font pack are in checkpoints 32 and 33 |
| Compositor cooperation | Transparency, glass and behind-window materials where the host provides them, joined to the theme engine | in progress: the Windows, macOS and X11 adapters, the XWayland probe, window backdrop chains, full-fidelity glass and reflection are done; some host rows of the stage gate are open |
| Authoring manifest for compiled forms | A versioned manifest of controls, properties, events, layout and resources that form compilers consume, with conformance fixtures | in progress: the control registry lists every public control type, and the member manifest (`manifests/members.json`) classifies every property, event and method against Windows Forms; conformance fixtures are open |
| Stable C ABI | Versioned function tables and an error model that pass a breaking-change audit, an export allowlist, and C++ wrappers as RAII conveniences over them | in progress: ABI 1.3 is installed (1.1 the seal, 1.2 and 1.3 additions only, including the class tables), with a blocking audit against the recorded baselines, a conformance kit, export lists and generated C# bindings; the drawing ABI (`gd_*`, 0.3 on `main`) is sealed after the demonstrators |
| Windows and Linux hosts | The same protocol on Win32 (DIB presentation, MSAA, dialogs, clipboard, automation channel) and X11 (XDND drops, ATK/AT-SPI, dialogs, clipboard) | in progress: Windows lacks inbound drag and drop and occlusion; X11 lacks preedit |
| Packaging and release | A pipeline that builds, tests, packages and launch-tests every program on every platform, then publishes archives, installers, checksums and licence notices | in progress: release signing and notarization, a software bill of materials and reproducible builds remain |
| Coherent latest-state presentation | Separate content, rendered and presented revisions; one paint lease, one coalesced wake and one deferred follow-up per window; exact presentation receipts; owner-callback failure restores the previous chunks | done |
| Pressure, input and stall conformance | Under 100 mutations per frame, nested paints, resize during replay, occlusion, backend faults and owner retirement: one queued drain, latest-state rendering and a return to idle; input deferred outside the paint lease with a fixed bound | in progress: the deterministic gates pass; a native wall-clock soak remains |
| Atomic candidate raster | Replay into a private candidate raster and publish it only after every backend command succeeds, keeping the last presented raster across allocation, renderer and upload faults | in progress: an opt-in Windows transactional frame store exists; default hosts replay into the live raster |
| Damage-preserving painting | Painting and presenting each damage rectangle instead of their bounding box | done: every host paints the rectangle set (paint contract version 2), and live frames follow one frame sequence; the gate is recorded on Windows, X11 and macOS ([`results/2026-10-25-dp-gate-macos`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-25-dp-gate-macos)), where AppKit copies the union of the set over unchanged pixels, accepted by the owner; paint edges snap to whole device pixels |
| Presentation availability record | Publishing the mandatory paint-contract version and optional host extensions (partial present, zero-copy import, presentation fences) | done: `Window::presentation_availability()` reports contract version 2 (partial present is now part of the contract), the extensions and the live paint lease; no host offers zero-copy import or fences yet |
| API stability (1.0) | The conditions listed under [1.0 Stable](#10-stable) | planned |

## Done and tested

- Retained tree with explicit ownership, stable identity, transactional
  attachment and synchronous disposal.
- Typed dirtiness, nested update scopes, bounded flushes, read barriers.
- Damage-limited CPU painting with coherent paint leases, exact presentation
  receipts, paint planes, live surfaces and per-frame counters.
- One text engine (STXlib's shaper and light auto-hinter, shared sRGB
  blending) with identical output on macOS, Windows and Linux, enforced
  by a hash-compared identity test. Skia is built without any font backend.
- Owned compression and image codecs on every host: one zlib (`stx_zlib`),
  an owned PNG codec (`stx_png`), the only image codec, and CONV
  interpolation (`stx_conv`). CNG (`stx_cng`), taken in for 0.2, was removed
  on 2026-10-26 (the owner's decision: our own PNG and zlib suffice). WIC
  decoding is removed, and FreeType, built without zlib while it was
  used, left on 2026-11-16.
- The theme engine: a cascade of rules over named parts, layered materials,
  effect primitives with declared cost bounds, transitions and settling
  motion, idle-time bakes, and procedural materials, with identical pixels on
  every host and no frames once a window settles.
- Forms-like focus, Tab order, focus scopes, mnemonics, accept/cancel
  targets, validation, pointer capture, deferred input during paint.
- One UI thread per window, a bounded FIFO dispatcher, synchronous `invoke`,
  timers, cooperative workers and cancellation.
- A portable host protocol with capability discovery; AppKit, Win32 and X11
  adapters plus a deterministic headless adapter that defines the reference
  trace.
- Stock controls: panels and groups, labels and links, buttons, check and
  radio, text boxes (single and multiline with scrollbars), list, checked
  list, combo, tree, tab control, track bar, progress bar, numeric up/down,
  date/time picker, property grid, picture box, image list, menus and context
  menus, tooltips, error and help providers, split and master/detail
  containers, flow and table layout, command bar, choice group, expandable
  sections, breadcrumb trail, object and correspondence views, raster
  canvas, drawing surface and diagnostic views.
- Application-owned `Value<T>` and `Command` state with two-way control
  bindings, record currency and binding-aware validation.
- An installed CMake SDK consumed by standalone examples on every CI platform,
  with generation-time rejection of unsafe linking combinations.
- A compiler-cache pipeline that publishes tested object caches per
  platform, with pinned revisions that pruning keeps for consumers.
- This documentation site, with control indexes and an API reference
  generated from the control registry and the public headers, rebuilt on
  Read the Docs only after CI passes on main or on a release tag.
- Merges to main go through a merge queue whose required checks include a
  state-machine gate that fails on findings.
- The host protocol versions are one table in the public header, now at
  version 13.
- Flex, grid and block layout panels and a `TextBlock` leaf, checked against
  a browser layout oracle, and the Windows Forms containers with full
  right-to-left mirroring.
- Damage-preserving painting on every host (paint contract version 2).
- The theme language (CSS syntax over named parts), with packages, user
  themes and effects, and the GUI.Forms and familiar themes written in it.
- Form documents that generate bound window classes, window rules, the
  application object, Popup and Splash windows, and properties as cells.
- Compositor adapters on Windows, macOS and X11, with full-fidelity glass.
- `stx_rtf2md`, the first-party rich-text library: one document model, a
  markdown-like dialect, and RTF read and written by meaning.
- `stx_skia/`, the CPU renderer (from the pinned stx checkout), built with the tree on every host.
- The C ABI at version 1.3, with class tables for C and C# overrides, a
  blocking breaking-change audit and generated C# bindings.
- VirtualRowsView, TreeListView virtual rows, DragTracker, NodeCanvas,
  paragraph layout, ImageAtlas, allocation statistics and IndicatorLamp.
- Three demonstration programs (the Gallery and the Explore and
  Correspondence previews) on macOS, Windows and Linux, and a release
  pipeline that builds, tests, packages and launch-tests each of them before
  publishing archives, installers and checksums.

## Open work by area

The current work is under
[1.0 (backend): in progress](#10-backend-in-progress). By area, what is
still open:

Documentation
: Reviewed descriptions for the public members the API reference still lists
  by signature only, and screenshots captured by CI rather than by hand.

Hosts
: Input-method preedit on X11, inbound drag and drop on Windows, outbound
  drag on every host, and the locale read from the host (checkpoint 32;
  Windows with the hand-back).

Compositor cooperation
: The stage gate's remaining rows: the Windows fallback and settings rows,
  X11 with and without a compositing manager, and the macOS light, dark
  and reduced-transparency rows.

Windows Forms controls
: MDI with the docking work, the slow-click label edit, the remaining
  ListView and TabControl gaps, and the DataGridView members still deferred
  (cell mouse events, `EditingControlShowing`, the selected-cell
  collections and others).

## Not planned

GPU rendering of any kind; a browser engine or an HTML runtime (themes use a
CSS syntax, and HTML import comes after 1.0, but nothing renders a web page); a .NET,
Java or Godot dependency in the native runtime; immediate-mode controls or a
perpetual redraw loop; host font discovery for control text; LCD subpixel
text; font registration by programs for interface text; ActiveX, browser
hosting, device printing and obsolete Forms families.

## Linux and other Unix desktops

GUI.Forms aims to be agnostic about the display server and to target what
already works everywhere. Today that is the X11 host: it runs on X11 desktops
and, through XWayland, in Wayland sessions, with the same behaviour. A
Wayland-native adapter is not planned. FreeBSD is a desired target; it has
not yet been built or tested. After 1.0, once STXlib runs on FreeBSD, the
plan is to build and test the X11 host there.

## Known gaps

Outbound drag initiation on every host, and inbound drag and drop on Windows;
preedit composition on X11; occlusion notifications on Windows; native
menu-bar publication; per-monitor DPI policy as
a host service beyond scale notifications; designer serialization. Linux already publishes accessibility through ATK/AT-SPI, and
table cells already align and stretch children through Dock and Anchor. The
detailed planning records may describe earlier states; check the current
implementation and tests when assessing a remaining gap.
