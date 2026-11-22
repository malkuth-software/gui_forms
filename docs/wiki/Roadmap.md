# Roadmap

GUI.Forms is pre-1.0. The detailed roadmap, with the current version
numbers, is
[on the documentation site](https://gui-forms.readthedocs.io/en/latest/dev/roadmap.html).

| Release | What it is | State |
|---|---|---|
| 0.1 Shipped | retained core, CPU painting, one text engine, 70+ Forms controls, hosts for macOS, Windows and X11 | released |
| 0.2 Verified | accessibility verified on every host, font substitution, articulated controls, owned codecs | released (v0.2.0) |
| 0.3 Themeable, 0.4 Flexible | the theme engine and language, the GUI.Forms and familiar themes, compositor cooperation, `stx_skia`; flex and grid layout, form documents, damage-preserving painting, more controls | finished, not tagged; part of 0.5 |
| 0.5 ABI and API seal | C ABI 1.1 (audited, with a conformance kit and C# interop), properties as capabilities, the application, windows and rules, Window, Popup and Splash, the form and theme languages, sub-surfaces, the creation-cost contract, smooth scrolling, the window band, the reference application (`examples/reference`) | done (2026-11-08), not tagged |
| **The backend, toward 1.0** (the current package version) | the library, its C ABI and its C# bindings. Done: the C ABI additions with class tables, the content boundary and composites; the drawing ABI sealed; the final controls, the help system, plots and 3D plots, NodeCanvas, VirtualRowsView, industrial surfaces; STXlib's text stack and the embedded font pack; form documents in XML. Next: the open work integrated (the C# package and NuGet, the Windows traces, the remaining Windows Forms gaps), automation through MCP, then touch input and the tools brought forward | in progress |
| **1.0** | declared once the stability conditions hold, after a cleanup round and signed, reproducible packaging | next |
| 1.x Tools | the theme editor, the WYSIWYG designer, Visual Studio integration, the Windows Forms conversion program, the mirrored-mode launcher (closed source) | after 1.0, unless brought forward; the launcher is in progress |

Builds published from `main` are tagged `v<major>.<minor>.N` with the
package version; earlier builds were tagged `v0.2.N`.

**After 1.0:** HTML import, a light FreeType fork, identical floating-point
results on every architecture, registered font families through themes once
FreeType is formally verified (planned while GUI.Forms read fonts with
FreeType; since 2026-11-16 STXlib's glyph library reads them). GUI.Forms 2.0 is planned for release builds
without exceptions, formal verification and freestanding builds.
