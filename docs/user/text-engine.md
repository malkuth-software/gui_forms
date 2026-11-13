# Text engine

GUI.Forms owns text rendering end to end. One shaping engine, one rasterizer
and one blending rule run on every platform, from a fixed set of bundled
fonts, so a string set in a given face at a given size and scale produces the
same pixels on macOS, Windows and Linux. No host text API (CoreText,
GDI/ClearType, DirectWrite, FreeType-through-Skia) decides what text looks
like. The decision and its measurements are recorded in ADR-004; this page
describes the result.

## The profile

Shaping
: HarfBuzz over FreeType with unhinted advances at the logical size. Kerning
  and GPOS positioning are HarfBuzz's. Line widths therefore match the font's
  designed outlines within a tenth of a pixel instead of drifting with the
  hinter.

Placement
: Integer 26.6 device units. The baseline snaps to a whole device pixel; the
  horizontal subpixel phase is kept, so proportional text does not jitter.

Rasterization
: FreeType light auto-hinting, grayscale anti-aliasing, no stem darkening and
  no outline widening. Light hinting keeps vertical stems crisp without
  deforming glyphs in fonts that have no meaningful bytecode instructions.

Blending
: Coverage is composited in sRGB values with gamma 1.0, using one arithmetic
  that every host shares: the Skia raster blit, which every host's renderer
  uses. Dark-on-light and light-on-dark text come out at the same
  perceived weight.

Glyph memory
: Rasterized glyph runs are stored with the retained display chunk that drew
  them and released with it. There is no global glyph cache or eviction
  policy.

A cross-platform identity test renders a fixed corpus and compares the output
against checked-in hashes on every CI platform. A change that alters one
pixel on one platform fails that test.

## What was deliberately left out

- **LCD subpixel rendering.** Colour-fringe rendering needs per-display
  subpixel-layout knowledge the toolkit does not have, and current displays
  are high density. Grayscale only.
- **Host font catalogues for control text.** Controls never enumerate or
  load system fonts. The bundled set is the whole set, which is what makes
  identical output possible. The one exception is the separate GUI.Drawing C
  API. A family it is asked for resolves first through the font substitution
  table (below); for a family the table does not name, the macOS build asks
  CoreText for that family's bytes and the Windows build registers the bytes
  GDI selects for it, accepted only when the host's face has the requested
  name, and otherwise the content face substitutes. Either way the shared
  engine shapes and rasterizes the text.
- **Colour emoji.** The bundled emoji face is monochrome.
- **Coverage gamma other than 1.0, linear-light blending, bytecode hinting.**
  Each was measured and rejected as a stylistic or polarity-dependent change
  rather than a correction.

## Bundled fonts

| Face | Role | Licence |
|---|---|---|
| Portsmouth Rapids (regular, bold) | `FontRole::control`: titles, buttons, labels, menus | Owner's MIT grant (`assets/fonts/LICENSE-PortsmouthRapids.txt`) |
| Portsmouth Rapids (light, italic, bold italic), Portsmouth (five styles), Portsmouth Mono | Installed with the SDK; not registered by the hosts | Owner's MIT grant |
| Carlito (four styles) | `FontRole::content`: editable text, lists, fields; metric-compatible with Calibri | SIL OFL |
| Cousine (four styles) | `FontRole::monospace` | SIL OFL |
| Selawik (regular, semibold, bold) | A bundled family a theme names with `font-family: Selawik` (the Segoe UI-like face of `windows-professional`); read the first time text asks for it. It has no italic | SIL OFL |
| Arimo, Tinos (four styles each) | Metric-compatible substitutes for Arial and Times New Roman in the font substitution table | SIL OFL |
| Noto Sans CJK JP | Fallback: Han, kana, Hangul | SIL OFL |
| Noto Sans Arabic, Bengali, Devanagari, Gurmukhi, Hebrew | Fallback by script | SIL OFL |
| Noto Emoji (monochrome) | Fallback: emoji and symbols | SIL OFL |

Controls ask for a `FontRole`, not a family name; the theme maps roles to
faces, and may name a bundled family in place of a role's faces
([The theme language](theme-language)), never a host font. Fallback is resolved per grapheme cluster, so a Japanese word
inside a Latin label takes the CJK face for exactly that run. The provenance
of every file, including hashes, is in `assets/fonts/ATTRIBUTION.md` and
`assets/fonts/language-font-provenance.json`.

Every host, and GUI.Drawing's raster C API, reads the faces from the font
pack compiled into the library; no font file is read at run time. A
development build configured with `GUI_FORMS_DEVELOPMENT_FONT_DIRECTORY=ON`
reads a font directory first (`GUI_FORMS_FONT_DIR`, else the checkout's
`assets/fonts`), for work on the faces themselves.

The font substitution table, `assets/fonts/font-substitution.json`, resolves
a family requested by name to a bundled family: its own name first, then a
substitution (Calibri to Carlito, Arial to Arimo, Times New Roman to Tinos,
Courier New to Cousine, Tahoma to Selawik, each recorded as metric-compatible
or nearest with its measured exceptions), an alias (Helvetica as Arial) or a
generic name (`sans-serif` is Carlito, `serif` Tinos, `monospace` Cousine).
The build generates fixed records from it
(`gui_forms/drawing/font_substitution/font_substitution.hpp`); the C# and
Web.Forms siblings read the same file.

The native SDK installs the faces' licences and exports their directory as
the CMake variable `GUIForms_FONT_DIR`. Ship the licences with your program.

## The text store

Beneath shaping sits a renderer-neutral text store used by every editable
control:

- Strict UTF-8. Malformed, overlong, surrogate, truncated and out-of-range
  sequences are rejected rather than replaced.
- Distinct position types (`Utf8Offset`, `Utf16Offset`, `ScalarIndex`,
  `GraphemeIndex`, `LineIndex`). An offset that would split a scalar or a
  surrogate pair is rejected.
- Unicode 17 extended grapheme segmentation, passing all official conformance
  cases, with next/previous cluster navigation.
- Atomic bounded `replace` that reports removed and inserted scalar counts.
- Line indexing for CRLF, CR, LF, NEL, line separator and paragraph
  separator.
- Sorted, non-overlapping style spans that split and shift deterministically
  under edits.
- A snapshot of revision, edit, style and rejected-operation counters.

Bidirectional analysis in the shared shaping engine uses the vendored
SheenBidi. Unicode line breaking through libunibreak belongs to the optional
development text-mask profile (`GUI_FORMS_BUILD_TEXT_MASKS`); it is not part
of the normal installed text engine. Neither dependency's types appear in
public headers.

## Paragraph layout for owner-drawn rows

A control that draws many rows of wrapped text itself, such as the rows of a
`VirtualRowsView`, lays each paragraph out with `lay_out_paragraph`
(`gui_forms/text/paragraph_layout/paragraph_layout.hpp`) and draws it with
`paint_paragraph`. The layout wraps as `TextBlock` wraps, with the same
provider calls, so its lines are the lines `TextBlock` would paint, at about
two shapings a line instead of one per candidate run.

- **Caller storage.** The call keeps nothing between calls and allocates
  nothing of its own: it fills the caller's `ParagraphStorage`, and a call
  whose storage is too small returns `storage_too_small` with the sizes it
  needs. `ParagraphBuffers` owns storage sized once and reused, and a
  shaping workspace from the window's text metrics provider, so the
  provider's shaping allocates nothing either once it is reserved.
- **Glyph runs.** With `ParagraphGlyphOutput::runs` the layout also fills
  each line's glyph runs (`GlyphRunRecord`, `PositionedGlyph`) into the
  storage, and the `paint_paragraph` overload that takes them draws with
  `Painter::draw_glyph_runs` without shaping the text again.
- **Estimates.** `measure_paragraph` returns only the line count, height and
  widest line, for a row's height before it is drawn.
- **Limits.** Lines break in logical order and each line is reordered for
  display as its own bidirectional paragraph; one base direction across a
  paragraph's lines and vertical text are not supported.

Its storage is counted under `AllocationSubsystem::text_layout`
([Counting allocations](debugging.md#counting-allocations)).

## Text input

Committed text arrives through `Window::dispatch_text` as UTF-8. Composition
(IME) is a host capability. macOS bridges preedit composition. Windows does
not advertise `HostCapability::text_composition`: the system input method
draws its own composition and candidate windows, placed at the focused
control's caret, and committed `WM_CHAR` text arrives without a preedit
session. The X11 host does not advertise the flag either. Multiline `TextBox` reports its document extent through the
shared scrolling base, so it gets real scrollbars, wheel and keyboard paging.

## Open edges

Small-size legibility (lightness models do not capture thin-stroke
perception), vertical metrics under light hinting, coloured text
compositing and glyph-run memory under very large documents remain open and
are listed as such in the decision record.
