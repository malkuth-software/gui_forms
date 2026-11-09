# Roadmap

GUI.Forms is pre-1.0. The detailed roadmap is
[on the documentation site](https://gui-forms.readthedocs.io/dev/roadmap.html).

| Release | What it is | State |
|---|---|---|
| 0.1 Shipped | retained core, CPU painting, one text engine, 70+ Forms controls, hosts for macOS, Windows and X11 | released |
| 0.2 Verified | accessibility verified on every host, font substitution, articulated controls, owned codecs | released (v0.2.0) |
| 0.3 Themeable, 0.4 Flexible | the theme engine and language, the GUI.Forms and familiar themes, compositor cooperation, `stx_skia`; flex and grid layout, form documents, damage-preserving painting, more controls | finished, not tagged; shipped in 0.5 |
| **0.5 ABI and API seal** | C ABI 1.1 (audited, with a conformance kit and C# interop), properties as capabilities, the application, windows and rules, Window, Popup and Splash, the form and theme languages, sub-surfaces, the creation-cost contract | closing |
| 1.0 (backend) | the final controls; the library, its ABI and its C# bindings | after 0.5 |
| Tools | the conversion program, the visual editor, the interface builder, Visual Studio integration (closed source) | after 1.0 (backend) |
| 1.0 Stable | a final cleanup, better documentation and tutorials, the stability conditions met | |

**Placed after 0.5:** smooth scrolling (unless the owner places it in 0.5),
outer-edge effects (the active border), further Splash work, rules in form
documents (built-in actions only), public run-time form loading, capability
demonstrators, an Emscripten build.

**After 1.0:** HTML import, a light FreeType fork, identical floating-point
results on every architecture.
