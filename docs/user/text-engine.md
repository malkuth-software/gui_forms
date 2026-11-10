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
  that every host implements bit for bit. The Skia raster blit is the
  reference; the Windows DIB compositor matches it, and a test compares them
  byte for byte. Dark-on-light and light-on-dark text come out at the same
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
  API: when a caller names a font family it never registered, the macOS
  build asks CoreText for that family's bytes, and the Windows build
  registers the bytes GDI selects for it. Either way the shared engine
  shapes and rasterizes the text.
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
| Noto Sans CJK JP | Fallback: Han, kana, Hangul | SIL OFL |
| Noto Sans Arabic, Bengali, Devanagari, Gurmukhi, Hebrew | Fallback by script | SIL OFL |
| Noto Emoji (monochrome) | Fallback: emoji and symbols | SIL OFL |

Controls ask for a `FontRole`, not a family name; the host-owned theme maps
roles to faces. Fallback is resolved per grapheme cluster, so a Japanese word
inside a Latin label takes the CJK face for exactly that run. The provenance
of every file, including hashes, is in `assets/fonts/ATTRIBUTION.md` and
`assets/fonts/language-font-provenance.json`.

The native hosts locate fonts differently:

- macOS reads `Contents/Resources/fonts` from the application bundle.
- Windows reads `GUI_FORMS_FONT_DIR` when set, otherwise `fonts/` beside the executable.
- Linux reads `GUI_FORMS_FONT_DIR` when set, otherwise `fonts/` beside the
  executable if present, then `../share/GUIForms/fonts` relative to it.

The native SDK installs fonts and exports their path as the CMake variable
`GUIForms_FONT_DIR`; that variable does not set the runtime environment.
Ship the directory, licences included, at the location your host expects.

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
