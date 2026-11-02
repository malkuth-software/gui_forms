<!-- Appended verbatim after the generated header by tools/publish_tested_release.py.
     Keep "Breaking changes" separate from "Other changes"; group "Other changes" by
     control or area; attach one screenshot for anything visual. Rewrite this file for
     each release series; the generated header carries version, commit and platforms.
     The 0.1 series' notes are kept in docs/release/0.1.md. -->

## Focus

0.2 is the "Verified" series. Each item in it has a recorded gate: tests
that measure it on every host it applies to (Windows x64, macOS arm64, Linux
x64 and Linux arm64), and a record under [`results/`](https://github.com/malkuth-software/notes/tree/gui_forms/results) saying what was
measured. The demonstration programs are the same three as in 0.1: the
control Gallery, the fixture-backed Explore preview and the Correspondence
mail mockup.

## Breaking changes

- **The CMake package is 0.2.** Its version file accepts only the same minor
  version, so `find_package(GUIForms 0.1 ...)` no longer finds it. Ask for
  `find_package(GUIForms 0.2 CONFIG ...)`. The shared libraries still carry
  `SOVERSION 0`, and as in 0.1 there is no binary promise between releases:
  rebuild against each new SDK.
- **Images are admitted by format, not only as PNG.**
  `ImageRegistry::load_image` and `replace_image` take an encoded file and
  choose the decoder by its signature; PNG is the only format admitted (CNG,
  admitted in 0.2.0, was removed afterwards). Image resources and their views
  report `ImageMetadata` (width, height, alpha and decoded byte count) in
  place of `PngMetadata`, which only `validate_png` reports now.
  `load_png` and `replace_png` remain, for PNG only.
- **Optional values replace `has_*` flags** where the flag could disagree
  with the value beside it:
  - `ErrorProviderChange::has_error` is gone; it was always
    `!error.empty()`.
  - `ImageAttributesSnapshot::color_matrix` is a `std::optional`, and
    `has_color_matrix` is gone.
  - `HostClipboardImageResult::image` is a `std::optional`, and `has_image`
    is gone.
  - In stx_vorbis, `OggPacket::granule` and `page_granule`, and
    `OggStreamEnd::granule`, are optionals; `has_granule` and
    `page_has_granule` are gone.
- **Removed: active surfaces** (ADR-016). `Window::activate_surface`,
  `Control::on_frame`, `MetricsSnapshot::active_surface_count`,
  `maximum_active_surface_count` and `active_surface_ticks`,
  `FramePollResult::active_surface_ticks`, `maximum_active_surfaces` and
  `minimum_active_surface_interval` are gone. Animate with an
  `AnimationTimer` member: its tick damages what it changed and returns
  `AnimationTickResult`, and it waits for the control's next paint. For work
  that is not drawing, such as a press-and-hold repeat or a poll, use
  `Window::schedule_ui_timer` and stop it yourself. A `schedule_paint`
  deadline now only repaints its control; it calls nothing.
- **Removed:** `Command::set_key_tip` and `CommandState::key_tip`. The ribbon
  has no KeyTips, and nothing else read them.

## Consumer migration

- **TextBox passes Enter and Escape on** (f333fb12, 4f0be17f), as in Windows
  Forms, so the window's accept and cancel buttons, menus, docking and
  containers receive them. A single-line box consumes Enter only while it
  has something to accept (an open suggestion list, an appended completion,
  or an input-method composition). It consumes Escape only while it has
  something to cancel (the same, or a selection drag). Otherwise it raises
  neither `committed` nor `cancelled` for those keys.
  - A box that is an edit session and relied on `committed()` or
    `cancelled()` firing for plain Enter or Escape must opt in:
    `set_accepts_enter(true)` and `set_accepts_escape(true)`. The box then
    raises the event and stops the key there.
  - A multi-line box still takes Enter to insert a newline.
  - Known affected consumers: File Manager had five such boxes, and
    SwiftEdit's CSV editor has the same pattern.
  - The in-tree editors (BreadcrumbTrail, ColorValueEditor, InstrumentRack,
    PropertyList and TokenBox editors, the Linux host's dialog fields and
    the demoboard's search box) already opt in.

## Other changes

### Controls
- Controls are articulated. Each registry control declares its named parts
  and states in one schema (`manifests/control-schema.json`), and its parts
  publish their geometry. A test lays out every Gallery page and reads each
  part's geometry from it. When the gate was recorded, that covered 60 of 60
  controls ([`results/2026-10-13-t1-anatomy-gate`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-13-t1-anatomy-gate)).

### Accessibility
- Verified on every host through the platform's own accessibility API.
  Client tests read the whole Gallery as an assistive technology would and
  compare every control with the semantic tree it publishes: role, name, state,
  value and actions. Each control's default action is performed through the
  same API. Windows is read through MSAA and UI Automation, Linux through
  AT-SPI and macOS through AX. Windows gains a UI Automation provider.
  Records: [`results/2026-10-19-a-w1-msaa-gallery`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-19-a-w1-msaa-gallery),
  [`results/2026-10-20-a-w3-uia-gallery`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-20-a-w3-uia-gallery),
  [`results/2026-10-20-a-l2-linux-gallery`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-20-a-l2-linux-gallery) and [`results/2026-10-20-a-macos`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-20-a-macos).
- Text fields expose editable text ranges on every host (ATK Text, UI
  Automation TextPattern and the AppKit text attributes), each with client
  tests.
- Every native host advertises `HostCapability::accessibility`.

### Fonts
- Families that applications ask for map to bundled faces. The table is
  `assets/fonts/font-substitution.json`:
  - **Metric-compatible:** Calibri to Carlito, Arial to Arimo, Times New
    Roman to Tinos and Courier New to Cousine. Every advance is identical in
    font units, every line break is the same, and line height is within 1 px
    at every measured size.
  - **Nearest:** Microsoft Sans Serif to Arimo, Tahoma, Segoe UI and Segoe UI
    Semibold to Selawik, and Consolas to Portsmouth Mono. The mean line width
    is within 5% of the reference face at every measured size.

  Every pair is measured against the Windows reference face on all four
  hosts ([`results/2026-10-23-f-font-substitution`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-23-f-font-substitution)).
- Four styles pass only under a named, measured exception:
  - Calibri bold italic: 2 of 90 line-break sets differ.
  - Arial bold: one advance differs (U+00B5).
  - Arial bold italic: 4 of 90 line-break sets differ.
  - Tahoma bold: lines are 7.05% shorter at the worst size.
- Arimo, Tinos and Selawik are bundled, under the SIL Open Font License.
- `resolve_font_family` reports how each request was resolved: by the
  family's own name, a substitution, an alias, a generic name, or the
  content family when nothing matches. A host face is used only under its
  own name, never in place of another family.

### Images and compression
- One zlib, owned by the project (stx_zlib), and an owned PNG codec
  (stx_png) on every host. CI checks the link map of every static archive
  the toolkit links, and Skia's, and fails on any other zlib, FreeType's
  bundled gzip or libpng.
- Windows no longer decodes images through WIC, and windowscodecs is no
  longer linked.
- PNG is the only image format, for resources, the clipboard and
  GUI.Drawing alike.
- stx_conv's CONV interpolation prepares the sized variants of image faces:
  enlargements use the CONV interpolant, and reductions use pixel-area
  averaging.
- The compression and image code is free of GPL-licensed code.

### Engine core
- The engine core of the 0.3 theme engine is sealed. On all four hosts,
  144 primitive images and 308 feature-sample images were pixel-identical,
  with no differences between hosts ([`results/2026-10-23-t2-engine-gate`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-23-t2-engine-gate)).
- Baking work is held to an effort budget.
- The idle checks count no frames once a window settles: an idle window
  draws nothing.

### Themes and text masks
- `Theme::create` takes its `ThemeDefinition` by const reference.
  Existing calls still compile, and the definition is copied into the theme.
- `TextMaskLimit` gains `cache_entries`. The mask cache's entry count had
  been reported as `metadata_bytes`.

### Build and CI
- GUI.Forms builds through template_harness. The shared CI tools (reuse,
  compiler caches, the Windows toolchain, the checkers) moved to
  [malkuth-software/template_harness](https://github.com/malkuth-software/template_harness),
  pinned by `harness.lock`; their old `tools/` paths forward to it (see
  docs/building/template-harness.md).

### Release pipeline
- As in 0.1, each program is built, tested, packaged and smoke-launched on
  macOS, Windows and Linux runners before anything is uploaded.
- Each release now also carries the source SDK,
  `gui-forms-sdk-<version>-source.tar.gz`, listed in `SHA256SUMS.txt`. It
  holds the release commit's tree, the pinned HarfBuzz, FreeType,
  libunibreak and miniaudio sources, and `SDK_MANIFEST.json`. Build GUI.Forms
  for your own platform with `tools/build_sdk.py --target <platform> --prefix
  <dir>` (see docs/user/install.md, "The source SDK"). It replaces the four
  per-platform binary SDK archives that briefly accompanied 0.2 releases.
- `tools/build_sdk.py --features all` builds one SDK that serves every
  product. It adds the text masks and the audio loop transport, which now
  install as the `TextMasks` and `AudioLoopTransport` package components
  (`find_package(GUIForms COMPONENTS TextMasks AudioLoopTransport)`). Before,
  installing them was refused. The default set is unchanged.
  It also installs the pinned FreeType and HarfBuzz that the text engine
  links, as static libraries with their headers and licences, exported as
  the `FreeType` and `HarfBuzz` components (`GUIForms::FreeType`,
  `GUIForms::HarfBuzz`), for products that call them directly.

## Known limits

- macOS: the bundle is ad hoc signed (`codesign --sign -`), not Developer ID
  signed or notarized. Gatekeeper asks for confirmation on first launch;
  right-click, Open, or allow it in System Settings, Privacy & Security.
- Windows: the executable and installer are unsigned; SmartScreen shows
  "Windows protected your PC" until the publisher reputation exists. Choose
  More info, Run anyway.
- Linux: X11 is required (XWayland works); the DEB declares X11, Xcursor and
  ATK dependencies.
- Explore (preview) is still a fixture-backed proving slice;
  `file_manager_demoboard/STATUS.md` states what is measured.
- Fonts ship with each program. A family that the substitution table does
  not name is taken from the host only when the host has a face of exactly
  that name; otherwise the bundled content face (Carlito) is used.
