<!-- Appended verbatim after the generated header by tools/publish_tested_release.py.
     Keep "Breaking changes" separate from "Other changes"; group "Other changes" by
     control or area; attach one screenshot for anything visual. Rewrite this file for
     each release series; the generated header carries version, commit and platforms.
     Earlier series' notes are kept in docs/release/ (0.1.md to 0.5.md). -->

## Focus

Towards **1.0 (backend)**: the library, its C ABI and its C# bindings. 0.5,
the ABI and API seal, was declared done on 2026-11-08 (see the 0.5 notes).
Everything since builds on the sealed interfaces additively. A change to a
sealed interface needs a recorded owner decision.

The package and the release tags still carry the 0.2 version number
(`v0.2.N`, `find_package(GUIForms 0.2 ...)`). The stages 0.3 to 0.5 were
not tagged separately. The version number moves when 1.0 (backend) is
declared.

## Breaking changes

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

### Pending (checkpoint 27, not yet on main)
- The look integration.
- The declarative drag engine (Splitter and SplitContainer on it), the
  Blender-style value bar, the new drawing primitives (batch and polyline
  drawing, a conic gradient) and the plots package (PlotView, its items and
  Sparkline).
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
