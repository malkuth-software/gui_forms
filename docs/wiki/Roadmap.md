# Roadmap

GUI.Forms is pre-1.0. The package version is 0.6. The detailed roadmap is
[on the documentation site](https://gui-forms.readthedocs.io/en/latest/dev/roadmap.html).

| Release | What it is | State |
|---|---|---|
| 0.1 Shipped | retained core, CPU painting, one text engine, 70+ Forms controls, hosts for macOS, Windows and X11 | released |
| 0.2 Verified | accessibility verified on every host, font substitution, articulated controls, owned codecs | released (v0.2.0) |
| 0.3 Themeable, 0.4 Flexible | the theme engine and language, the GUI.Forms and familiar themes, compositor cooperation, `stx_skia`; flex and grid layout, form documents, damage-preserving painting, more controls | finished, not tagged; part of 0.5 |
| 0.5 ABI and API seal | C ABI 1.1 (audited, with a conformance kit and C# interop), properties as capabilities, the application, windows and rules, Window, Popup and Splash, the form and theme languages, sub-surfaces, the creation-cost contract, smooth scrolling, the window band, the reference application (`examples/reference`) | done (2026-11-08), not tagged |
| **1.0 (backend)** | the library, its C ABI and its C# bindings. Done: C ABI 1.3 with class tables, the final controls, the help system, the drag engine, plots, NodeCanvas, VirtualRowsView, industrial surfaces. To come: font identity and the font pack, the content boundary, the demonstrators and the drawing ABI seal, the STXlib swap-in, the 1.0 conditions, then the Windows hand-back (the C# package, Visual Studio integration) | in progress |
| Tools | the theme editor, the WYSIWYG designer, Visual Studio integration, the Windows Forms conversion program, the mirrored-mode launcher (closed source) | after 1.0 (backend); the launcher is in progress |
| 1.0 Stable | a final cleanup, documentation and tutorials, signed and reproducible packaging, the stability conditions met | |

Builds published from `main` are tagged `v0.6.N`; earlier builds were tagged
`v0.2.N`.

**After 1.0:** HTML import, a light FreeType fork, identical floating-point
results on every architecture, registered font families through themes once
FreeType is formally verified (planned while GUI.Forms read fonts with
FreeType; since 2026-11-16 STXlib's glyph library reads them). GUI.Forms 2.0 is planned for release builds
without exceptions, formal verification and freestanding builds.
