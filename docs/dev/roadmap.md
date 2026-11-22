# Roadmap

GUI.Forms is pre-1.0. This page says what is done, what is being worked on
now, and what has to be true before 1.0 is declared. It is a
summary of the planning documents in the [`planning/`](https://github.com/malkuth-software/notes/tree/gui_forms/planning) directory of the project's notes repository,
which remain the detailed source; where they disagree with the code, the code
and its tests are the authority.

```{image} ../_static/roadmap.svg
:alt: Releases toward 1.0: 0.1 Shipped, 0.2 Verified, 0.3 Themeable, 0.4 Flexible and 0.5 ABI seal are done; the current series, the backend, is in progress; 1.0 is next; the tools follow as 1.x releases.
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

## Now: the backend, toward 1.0 (in progress)

The current series is the library, its C ABI and its C# bindings, brought
to the point where 1.0 can be declared. Builds published from `main` carry
the package version, now {{ gf_version }}. Work lands in checkpoints:
several changes are folded into one tree, built once and gated (the whole
test suite, the sanitized tree and a libstdc++ compile of every changed
file), then merged as one pull request.

### Done since the seal

On `main` since the 0.5 seal; [Unreleased](../release/unreleased) lists
every change and the breaking ones.

Text and fonts
: STXlib's glyph library reads, shapes and hints every face, and its text
  layout handles bidirectional text, line breaks and scripts; FreeType,
  HarfBuzz, SheenBidi, libunibreak and MD4C are gone. Every bundled face is
  compiled into the library as one font pack, so the interface faces cannot
  be missing, with smaller Chinese, Japanese, Korean, Arabic and Indic
  fallback subsets. A registry names the bundled families (`FontFamilyId`),
  with family stacks and nearest faces. Programs can load their own font
  families for composites and sub-surfaces
  ([Application font families](../user/app-fonts)). TextBox takes styled
  text runs, and paragraph layout gives glyph runs to draw.

Content and hosting
: Content surfaces run on their own execution context, with stale frames, a
  watchdog and termination. In the content editing mode the program lays out
  and draws the text, and GUI.Forms supplies the caret, input, undo and
  accessibility. Hosted content runs the content program in its own process,
  on macOS, Linux and Windows ([Content surfaces](../user/content-surfaces)).

Windows, input and presentation
: Window properties in the Windows Forms shape, modal and owned windows and
  anchored overlays; the window band's per-edge reservations, colour roles,
  cycling and textures, and window shaping
  ([The window band](../user/window-band)); a business card that shows the
  program's identity, and a diagnostic overlay with a counters card;
  relative pointer mode ([Relative pointer mode](../user/relative-pointer));
  key events in the Windows Forms shape and focus that returns where it
  was; a new window that stays off screen until its first frame is painted,
  on macOS and X11; one presentation layer through which each host presents
  and moves scrolled pixels ([Presentation](../user/presentation)); modal
  loops that wake for work scheduled outside them; click-through on macOS;
  and the user's locale read from the host on all three hosts.

Controls
: The final controls (seven-segment display, linear and radial gauges,
  dial, step indicator, wizard, callout, modal layer, lookup combo box and
  About dialog); the help system (HelpBook, its viewer, search, F1 and
  What's This); VirtualRowsView, with TreeListView and DataGridView on the
  same row engine; DataGridView's frozen rows and columns, click events,
  cell tooltips and hit testing; menu first-letter jumps and file dialog
  checks; the ribbon's minimized popup, Quick Access Toolbar and help
  button; IndicatorLamp, the danger key and the public NodeCanvas; the plots
  package and 3D plotting (Plot3DView); stock icons from Heroicons; and
  composite controls written as `.control` documents
  ([Forms](../user/forms)).

Themes and drawing
: `gui-forms` as the default theme and theme sheets loaded at run time;
  custom states in selectors and the canvas theme; industrial surfaces
  (paint finishes, baked light planes, the GOST palette); content effects
  (filters, masks, blend modes, backdrop filters and colour stages) on a
  control and its descendants; linear-light and dithered gradients; curved
  paths, trimmed strokes, text at any angle and shaded triangles in the
  Painter and the drawing ABI.

Forms and the member manifest
: Form documents are XML (`urn:gui-forms:form:1`), and
  `tools/convert_form_to_xml.py` converts the old syntax once. The member
  manifest (`manifests/members.json`) lists every control member against
  Windows Forms. Controls raise per-event notifications in the Windows
  Forms shape, `right_to_left` is the visible spelling, and alignments that
  follow the reading direction are spelled `start` and `end`, as in CSS.

C ABI and C#
: The C ABI grows by additions only, each checked by a blocking
  breaking-change audit: the window, focus, decoration, overlay, data grid,
  docking and font surfaces; class tables, so C and C# code can override a
  control's hooks; the content boundary; then the window band and shaping,
  the pointer location, the list view table, failure detail, composites,
  the business card and diagnostic overlay, relative pointer mode,
  application fonts and the presentation capabilities. It is now version
  {{ gf_c_abi }}. The drawing ABI (`gd_*`) was sealed on 2026-11-13 and is
  now version {{ gf_drawing_abi }}; the host protocol is at version
  {{ gf_host_protocol }}.

Library and build
: STXlib, pinned with the stx packages in `stx.lock`, supplies the row
  height index and owned types at the boundaries; public signatures move
  from `std::optional` to `stxlib::Optional` and the other STXlib types.
  Control headers are flat, the shared libraries export exactly what
  `GUI_FORMS_API` marks, and `import gui_forms;` is a C++20 module. A
  program is one module: it links GUI.Forms statically, or loads only the C
  ABI library. The tests live in their own repository,
  `malkuth-software/gui_forms_tests`, and build against a test cache CI
  publishes for each commit; CI builds with warnings as errors.

Each build that passes CI on `main` is published on the
[Releases page](https://github.com/malkuth-software/gui_forms/releases).

### Before 1.0

These steps run in order; each finishes before the next starts.

1. **Finish and integrate the open work.** On Windows: the C# package and
   NuGet, the Windows normative traces and the Windows 11 border of the
   window band. Across the library: the rows under
   [Open work by area](#open-work-by-area), the remaining Windows Forms
   behaviour gaps, the move to STXlib's owned types at the public surface,
   one canonical image interpolation in `stx_conv`, and two studies, a
   guarantee that a window's content always fits the area it is shown in
   and behavioural constraints checked by a block fuzzer. Alongside,
   controls and tools from other frameworks are reviewed for what
   GUI.Forms lacks.
2. **Automation through the Model Context Protocol.** Every GUI.Forms
   program can offer its interface to an assistant or a test tool, which
   reads and drives it.
3. **Everything else before 1.0:** touch input, building for Apple
   platforms without a Mac, signed files in STXlib, and the tools brought
   forward from after 1.0.
4. **Declare 1.0** once the [conditions for 1.0](#conditions-for-10) hold,
   after a cleanup round across the tree and signed, reproducible packaging.

After the declaration, the documentation is reviewed against the release
and embedded in the package, with pages showing GUI.Forms in use in a
range of programs.

### Owner decisions that shape the plan

The owner recorded these choices on 2026-11-13:

- **Interface and content fonts.** Interface text uses only the families
  GUI.Forms bundles; stock controls and themes never draw anything else.
  Since 2026-11-15 a program may load its own font families for its
  composites and sub-surfaces ([Application font families](../user/app-fonts)).
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

## Conditions for 1.0

1.0 is declared, and the API with it, only when all of the following hold:

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

## After 1.0: the tools and beyond

The visual tools follow 1.0 as 1.x releases; any brought forward come
before it. They are closed source and separate from the library, which
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

Also after 1.0:

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

## Milestones

The implementation programme is a chain of independently testable contracts.
The table lists them in dependency order, with the paint-pipeline closure
gates as their own rows. "Done" means the deliverable exists and a test in
the tests repository (`malkuth-software/gui_forms_tests`) exercises it; a
row's open remainder is named in its status.

| Milestone | What it delivers | Status |
|---|---|---|
| Retained kernel and headless reference host | Component ownership apart from the visual tree, stable identity, transactional attach and detach, tokenized events, UI-thread enforcement, typed dirtiness, and a deterministic headless host whose trace is the ordering reference | done |
| Display chunks, damage and the CPU renderer | Retained display chunks, bounded multi-rectangle damage per paint plane, a bounded PNG registry with a fuzz target, the renderer-free drawing core (paths, gradients, transforms, clips, images), and the private CPU-only Skia adapter behind symbol audits | done |
| Portable host protocol and the macOS host | A versioned, capability-reported protocol; on AppKit: lifecycle and close cancellation, monitors, occlusion, capture, cursors, clipboard text and images, inbound typed drops, the five common dialogs, sound cues and IME composition | done; outbound drag and native menus are open |
| Unicode text, shaping and editing | A strict UTF-8 text store with distinct position types, Unicode grapheme boundaries, STXlib's shaping over bundled faces with script fallback, bidirectional analysis, and single and multiline editing with selection, clipboard and undo | in progress: preedit on X11 remains |
| Layout, scrolling and virtualization | Measure and arrange with Dock/Anchor, flow, table, split and responsive-track containers; scrollable containers with real scrollbars; read barriers and bounded passes; lists and trees that realize only visible rows | done: FlexPanel, GridPanel and BlockPanel pass a box and text gate against a browser layout oracle; VirtualRowsView, TreeListView and DataGridView share one row engine, with row heights indexed by STXlib's `stx_btree` (fixed-point heights); DataGridView virtual mode is tested with a million rows |
| Basic Forms controls | Panel, GroupBox, Label, LinkLabel, Button, CheckBox, RadioButton, TextBox, ListBox, ComboBox, NumericUpDown, scroll bars, TrackBar, ProgressBar, PictureBox, ContainerControl, UserControl, Timer, ToolTip, ErrorProvider, HelpProvider, ImageList and commands, with initialization scopes, mnemonics and accept/cancel targets | done |
| Collection, command and data controls | TreeView, ObjectView, CheckedListBox, TabControl, SplitContainer, MasterDetailView, MenuStrip, ContextMenu, CommandBar, DateTimePicker and a metadata-driven PropertyGrid; `Value<T>` and `Command` bindings with record currency and validation | in progress: DataGridView (with link and sparkline columns), ListView, StatusStrip and MessageBox are implemented; menu first-letter jumps and the DataGridView breadth (frozen columns and rows, click events, cell tooltips, hit testing) are done; MDI (with docking) is open |
| Semantic tree | A retained semantic graph with stable-ID snapshots, default adapters for stock controls, virtual children and semantic actions, independent of authored metadata | done |
| Native accessibility on every host | Publication of the semantic tree through each platform's accessibility API, exercised by tests | done: macOS (NSAccessibility), Windows (MSAA and UI Automation) and Linux (ATK/AT-SPI) publish the tree and editable text ranges, client tests read each through its own API, and every native host advertises the capability |
| Themes, assets and localization | Immutable theme records with named visual roles and high-contrast recipes, atomic theme replacement, layered surface materials, OKLab/OKLCH colour and a fixed bundled font set | in progress: the theme language is complete (packages, imports, user themes, effects), with the GUI.Forms theme and four familiar themes whose gate records are open; language files carry each control's translatable text and help records; the industrial theme and the GOST paint palette ship as Gallery packages; the font family registry and the embedded font pack are done |
| Compositor cooperation | Transparency, glass and behind-window materials where the host provides them, joined to the theme engine | in progress: the Windows, macOS and X11 adapters, the XWayland probe, window backdrop chains, full-fidelity glass and reflection are done; some host rows of the stage gate are open |
| Authoring manifest for compiled forms | A versioned manifest of controls, properties, events, layout and resources that form compilers consume, with conformance fixtures | in progress: the control registry lists every public control type, and the member manifest (`manifests/members.json`) lists every property, event and method against Windows Forms; many members are not yet classified as supported, deferred or excluded ([Members](../controls/generated/members)), and 1.0 needs every one classified; conformance fixtures are open |
| Stable C ABI | Versioned function tables and an error model that pass a breaking-change audit, an export allowlist, and C++ wrappers as RAII conveniences over them | in progress: ABI {{ gf_c_abi }} is installed (1.1 the seal, every later version additions only), with a blocking audit against the recorded baselines, a conformance kit, export lists and generated C# bindings; the drawing ABI (`gd_*`) is sealed, now version {{ gf_drawing_abi }} |
| Windows and Linux hosts | The same protocol on Win32 (DIB presentation, MSAA, dialogs, clipboard, automation channel) and X11 (XDND drops, ATK/AT-SPI, dialogs, clipboard) | in progress: Windows lacks inbound drag and drop and occlusion; X11 lacks preedit |
| Packaging and release | A pipeline that builds, tests, packages and launch-tests every program on every platform, then publishes archives, installers, checksums and licence notices | in progress: release signing and notarization, a software bill of materials and reproducible builds remain |
| Coherent latest-state presentation | Separate content, rendered and presented revisions; one paint lease, one coalesced wake and one deferred follow-up per window; exact presentation receipts; owner-callback failure restores the previous chunks | done |
| Pressure, input and stall conformance | Under 100 mutations per frame, nested paints, resize during replay, occlusion, backend faults and owner retirement: one queued drain, latest-state rendering and a return to idle; input deferred outside the paint lease with a fixed bound | in progress: the deterministic gates pass; a native wall-clock soak remains |
| Atomic candidate raster | Replay into a private candidate raster and publish it only after every backend command succeeds, keeping the last presented raster across allocation, renderer and upload faults | in progress: an opt-in Windows transactional frame store exists; default hosts replay into the live raster |
| Damage-preserving painting | Painting and presenting each damage rectangle instead of their bounding box | done: every host paints the rectangle set (paint contract version 2), and live frames follow one frame sequence; the gate is recorded on Windows, X11 and macOS ([`results/2026-10-25-dp-gate-macos`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-25-dp-gate-macos)), where AppKit copies the union of the set over unchanged pixels, accepted by the owner; paint edges snap to whole device pixels |
| Presentation availability record | Publishing the mandatory paint-contract version and optional host extensions (partial present, zero-copy import, presentation fences) | done: `Window::presentation_availability()` reports contract version 2 (partial present is now part of the contract), the extensions and the live paint lease; no host offers zero-copy import or fences yet |
| API stability (1.0) | The conditions listed under [Conditions for 1.0](#conditions-for-10) | planned |

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
  version {{ gf_host_protocol }}.
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
- The C ABI at version {{ gf_c_abi }}, with class tables for C and C#
  overrides, a blocking breaking-change audit and generated C# bindings;
  the sealed drawing ABI (`gd_*`) inside the same library.
- Content surfaces, the content editing mode and hosted content on macOS,
  Linux and Windows; composite controls; application font families;
  relative pointer mode; the business card and the diagnostic overlay.
- Every bundled face in one embedded font pack, read by STXlib's glyph
  library and laid out by its text layout.
- Form documents in XML, with a one-time converter for the old syntax.
- One presentation layer on every host, and a first frame painted before a
  new window shows on macOS and X11.
- VirtualRowsView, TreeListView virtual rows, DragTracker, NodeCanvas,
  paragraph layout, ImageAtlas, allocation statistics and IndicatorLamp.
- Three demonstration programs (the Gallery and the Explore and
  Correspondence previews) on macOS, Windows and Linux, and a release
  pipeline that builds, tests, packages and launch-tests each of them before
  publishing archives, installers and checksums.

## Open work by area

The current work is under
[Now: the backend, toward 1.0](#now-the-backend-toward-10-in-progress). By
area, what is still open:

Documentation
: Reviewed descriptions for the controls and members the API reference
  still lists by signature only; a preview of every control, captured
  without a screen in the windows-professional theme; and links from the
  reference to the source at the commit it was built from.

Hosts
: Input-method preedit on X11, inbound drag and drop on Windows, and
  outbound drag on every host.

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
and, through XWayland, in Wayland sessions, with the same behaviour. There
is no Wayland-native host; a Wayland shared-memory presentation path is
built only with `GUI_FORMS_ENABLE_WAYLAND_PRESENT`. FreeBSD is a desired target; it has
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
