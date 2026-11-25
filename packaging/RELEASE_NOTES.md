<!-- Appended verbatim after the generated header by tools/publish_tested_release.py.
     Keep "Breaking changes" separate from "Other changes"; group "Other changes" by
     control or area; attach one screenshot for anything visual. Rewrite this file for
     each release series; the generated header carries version, commit and platforms.
     Earlier series' notes are kept in docs/release/ (0.1.md to 0.5.md). -->

## Focus

**1.0**: the library, its C ABI and its C# bindings; the tools follow it as
1.x releases. 0.5,
the ABI and API seal, was declared done on 2026-11-08 (see the 0.5 notes).
Everything since builds on the sealed interfaces additively. A change to a
sealed interface needs a recorded owner decision.

The package version is 1.0 (owner, 2026-11-23): release tags are `v1.0.N`
and consumers ask for `find_package(GUIForms 1.0 ...)`. Under the 0.6
number (from 2026-11-12) the series was tagged `v0.6.N`, from `v0.6.1541`
(checkpoints 29 to 31), and under the 0.2 number `v0.2.N`; the stages 0.3
to 0.5 were not tagged separately. Those tags are kept.

These notes cover pull requests #474 to #486.

## Breaking changes

- The package version moves from 0.6.0 to 1.0.0. `find_package(GUIForms
  0.6 ...)` no longer finds it (the version file accepts the same minor
  version only); ask for 1.0. The shared libraries keep `SOVERSION 0`, and
  the C ABI keeps its own version (1.7).

- The package version moves from 0.2.0 to 0.6.0. `find_package(GUIForms
  0.2 ...)` no longer finds it (the version file accepts the same minor
  version only); ask for 0.6. The shared libraries keep `SOVERSION 0`.

- **The default theme is GUI.Forms' own (`gui-forms`).** `default_theme()`,
  `default_theme_base`, `ApplicationConfig`'s default base,
  `ThemeCompileOptions::base` and the C ABI's `themes.reset` all start from
  `gui-forms` (owner decision, 2026-11-08). A theme base of zero in the C
  ABI means the default base. A program that relied on the
  Windows-familiar look names it: `ThemeBase::windows_professional`, or
  `GF_THEME_BASE_WINDOWS_PROFESSIONAL`.
- **`ApplicationWindowOptions::minimum_size` defaults to {0, 0}**, no
  minimum beyond the host's own, as Windows Forms' empty `MinimumSize`.
  `Application::validate` names why it refused
  (`ApplicationResult::refusal`).
- **Flat control headers.** Every control header moved to
  `include/gui_forms/controls/<name>.hpp`, with no forwarding headers;
  `tools/control_header_moves.json` maps the old paths to the new, and
  `tools/flatten_control_headers.py check` finds old spellings. The
  umbrella headers (`basic_controls.hpp` and the others) are unchanged.
- **Lab controls left the installed library.** CorrespondenceView,
  ReviewCard and InstrumentRack moved to the optional `GUIForms::Lab`
  (`lab/include/gui_forms_lab/`, installed with `GUI_FORMS_INSTALL_LAB`);
  EasingPreview, TypographySpecimen and the visual inspector moved to the
  Gallery's own controls and are not installed.
- **Exports.** The shared libraries export exactly what `GUI_FORMS_API`
  marks, through generated export lists (`cmake/exports/`); a symbol
  without the mark is no longer visible to a program.
- **Per-event raisers.** `Control::on_pointer` and `Control::on_key` are
  gone. A control overrides the Windows Forms-shaped raisers instead
  (`on_mouse_down`, `on_key_down`, `on_click` and the others, 30 in all);
  `MouseEventArgs` gains the routed event's fields and `handled`. MouseUp
  is raised before Click, and `suppress_click` and `suppress_key_press`
  follow Windows Forms.
- **`right_to_left` replaces `layout_direction`** as the visible spelling
  (`no`, `yes`, `inherit`, inherited down the tree). A form document that
  writes `layout-direction` is refused with E820, which names the new
  spelling; `tools/migrate_right_to_left.py` rewrites forms, JSON and
  registry writes.
- **The 0.x C ABI is retired.** The sealed C ABI (`gf_abi.h`,
  `GUIForms::CAbi`) is the one C surface; `c_api.h` is gone.

Since pull request #483:

- **Form documents are XML.** A form is an element in the
  `urn:gui-forms:form:1` namespace; each object is an element named by its
  type, with its stable ID in `id`. A document in the old syntax is refused
  (E821, `form_xml_invalid`). `tools/convert_form_to_xml.py` converts a
  document once; `tools/verify_form_conversion.py` checks that the
  generated header is unchanged. See
  [Forms](https://gui-forms.readthedocs.io/en/latest/user/forms.html).
- **FreeType and HarfBuzz are gone.** STXlib's glyph library, text layout
  and Markdown reader replace FreeType, HarfBuzz, SheenBidi, libunibreak
  and MD4C. The package's `FreeType` and `HarfBuzz` components and
  `GUI_FORMS_INSTALL_TEXT_STACK` no longer exist; a program that needs
  either links its own copy.
- **Fonts are compiled in.** Every bundled face is part of `Core` as one
  font pack. No face file is installed or packaged: the `fonts/` folder
  beside a program holds the fonts' licences only, and
  `GUI_FORMS_FONT_DIR` is read only by a build configured with
  `GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY`.
- **STXlib types at the public surface.** Public signatures move from
  `std::optional` to `stxlib::Optional` (for example, `Window::paint`
  returns `stxlib::Optional<PaintReceipt>`), and more standard types follow
  before 1.0. The package finds STXlib (`find_dependency(stxlib)`), which is
  installed with the stx packages.
- **Alignments are spelled `start` and `end`,** as CSS spells them.
  `HorizontalAlignment`, `VerticalAlignment`, `OverlayHorizontalAlignment`
  and GUI.Drawing's `StringAlignment` replace `near` and `far`, and mirror
  under right to left as before; the properties' values in form documents
  and the control schema follow. `DateTimeDropDownAlignment`'s `left` and
  `right` are `start` and `end`, `RibbonCommandSize::small` is `compact`
  and `SplitSeamState::near` is `nearby`. In the C ABI,
  `GF_ANCHORED_OVERLAY_ALIGN_FAR` is `GF_ANCHORED_OVERLAY_ALIGN_END` and
  `GD_STRING_NEAR` and `GD_STRING_FAR` are `GD_STRING_START` and
  `GD_STRING_END`, with their values unchanged.
- **The Ribbon's help event is `help_button_clicked`,** apart from
  `Control::help_requested`, which F1 raises.

Since pull request #484:

- **The tests left this repository.** They live in
  [`malkuth-software/gui_forms_tests`](https://github.com/malkuth-software/gui_forms_tests)
  and build against the test cache CI publishes for each commit
  (`test-cache-<commit>`). This tree registers no tests (only the fuzz
  targets, when fuzzers are enabled), and the test CMake options are gone.
  The C conformance kit's sources are in `src/abi/conformance`.

Visible-area guarantee (stage VA):

- **Windows Forms panels contain what overflows them** (owner decision 4):
  a `Panel`, `GroupBox`, `TabPage`, `UserControl` or `SplitterPanel` without
  `auto_scroll` whose children reach past it now shows them scaled to fit
  (never below `PresentationSettings::minimum_content_scale`, 0.5), where
  Windows Forms clips. Positions read through `absolute_bounds`,
  `point_to_window` and the pointer follow the scale. Set `viewport_fit` to
  `clip` for the old cut. A root larger than its window is contained the
  same way.
- **`absolute_bounds` and the window mapping functions are scaled under a
  contain**: code that adds local lengths to `absolute_bounds()` by hand
  should use `rectangle_to_window` and `point_from_window` (every control
  in the library now does).
- **Host protocol 16** adds `HostSafeInsetsEvent` to `HostEventPayload`; a
  visitor over the payload needs its case.

- **`ScaledPanel` and `ScaledGroupBox` have no `design_size` of their own.**
  They use `Control::design_size`, which every control now has: the same
  name and the same meaning for them, so code and form documents that set
  it keep working. Its default is 0 x 0 (the panels then map from the unit
  square, as their old default did), it refuses a size with one zero side,
  and `ScaledPanelCapabilities`, `ScaledGroupBoxCapabilities` and their
  `DesignSize` capabilities are gone.
- Hosts shrink a primary, standard or tool window whose frame is larger
  than its monitor's work area, unless it covers the whole monitor (full
  screen); a minimum size larger than the work area keeps the window and is
  counted (`HostWindowingSnapshot::unfit_frames`).

## Other changes

### Keys and focus
- **Key events in the Windows Forms shape:** `preview_key_down`,
  `key_down`, `key_press` and `key_up` on controls; `key_down`,
  `key_press`, `key_up` (KeyPreview) and the raw `key_state_changed` on
  windows; `Window::is_key_down`. `KeyEventArgs` carries the Windows Forms
  key code, the physical key and the scan code. See
  [Input and focus](https://gui-forms.readthedocs.io/en/latest/user/input-and-focus.html#key-events).
- **Composite focus:** focusing a combo box, an up-down control, a masked
  box, a token box or a search field puts the caret in its edit part
  (`Control::focus_target`). The composite is the active control.
  `focusable` is a schema property.
- **Focus restore:** a window gives focus back to the control that had it
  when its root is enabled or shown again, when it is reactivated, and when
  a modal window over it closes.

### Windows
- **Window properties:** Windows Forms' window chrome as properties on
  `Window`: `text`, `icon`, `form_border_style`, `minimize_box`,
  `maximize_box`, `control_box`, `window_state`, `top_most`, `opacity`,
  `start_position`, `show_in_taskbar` and `minimum_size`. Each host
  reports what it honours.
- **Modal and owned windows:** `ApplicationWindows::show_modal` runs a
  nested loop until the window closes, as `Form.ShowDialog` does, and gives
  back its dialog result. Owned non-modal windows and anchored overlays
  (menus and drop-downs) open over the running window.
- **Files the platform asks to open:** `Application::take_open_files` and
  `open_files_requested`. macOS hands over documents opened from the Finder
  (double-click, a drop on the Dock icon, Open With). Windows and Linux
  pass them on the command line, as before.
- **Linux file dialogs** match filters regardless of case.

### Text
- **Styled text runs in TextBox:** decoration layers over a multiline
  box's text, for syntax highlighting, diagnostics and search matches.
  Each layer gives byte ranges a theme ink or a colour, a weight, italics,
  an underline or squiggle, and a background. They follow every edit and
  undo. A colour change repaints only the affected rows. See
  [Styled text](https://gui-forms.readthedocs.io/en/latest/user/styled-text.html).

### Themes and painting
- **Allocation-free unkept parts:** a part the engine does not keep (a page
  strip's or tab control's tabs, for example) no longer allocates on every
  paint. Forty tabs under the dark variant paint with no allocation.

### Library
- **Public SHA-256** (`gui_forms/integrity/sha256/sha256.hpp`): a
  one-shot digest, a streaming `Sha256Hasher` and hex writers. It is the
  same digest the theme package compiler checks.

### C ABI (additive over 1.1)
- `windows.key_state`, key interactions and payloads on controls and
  windows, and NumericUpDown's minimum, maximum, increment and decimal
  places as schema properties.
- `windows.show_modal`, `windows.show_owned`, `overlays.open_anchored` and
  `overlays.close_anchored`.
- Window properties through `properties.get`, `set` and `reset` on a
  window (schema component type `window`).
- `controls.focus`, `controls.focus_state` and `windows.focused_control`.
- Five text-editing slots for styled runs (`set_decorations`,
  `replace_decorations`, `clear_decorations`, `decoration_count`,
  `decoration_at`).

### macOS
- Native readback probes for window chrome, modal windows, composite focus
  and opened files. A termination fixture covers the quit, logout and
  restart checks.

### Checkpoint 26 (pull request #476)
- The data grid in the C ABI.
- The ribbon gaps: its drop-down menus and Backstage take the ribbon's
  theme.

### Checkpoint 27 (pull request #480)
- The look integration's rows: overlapping folder tabs with a shadow,
  dither hover, scale-1 faces, ribbon labels and row stacks, field hit
  areas, check-box sizing and label clipping.
- The final controls: SevenSegmentDisplay, LinearGauge and RadialGauge,
  Dial, StepIndicator, Wizard, Callout, ModalLayer, LookupComboBox and
  AboutDialog, with new arc, needle, beak and scrim theme parts.
- The help system: help lines, help bodies and keywords in each control's
  text record, stand-alone topics, HelpBook and its CHM-shaped viewer and
  window, search, F1 and What's This, and a print layout.
- The declarative drag engine (Splitter and SplitContainer on it), the
  Blender-style value bar, the new drawing primitives (batch and polyline
  drawing, a conic gradient) and the plots package (PlotView, its items and
  Sparkline).
- The invalidation overlay: areas presented again are outlined for 300 ms,
  coloured by cause (environment variable, C++, C ABI and a Gallery
  switch).
- Tooltip rules: a blank tooltip never shows, none shows under an open
  menu, and programs and windows can switch them off; tooltips bake on a
  worker thread.
- The automation channel is off unless a program asks for it.
- Themes: application colour roles, a build-time theme check and window
  capture.
- FlexPanel: a reversed axis opens on its main-axis start, and the
  auto-scroll extent covers all wrapped content; the layout panels take
  scroll properties in the form language.
- Docking: floating panels shown while inactive, side restrictions, a close
  button, SDI mode, hover and the area colour; the C ABI docking table
  (`GF_TABLE_DOCKING`) with stable panel ids and floating windows owned by
  the main window.
- The C ABI host gives each `show_dialog` request an identity; the
  conformance kit edits text through an editable combo box; Button paints
  the flat visual style.
- Menus open in Popup windows with the menu frame and the theme's look.
- Windows: canvas panning replays occluded content under opaque live
  surfaces and reuses cached moved shadows, horizontal wheel input
  (`WM_MOUSEHWHEEL`); each damaged band is presented as a whole-height
  top-down DIB.

### Checkpoint 28 (pull request #481)
- **C ABI 1.2** (owner decision D9): the window, focus, decoration,
  anchored overlay, data grid, docking and font surfaces, additions only;
  custom-control layout callbacks; a data-provider binding (schema,
  provider, column store, sorted and filtered views, and the C#
  `BindingListProvider`); the help table.
- **Cost:** the window tree's paint cost falls from about 782,000 to about
  30,000 ns per control in the tree-cost benchmark; kept text layouts;
  content scroll copies (`Control::scroll_content`, RasterCanvas pans).
- TreeListView shows virtual rows over a height-indexed tree.
- **Themes:** every program takes a theme from its command line
  (`--theme`, `GUI_FORMS_THEME_SHEET`), and a plain sheet loads at run time
  (`themes.load_sheet` in the C ABI); the windows-professional theme is
  redone.
- **Failure:** native entry points fail fast through one boundary that
  reports the site, the exception and a stack; housekeeping keeps counters
  against named expectations instead (`gui-forms-telemetry=expectation`).
- Hosts: an idle handshake, input-method fixes, the Windows file dialog
  through `IFileDialog`, and MIT-SHM presentation on X11.
- Images drawn below 1x scale go through `stx_conv`.
- The Windows application DLL shrinks from 38.8 MB to 23.9 MB.

### Checkpoints 29 to 31 (pull request #482)
- **C ABI 1.3:** generated class tables, so C and C# programs can override
  a control's `On*` hooks (additions only; the audit against 1.1 and 1.2
  reports no break).
- **Controls:** `Control::RightToLeft`; TextBoxBase, ListControl and
  UpDownBase as Windows Forms' shared bases; VirtualRowsView;
  IndicatorLamp, a lamp control in every theme with an optional light-plane
  lens; `Button::danger`, the danger key; the public NodeCanvas, a node
  graph canvas with its own theme parts and a spatial index.
- **Libraries:** DragTracker; NodeGraph undo, redo and the
  `gui-forms-graph 1` save format; paragraph layout, with glyph runs as
  output and `Painter::draw_glyph_runs`; ImageAtlas; allocation statistics.
- **Rows:** row heights indexed by `stx_btree` (from stx_auxlib) with
  fixed-point heights; 3 to 5 times faster than the previous index at
  10 million rows in the package's timings.
- **Borrowed views** with a checked build (`GUI_FORMS_CHECKED_BORROWS`, on
  in the sanitizer tree), and the member manifest (`manifests/members.json`),
  which classifies every control member against Windows Forms.
- **Industrial surfaces:** eight per-pixel paint finishes and the glint as
  a `finish(...)` material, baked light planes composited at run time, the
  GOST paint palette and the industrial theme, with a Gallery page.
- **Themes:** custom states in selectors (`:state(x)` against a control's
  declared states); the canvas theme with `:item()` selectors and
  world-anchored finish tiles; dithered gradient fills (ordered and blue
  noise).
- **API traps closed:** a direct child `arrange()` during a layout pass is
  refused and counted; stale image ids are reported.

### Pull request #483

**Text and fonts**
- STXlib's glyph library reads, shapes and hints every face, and its text
  layout handles bidirectional text, line breaks and script runs.
- The embedded font pack, with smaller fallback subsets for Chinese,
  Japanese, Korean, Arabic and Indic scripts.
- The font family registry: `FontFamilyId`, family stacks with a theme
  fallback that warns instead of refusing, and nearest faces, across the C
  ABI and C#.
- Application font families: a program loads its own families for its
  composites and sub-surfaces; stock controls and themes never draw them.
  See [Application font families](https://gui-forms.readthedocs.io/en/latest/user/app-fonts.html).

**Content boundary**
- Content surfaces on their own execution context, with stale frames, a
  watchdog and termination; the content editing mode, in which the program
  draws the text and GUI.Forms supplies the caret, input, undo and
  accessibility; hosted content, the content program in its own process,
  on macOS, Linux and Windows (`GUIForms::HostedClient`). See
  [Content surfaces](https://gui-forms.readthedocs.io/en/latest/user/content-surfaces.html).

**Windows, input and presentation**
- The window band's per-edge reservations, colour roles, cycling and
  textures, the control border as schema properties, and window shaping.
  See [The window band](https://gui-forms.readthedocs.io/en/latest/user/window-band.html).
- The business card, which shows the program's identity
  (`application.set_program_identity` in the C ABI), and the diagnostic
  overlay's counters card.
- Relative pointer mode, with raw motion, confinement and warping where the
  host offers them. See
  [Relative pointer mode](https://gui-forms.readthedocs.io/en/latest/user/relative-pointer.html).
- One presentation layer on every host, with host scroll copies; a new
  window stays off screen until its first frame is painted, on macOS and
  X11. See [Presentation](https://gui-forms.readthedocs.io/en/latest/user/presentation.html).
- Modal loops wake for work scheduled outside them; click-through on macOS;
  VoiceOver follows keyboard focus.
- The user's locale is read from the host on macOS, Windows and Linux.
- Windows: native MessageBox icons, file dialog check and create flags that
  follow the request, and scroll copies through `ScrollWindowEx`.

**Controls**
- DataGridView on VirtualRowsView's row engine, with frozen rows and
  columns, click events, cell tooltips, hit testing, full-column selection
  and auto-generated columns.
- Menus jump to an item by its first letter when it has no mnemonic.
- Ribbon: the minimized popup, the Quick Access Toolbar on the tab row or
  below it, and the help button.
- Composite controls written as `.control` documents and named in form
  documents. See [Forms](https://gui-forms.readthedocs.io/en/latest/user/forms.html).
- Plot3DView, 3D plotting over `stx_raster3d`.
- Stock icons from Heroicons, baked from embedded path records.
- ProgressRing draws a continuous arc as well as dots; an HSV colour
  picker.

**Themes and drawing**
- Content effects on a control and its descendants: filters, masks, blend
  modes, `backdrop-filter` and colour stages (`fill()`, `duotone()`,
  `tritone()`).
- Linear-light gradients from exact tables; dithered gradients keep their
  level between bytes.
- Curved paths, trimmed strokes, text at any angle and shaded triangles in
  the Painter and in GUI.Drawing.
- Forced high contrast keeps content fills.

**C ABI, drawing ABI and host protocol** (additions only)
- C ABI 1.4: the content boundary (`GF_TABLE_CONTENT`,
  `GF_HOST_CAPABILITY_HOSTED_CONTENT`).
- C ABI 1.5: the window outer band and shaping, the window's pointer
  location, the list view table (`GF_TABLE_LIST_VIEW`), failure detail
  (`diagnostics.failure_detail`), composite controls, the business card and
  the diagnostic overlay, relative pointer mode, application font families,
  the presentation capabilities and the control's content effect.
- The drawing ABI (`gd_*`) is sealed at 1.0; 1.1, provisional, adds path
  flattening and the raster's entry in the table (`raster_initialize`),
  which replaces the `gdr_initialize` export.
- Host protocol 14 carries the locale, 15 relative pointer mode.

**Library and consumers**
- STXlib is pinned with the stx packages through `stx.lock`; the row
  height index is STXlib's `stx_btree`.
- The C++20 module `import gui_forms;` (`GUIForms::Module`, with
  `GUI_FORMS_BUILD_MODULE`), and a shield that keeps a consumer's macros
  out of GUI.Forms' headers.
- A program is one module: a C++ program links GUI.Forms statically, and a
  C or C# program loads only the C ABI library, which contains the drawing
  ABI.

### Pull requests #484 to #486
- CI builds with warnings as errors (`GUI_FORMS_WARNINGS_AS_ERRORS`, which
  `tools/ci_build.py` sets); the CI tree builds without LTO, and SDK builds
  keep it.
- Each push to `main` publishes its test cache as the release
  `test-cache-<commit>`, which the tests repository pins.
- Ribbon: the group divider's width is themeable through `min-width` on
  `ribbon::part(group-divider)`, in whole units from 1 to 8.
- Bitmap ids are unique across the toolkit; some public records gain
  default initializers, with no layout change.
- The installed-notices check requires `THIRD_PARTY_NOTICES.md` and still
  refuses GPL text.
- The README's documentation badge points at Read the Docs' new host.

### Checkpoint 36 (2026-11-22)
- Menus: every menu surface opens a submenu after the pointer rests on its
  row and closes it after the pointer rests elsewhere, with the safe
  triangle toward the submenu. This now includes the ribbon's two-pane
  application menu and ContextMenu separators. The menu delay is 300 ms on
  every host (Windows' `SPI_GETMENUSHOWDELAY` is no longer read).
- Tooltips: the wrap width follows the text, measured against a few
  candidate widths. Short text stays on one line, and long text wraps
  towards a golden-ratio box. Ribbon tooltips use the same rule.
  `ToolTip::maximum_width` defaults to 2048.
- Industrial surfaces: shipped compact light-map stores, jewel and
  pinpoint lamp shapes, the blasted enamel finishes
  (`finish(blasted-enamel-glint, …)`, `finish(blasted-enamel-heavy-glint,
  …)`), and world-tile painting for finishes over the bake budget. The
  `light_map` engine primitive draws lit, pressable keys from the bakes.
  `RadioButton::lamp_color` sets the lamp colour, and
  `IndicatorLampState::fault` makes a lamp flicker with a deterministic
  failing-lamp pattern. The Gallery's Industrial surfaces page is rebuilt
  around the process-control dialog, and `gui_forms_process_control` shows
  the same dialog as a program of its own. Carbon fibre has finer strands
  and sparse glints.
- Touch: `gui_forms_touch_start`, a demonstrator in the style of a tiled
  start screen: groups of live tiles on a surface that pans, with wheel,
  drag, click and keyboard working now, and launch animations. Its input
  runs through the touch seams (tap, flick, press-and-hold, inertia and
  overscroll), which touch input fills in.
- Ribbon: groups compact automatically after their authored layouts, first
  by image size and then by rows (up to three), before collapsing. A held
  tab shows the pressed dent. A checked command or a selected gallery item
  stands in the gold dithered bulge (the `gold` parameter of `smooth_bulge`
  and `dithered_bulge`). Split commands publish checked.

### Visible-area guarantee (stage VA)
- Every control has `viewport_fit` (`ViewportFit`: what it does when its
  content does not fit its viewport; an owner that scrolls its content
  answers by scrolling) and `design_size`. Defaults by kind: Windows Forms
  panels contain (a deliberate difference from Windows Forms, which clips),
  media contains with no floor, CSS panels scroll, a plain control clips.
- Contain and cover on any control, the root included, through the in-tree
  scale frame (`ScaleFrame`, `Control::window_frame`, `content_frame`,
  `Window::root_frame`): contained controls are recorded again at their
  effective scale and replayed through the frame; hit testing, the pointer,
  the caret, semantics (`SemanticNode::effective_scale`), anchors and damage
  map through it. A floor on the effective scale for controls
  (`PresentationSettings::minimum_content_scale`, 0.5).
- `PictureBox`'s zoom, `RasterCanvas::view_whole_bitmap` and `scene_framed`
  use the one contain computation.
- `ScaledPanel` and `ScaledGroupBox` stay an explicit stretch and gain
  `aspect` (`ScaledAspect::stretch`, the default, or `uniform`: one scale
  keeping the design's proportions, aligned by `viewport_fit`'s alignment).
- Safe insets from hosts (`HostSafeInsetsEvent`, `HostCapability::
  safe_insets`, host protocol 16): macOS in full screen, and the web host's
  visual viewport.
- The diagnostic overlay outlines unanswered (red) and clipped (amber)
  overflows; the visual inspection lists them as layout reasons.
- The window checks each layout's owners against their viewports and keeps
  `overflow_records()` (`OverflowRecord`, `OverflowCause`), diagnoses each
  new or changed overflow (`LayoutDiagnosticCode::content_overflow`),
  counts them (`MetricsSnapshot::overflow_*`) and reports the unanswered
  ones (`unanswered_overflow_count`).
- `SubSurfaceHost` with a design size lays its sub-surface out at that size
  and contains or covers it in its region, rendered at the real pixel size.
- Safe insets (`SafeInsets`, `set_safe_insets`, `visible_client_rectangle`):
  the root slot keeps clear of the edges a host reports hidden.
- `place_viewport_content` and `fit_to_work_area` are the pure
  computations.
- C ABI 1.7: `gf_viewport_fit`, `gf_overflow_record`, `gf_overflow_answer`,
  `gf_overflow_cause`, `gf_content_alignment`, the windows table's
  `overflow_records`, `unanswered_overflow_count` and `safe_insets`, and the
  controls table's `set_viewport_fit` and `viewport_fit`, and
  `GF_HOST_CAPABILITY_SAFE_INSETS`.

### 1.0.1: MCP in every program

- A program that calls its host's single-window entry (`host::run_macos`,
  `run_windows`, `run_linux`) serves `--mcp` and `--mcp-script` as one run by
  `Application::run` does; its window joins the automation as `main`. Until
  now every demonstrator, the Gallery among them, ignored both on every host
  without a word. The automation's run and transport moved from the
  Application library into their own archive (`gui_forms_automation`), which
  the Application library links whole and each desktop host links.
- CI checks this on every platform: the package smoke launches each packaged
  program with `--mcp` and expects answers to initialize and `tools/list`,
  then an exit once its input closes (`tools/check_mcp_launch.py`), and the
  installed reference examples run the same check on `collection_gallery`.
- `collection_gallery` (installed reference example) started with a theme
  error, because its sheet named a colour role outside the theme language's
  vocabulary. It now uses `ink-on-paper`.
- The installed package's config sets `GUIForms_MCP`.

### 1.0.2: STXlib 1.0.1, the library allocator in our programs

- GUI.Forms builds with stx 1.0.2 on STXlib 1.0.2. On Windows, STXlib's
  headers now declare the whole API of every system DLL it ships an import
  list for, and the C runtime's POSIX names (`open`, `fileno`, `stat`, ...),
  so third-party C code such as the image codecs compiles on the own
  runtime. A program may now replace only some of the global `operator new`
  and `delete` forms and still link.
- GUI.Forms' own demo and demonstrator programs, the Gallery included, put
  their C++ allocations on STXlib's allocator
  (`gui_forms_use_library_allocator`). Sanitizer builds keep the
  toolchain's `operator new`.
- The opt-in is proved, not assumed. `gui_forms_allocator_probe` opts in
  and checks that the blocks `operator new`, `new[]`, the aligned form and
  `std::allocator` return are STXlib's: STXlib's release refuses a block that
  is not its own. Under a sanitizer it checks that the replacement left
  itself out. `gui_forms_allocator_probe_control` does not opt in and checks
  that `operator new` is the toolchain's. CI runs both on every desktop host,
  the Windows own runtime and the sanitizer builds included.
- `gui_forms::detail::program_allocator()` reports what a program's
  replacement said at startup: not linked, left out, or replaced. With
  `GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR=1`, a program whose `operator new` is
  not STXlib's exits with status 70 before its window opens.
  `tools/check_mcp_launch.py --require-library-allocator` sets it, so the
  demonstrator check and the package smoke fail a program whose opt-in is
  not live.
- The test cache's loader no longer overrides a consumer's own cache
  entries (the own-runtime and libc++ prefixes) with the values recorded on
  the machine that built the cache.
- `THIRD_PARTY_NOTICES.md` and the Windows packages' `licenses/` match the
  own runtime: libc++ is the only LLVM runtime linked, and the MinGW-w64
  rows are gone. The reliability page says where each allocation route
  ends up.

### 1.0.3: the allocator opt-in for subdirectory consumers, Explore's MCP

- GUI.Forms builds with stx 1.0.3 on STXlib 1.0.3. On Windows, STXlib's C
  library headers declare the secure printf and scanf family (`sprintf_s`,
  `vsnprintf_s`, `sscanf_s`, ... and the wide forms) and `_scprintf`, so
  third-party code such as tinyxml2 compiles on the own runtime.
- `gui_forms_use_library_allocator` works in a project that adds GUI.Forms
  with `add_subdirectory`. It used to compile nothing into the program,
  which stayed on the toolchain's `operator new`. `gui_forms_add_theme` and
  `gui_forms_verify_effects` work there too, and the stx packages' imported
  targets (`stxlib::boundary`, `stxlib::state`, ...) are visible from the
  consumer's directory. `tools/subdirectory_consumer` is such a project:
  CI builds it on every desktop host and runs its allocator probe, which
  fails unless the replacement is live.
- `host::run_linux_application`, `run_macos_application` and
  `run_windows_application` start the automation, as `Application::run`
  does, when a program calls them directly. Explore runs its windows this
  way, and until now it ignored `--mcp` on every host.
- `tools/check_allocator_startup.py` checks the allocator opt-in of a
  program that takes no `--mcp`. It runs the program with
  `GUI_FORMS_REQUIRE_LIBRARY_ALLOCATOR=1` and fails it if it exits at startup.
  Plan Paint and Pocket Reference use it in CI.
- `tools/check_mcp_launch.py` names the exit status of a program that does
  not answer. It waits for a window's tools, which appear when the window
  joins the automation.

### 1.0.4: MCP round 1, the in-use sign

- **The in-use sign.** While a model drives a window over MCP, a 5 px
  accent band runs round it with a white glint that sweeps while the model
  reads or acts, the title names the model ("<title> — <model> is using
  this"), and the content is washed nearly to grey
  (`ThemeDefinition::model_wash_saturation`, `Window::set_content_wash`).
  The band follows the window's corners. Overlays and the band are drawn
  after the wash. A window no model has signed pays nothing. On Windows 11
  the system border takes the accent and is restored on disconnect.
  `--mcp-appearance-review` (debug and test builds) shows none of the sign.
- **Window corners** are reported with their source
  (`WindowCornerReport::source`: `platform`, `layout_region`,
  `private_getter` or `unknown`). macOS 26 reads the corner from layout
  regions; earlier macOS reads `-[NSWindow _effectiveCornerRadius]`, behind
  the CMake option `GUI_FORMS_MACOS_PRIVATE_CORNER_RADIUS` (on; off in
  high-integrity builds; turn it off for sandboxed or App Store builds). A
  window whose corner no source reports is drawn square.
- **Dialogs and models.** A model closes a dialog its own act opened as
  Cancel and never submits one. Controls that open a dialog are marked
  `needs_user` with their dialog-free tool (`set_opens_dialog`). The new
  `reset` tool closes the model's dialogs, popups, menus and tooltips and
  releases held input. A native alert is reported as `native`, never as a
  tooltip's window.
- **Tools.** Unknown argument keys are refused; `batch` is served again
  (its schema had left the MCP server's subset); new `find`; per-node
  actions, rows a list does not show (reached by ID), `scroll_into_view`
  and full text. The tool reference is generated from a running program
  (`tools/mcp_tool_reference.py`, docs: MCP tools), and CI's docs job
  checks it.
