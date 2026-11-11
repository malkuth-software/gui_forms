#pragma once

#include <string_view>

namespace gui_forms::detail {

// The stock `windows-professional` theme's sheets (owner, 2026-11-10: "I
// want the user to be confused as to whether it's Windows 2000, Office
// 2003, or Office 2007 ... then touch it up just a little bit to make it
// look slightly more modern, a bit closer to Windows 10, but with some
// retained depth and pop").
//
// The blend, by family:
// - Windows 2000: push buttons keep the raised bevel (a light inner line
//   along the top and left, a darker one along the bottom and right),
//   pressed inverts it; fields and choice wells are sunken. Both are
//   softened: pale bevels inside one crisp border.
// - Office 2003: graded tool strips and menu bars, orange-gold hover,
//   pressed and checked command highlights, gradient title strips on
//   panels' captions, group headers and docking captions.
// - Office 2007: restrained gloss (a split gradient) on commands and the
//   accent button, a graded ribbon command plane, rounded command
//   highlights.
//
// Colour (owner, 2026-11-10, candidate DX): surfaces are near-white silver
// with a faint ivory cast in light and neutral graphite in dark, so that
// Windows 2000's grey dominates; blue is kept for the accent button,
// selection, focus, links and progress, and amber for hot and checked
// commands.
// - Windows 10: crisp one-unit borders, low saturation, flat large
//   surfaces, a solid focus rectangle, accessible contrast; depth stays in
//   gentle gradients and shadows, and hover and press "pop" (a soft drop
//   shadow while hot, an inset bevel while pressed).
//
// Every enabled pair meets the contrast gate (4.5:1 text, 3:1 boundaries,
// glyphs and focus) against the surround (the panel face over the window
// face); disabled controls are exempt. High contrast is derived from the
// role model, as for every theme, and is not authored here.
//
// Fonts: Selawik, the bundled near metric match for Segoe UI, at 12 px
// stands in for Segoe UI 9 pt (`font-family: Selawik`, a named bundled
// family; owner decision of 2026-11-10). Selawik has Regular, Semibold and
// Bold and no italic; the content role (Carlito) covers what it lacks. No
// Tahoma-like face is bundled (the body-face gate in AGENTS.md).

// The floor (light): every family in every surface state.
inline constexpr std::string_view windows_professional_floor_sheet =
    R"css(/* windows-professional: the floor, light (owner, 2026-11-10).
   Stock #f5f5f3 (near-white silver, a faint ivory cast), windows
   #f7f7f5, paper white; ink #1f1f1d, muted #3c3c3a; the command highlight
   is Office's orange-gold with a #a0681a frame; the selection is a pale
   blue with a #336fb3 frame; the accent is a restrained blue gloss. */

* {
  color: #1f1f1d;
  muted-color: #3c3c3a;
  glyph-color: #1f1f1d;
  outline-color: #1f1f1d;
  default-outline: 1px #2f6db6;
}

/* Flat large surfaces (Windows 10): one quiet face, one line. */
:family(window) {
  background: #f7f7f5;
  border: 1px solid #a8a8a6;
  border-radius: 0;
}
/* A panel's frame is Windows 2000's etch, softened: a grey line along
   the top and left, a white one along the bottom and right, so a group
   box's frame reads as a groove with its light line inside. */
:family(panel) {
  background: #f5f5f3;
  border: 1px solid #b4b4b2;
  border-color: #b4b4b2 #ffffff #ffffff #b4b4b2;
  border-radius: 0;
}
/* A card and a notice lift off the stock: paper with a gentle shadow. */
:family(card), :family(notice) {
  background: #ffffff;
  border: 1px solid #767674;
  border-radius: 3px;
  box-shadow: 0 1px 3px #2a2a2824;
}
/* Office 2007's tooltip: white deepening to a pale silver, with black text
   (owner mandate 2026-11-11). */
:family(tool-tip) {
  background: linear-gradient(to bottom, #ffffff, #efefed);
  border: 1px solid #6b6b69;
  border-radius: 2px;
  box-shadow: 0 2px 4px #2a2a2833;
  color: #000000;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #dbeafa;
  border-color: #336fb3;
}

/* Push buttons: Windows 2000's raised bevel, softened, inside a crisp
   frame, with a pale silver gradient and a contact shadow. Hover turns
   the face a light blue and lifts it (a soft shadow); pressed sinks it
   (the bevel inverted, no shadow); a checked toggle stays sunken on a
   deeper blue. */
:family(button) {
  background: linear-gradient(to bottom, #ffffff, #f6f6f4 46%, #ececea 54%, #e2e2e0);
  border: 1px solid #676765;
  border-radius: 3px;
  keylines: #ffffff #c5c5c3 #b9b9b7 #ffffff;
  box-shadow: 0 1px 1px #2a2a2833;
}
:family(button):hover {
  background: linear-gradient(to bottom, #fbfdff, #e9f3fd 46%, #d5e7f9 54%, #c5dcf5);
  border-color: #3672bb;
  keylines: #ffffff #9dbde3 #8fb2dc #ffffff;
  box-shadow: 0 1px 3px #2a5fa055;
}
:family(button):active {
  background: linear-gradient(to bottom, #b6cfec, #c9dcf2 50%, #d9e7f7);
  border-color: #2a5f9f;
  keylines: #7f9fc8 #ffffffa0 #ffffffa0 #7f9fc8;
  box-shadow: none;
}
:family(button):state(selected) {
  background: linear-gradient(to bottom, #c3d9f2, #b4cfee);
  border-color: #2a5f9f;
  keylines: #7f9fc8 #ffffffa0 #ffffffa0 #7f9fc8;
  box-shadow: none;
}
:family(button):state(pending) {
  background: linear-gradient(to bottom, #fffaf0, #fbe9b8);
  border-color: #9a6316;
}
:family(button):invalid {
  background: linear-gradient(to bottom, #fff6f6, #f7d9d9);
  border-color: #b23636;
}
:family(button):disabled, :family(button):state(selected):disabled {
  background: #f6f6f4;
  border-color: #cbcbc9;
  keylines: none;
  box-shadow: none;
}

/* Command members (tool strips, the ribbon): clear at rest; Office's
   orange-gold while hot, with 2007's split gloss and a soft lift;
   deeper amber while pressed; a checked command keeps a warm gold. */
:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
  keylines: none;
  box-shadow: none;
}
:family(command-button):hover {
  background: linear-gradient(to bottom, #fff6d6, #ffe5a3 45%, #ffd57a 46%, #ffe7a6);
  border-color: #a0681a;
  keylines: #ffffffb0 #ffffffb0 #ffffffb0 #ffffffb0;
  box-shadow: 0 1px 2px #8a5a1033;
}
:family(command-button):active {
  background: linear-gradient(to bottom, #f7c26a, #f9b54f 45%, #f6a73a 46%, #fbcf73);
  border-color: #9a6316;
  keylines: #c98a2e80 #ffffff60 #ffffff60 #c98a2e80;
  box-shadow: none;
}
:family(command-button):state(selected) {
  background: linear-gradient(to bottom, #ffefc4, #ffd98a);
  border-color: #9a6316;
  keylines: #ffffff90 #ffffff90 #ffffff90 #ffffff90;
  box-shadow: none;
}
:family(command-button):state(selected):hover {
  background: linear-gradient(to bottom, #ffe9b0, #ffcc66);
}
:family(command-button):state(selected):active {
  background: linear-gradient(to bottom, #f7c26a, #f6a73a);
  keylines: #c98a2e80 #ffffff60 #ffffff60 #c98a2e80;
}
:family(command-button):state(pending) {
  background: linear-gradient(to bottom, #fffaf0, #fbe9b8);
  border-color: #9a6316;
}
:family(command-button):invalid {
  background: linear-gradient(to bottom, #fff6f6, #f7d9d9);
  border-color: #b23636;
}
:family(command-button):disabled, :family(command-button):state(selected):disabled {
  background: #ffffff00;
  border-color: #ffffff00;
  keylines: none;
  box-shadow: none;
}

/* The accent (default-action) button: Office 2007's blue, its gloss
   restrained to a split one third down, white text. */
:family(accent-button) {
  background: linear-gradient(to bottom, #4b7ebe, #3c74b8 40%, #3267a8 41%, #3a6fb0);
  border: 1px solid #1b3970;
  border-radius: 2px;
  keylines: #ffffff40 #10224650 #10224650 #ffffff40;
  box-shadow: 0 1px 1px #24385733;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):hover {
  background: linear-gradient(to bottom, #497cbe, #4179be 40%, #376daf 41%, #3d73b5);
  border-color: #132d5c;
  box-shadow: 0 1px 3px #24385755;
}
:family(accent-button):active {
  background: linear-gradient(to bottom, #2a5991, #3267a8);
  keylines: #10224660 #ffffff30 #ffffff30 #10224660;
  box-shadow: none;
}
:family(accent-button):state(pending) {
  border-color: #9a6316;
}
:family(accent-button):invalid {
  border-color: #b23636;
}
:family(accent-button):disabled {
  background: #dbdbd9;
  border-color: #cbcbc9;
  keylines: none;
  box-shadow: none;
  color: #676765;
  muted-color: #676765;
  glyph-color: #676765;
}

/* Wells: Windows 2000's sunken field, softened to one crisp frame over a
   faint inner shade. Hover and focus (pressed) turn the frame blue. */
/* The sunken edge: one crisp frame, a soft grey groove inside its top and
   left (Windows 2000's inner shadow line) and a faint inner shade. */
:family(choice), :family(editor) {
  border: 1px solid #696967;
  border-radius: 1px;
  keylines: #d9d9d7 #ffffff00 #ffffff00 #d9d9d7;
  box-shadow: inset 0 1px 2px #2a2a2833;
}
:family(choice) {
  background: linear-gradient(to bottom, #fafaf8, #ffffff);
}
:family(editor) {
  background: #ffffff;
}
:family(choice):hover {
  background: linear-gradient(to bottom, #eaf3fd, #f8fbff);
  border-color: #3a78c2;
}
:family(editor):hover {
  border-color: #3a78c2;
}
:family(choice):active {
  background: linear-gradient(to bottom, #cfe1f6, #e6f0fb);
  border-color: #2f6db6;
}
:family(editor):active {
  border-color: #2f6db6;
}
:family(choice):state(pending), :family(editor):state(pending) {
  background: #fffaeb;
  border-color: #9a6316;
}
:family(choice):invalid, :family(editor):invalid {
  background: #fff4f4;
  border-color: #b23636;
}
:family(choice):disabled, :family(editor):disabled {
  background: #f6f6f4;
  border-color: #cbcbc9;
  box-shadow: none;
}

/* A progress bar: a sunken pale track; the fill (selected) is the accent
   blue, glossed, with white text. */
:family(progress) {
  background: linear-gradient(to bottom, #ececea, #f7f7f5);
  border: 1px solid #696967;
  border-radius: 2px;
  box-shadow: inset 0 1px 2px #2a2a282a;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #4b7ebe, #3c74b8 40%, #3267a8 41%, #3a6fb0);
  border-color: #1b3970;
  box-shadow: none;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(progress):disabled, :family(progress):state(selected):disabled {
  background: #f6f6f4;
  border-color: #cbcbc9;
  box-shadow: none;
}

/* Menu members: clear at rest; Office 2003's gold bar while hot, pressed
   or selected, rounded a little (2007). */
:family(menu-item) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 2px;
}
:family(menu-item):hover, :family(menu-item):state(selected) {
  background: linear-gradient(to bottom, #fff3cf, #ffe2a0);
  border-color: #a0681a;
}
:family(menu-item):active {
  background: linear-gradient(to bottom, #fbd38a, #f9bf5c);
  border-color: #9a6316;
}
:family(menu-item):disabled, :family(menu-item):state(selected):disabled {
  background: #ffffff00;
  border-color: #ffffff00;
}

/* Rows and items: clear at rest; a pale blue while hot, a deeper blue
   (Windows 10's selection, framed) while selected. */
:family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(selection):hover {
  background: linear-gradient(to bottom, #f0f7fe, #dfedfc);
  border-color: #3f7fc4;
}
:family(selection):active, :family(selection):state(selected) {
  background: linear-gradient(to bottom, #dbeafa, #c7def7);
  border-color: #336fb3;
}
:family(selection):state(selected):disabled {
  background: #efefed;
  border-color: #cbcbc9;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #1f4f8f;
  glyph-color: #1f4f8f;
}
:family(visited-link) {
  color: #6a3d99;
  glyph-color: #6a3d99;
}

:family(badge) {
  background: #3c74b8;
  border: 1px solid #3c74b8;
  border-radius: 0;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #dbdbd9;
  border-color: #dbdbd9;
}

:disabled, :state(selected):disabled {
  color: #676765;
  muted-color: #676765;
  glyph-color: #676765;
}

/* The original accents (owner, 2026-11-10, candidate DX): blue buttons,
   selection, links, progress fill and scroll-thumb hover. */
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected), :family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #dbeafa;
  border-color: #336fb3;
}
:family(button):hover {
  background: linear-gradient(to bottom, #f4f9ff, #d5e6f9);
  border-color: #3a78c2;
  keylines: #ffffff #b3cdea #b3cdea #ffffff;
  box-shadow: 0 1px 3px #2a5fa040;
}
:family(button):state(selected) {
  background: linear-gradient(to bottom, #d3e4f7, #c2d8f2);
  border-color: #2f6db6;
  keylines: #9fb9d9 #ffffff80 #ffffff80 #9fb9d9;
  box-shadow: none;
}
:family(accent-button) {
  background: linear-gradient(to bottom, #3773c0, #2e6bb5 40%, #255fa6 41%, #2c66ad);
  border: 1px solid #12336f;
  border-radius: 2px;
  keylines: #ffffff40 #0a1e4650 #0a1e4650 #ffffff40;
  box-shadow: 0 1px 1px #1d335533;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):hover {
  background: linear-gradient(to bottom, #3672bf, #3270bb 40%, #2964ad 41%, #2f6ab2);
  border-color: #0c285c;
  box-shadow: 0 1px 3px #1d335555;
}
:family(accent-button):active {
  background: linear-gradient(to bottom, #1f528f, #255fa6);
  keylines: #0a1e4660 #ffffff30 #ffffff30 #0a1e4660;
  box-shadow: none;
}
:family(accent-button):state(pending) {
  border-color: #9a6316;
}
:family(accent-button):invalid {
  border-color: #b23636;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #3773c0, #2e6bb5 40%, #255fa6 41%, #2c66ad);
  border-color: #12336f;
  box-shadow: none;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(selection):hover {
  background: linear-gradient(to bottom, #f0f7fe, #dfedfc);
  border-color: #3f7fc4;
}
:family(selection):active, :family(selection):state(selected) {
  background: linear-gradient(to bottom, #dbeafa, #c7def7);
  border-color: #336fb3;
}
:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #1f4f8f;
  glyph-color: #1f4f8f;
}
:family(visited-link) {
  color: #6a3d99;
  glyph-color: #6a3d99;
}
)css";

// The floor (dark): the same vocabulary on graphite. The highlight stays
// gold, darkened so light ink reads on it; the selection is a deep blue.
inline constexpr std::string_view windows_professional_dark_floor_sheet =
    R"css(/* windows-professional: the floor, dark (owner, 2026-11-10).
   Stock #30302e (neutral graphite), windows #292927; ink #eaeae8,
   muted #d6d6d4; the command highlight a dark amber framed in #e0aa45;
   the selection #264f78 framed in #4f8fd6; no light plates. */

* {
  color: #eaeae8;
  muted-color: #d6d6d4;
  glyph-color: #eaeae8;
  outline-color: #eaeae8;
  default-outline: 1px #5f9be0;
}

:family(window) {
  background: #292927;
  border: 1px solid #51514f;
  border-radius: 0;
}
:family(panel) {
  background: #30302e;
  border: 1px solid #646462;
  border-color: #757573 #1e1e1c #1e1e1c #757573;
  border-radius: 0;
}
:family(card), :family(notice) {
  background: #373735;
  border: 1px solid #888886;
  border-radius: 3px;
  box-shadow: 0 1px 3px #00000066;
}
:family(tool-tip) {
  background: linear-gradient(to bottom, #40403e, #353533);
  border: 1px solid #888886;
  border-radius: 2px;
  box-shadow: 0 2px 4px #00000080;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #264f78;
  border-color: #4f8fd6;
}

:family(button) {
  background: linear-gradient(to bottom, #51514f, #484846 46%, #3f3f3d 54%, #383836);
  border: 1px solid #888886;
  border-radius: 3px;
  keylines: #ffffff2a #00000059 #00000066 #ffffff2a;
  box-shadow: 0 1px 1px #00000066;
}
:family(button):hover {
  background: linear-gradient(to bottom, #4a6284, #435a7a 46%, #3a506e 54%, #344863);
  border-color: #6aa5ea;
  keylines: #ffffff30 #00000050 #00000060 #ffffff30;
  box-shadow: 0 1px 3px #00000099;
}
:family(button):active {
  background: linear-gradient(to bottom, #263446, #2e3e54 50%, #34465e);
  border-color: #5f9be0;
  keylines: #00000070 #ffffff18 #ffffff18 #00000070;
  box-shadow: none;
}
:family(button):state(selected) {
  background: linear-gradient(to bottom, #424240, #3b3b39);
  border-color: #5f9be0;
  keylines: #00000060 #ffffff14 #ffffff14 #00000060;
  box-shadow: none;
}
:family(button):state(pending) {
  background: linear-gradient(to bottom, #4a4330, #3d3727);
  border-color: #e0aa45;
}
:family(button):invalid {
  background: linear-gradient(to bottom, #4d3434, #3f2b2b);
  border-color: #e07070;
}
:family(button):disabled, :family(button):state(selected):disabled {
  background: #353533;
  border-color: #4f4f4d;
  keylines: none;
  box-shadow: none;
}

:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
  keylines: none;
  box-shadow: none;
}
:family(command-button):hover {
  background: linear-gradient(to bottom, #6b5222, #6b5320 45%, #5f4819 46%, #6b5222);
  border-color: #e0aa45;
  keylines: #ffffff20 #ffffff20 #ffffff20 #ffffff20;
  box-shadow: 0 1px 2px #00000066;
}
:family(command-button):active {
  background: linear-gradient(to bottom, #5a4416, #6b5019);
  border-color: #e0aa45;
  keylines: #00000050 #ffffff14 #ffffff14 #00000050;
  box-shadow: none;
}
:family(command-button):state(selected) {
  background: linear-gradient(to bottom, #66501f, #5a4519);
  border-color: #e0aa45;
  keylines: none;
  box-shadow: none;
}
:family(command-button):state(pending) {
  background: linear-gradient(to bottom, #4a4330, #3d3727);
  border-color: #e0aa45;
}
:family(command-button):invalid {
  background: linear-gradient(to bottom, #4d3434, #3f2b2b);
  border-color: #e07070;
}
:family(command-button):disabled, :family(command-button):state(selected):disabled {
  background: #ffffff00;
  border-color: #ffffff00;
  keylines: none;
  box-shadow: none;
}

:family(accent-button) {
  background: linear-gradient(to bottom, #2f6fc0, #2a66b2 40%, #235ba2 41%, #2862aa);
  border: 1px solid #5f9be0;
  border-radius: 2px;
  keylines: #ffffff30 #00000040 #00000040 #ffffff30;
  box-shadow: 0 1px 1px #00000066;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):hover {
  background: linear-gradient(to bottom, #2c6bbb, #2864ae 40%, #2259a0 41%, #2660a8);
  box-shadow: 0 1px 3px #00000080;
}
:family(accent-button):active {
  background: linear-gradient(to bottom, #1d4f8e, #235ba2);
  keylines: #00000060 #ffffff20 #ffffff20 #00000060;
  box-shadow: none;
}
:family(accent-button):state(pending) {
  border-color: #e0aa45;
}
:family(accent-button):invalid {
  border-color: #e07070;
}
:family(accent-button):disabled {
  background: #3a3a38;
  border-color: #4f4f4d;
  keylines: none;
  box-shadow: none;
  color: #939391;
  muted-color: #939391;
  glyph-color: #939391;
}

:family(choice), :family(editor) {
  border: 1px solid #888886;
  border-radius: 1px;
  keylines: #111111 #ffffff00 #ffffff00 #111111;
  box-shadow: inset 0 1px 2px #00000080;
}
:family(choice) {
  background: linear-gradient(to bottom, #1f1f1d, #272725);
}
:family(editor) {
  background: #222220;
}
:family(choice):hover {
  background: linear-gradient(to bottom, #1f2a38, #262f3b);
  border-color: #5f9be0;
}
:family(editor):hover {
  border-color: #5f9be0;
}
:family(choice):active {
  background: linear-gradient(to bottom, #233753, #2a3d55);
  border-color: #5f9be0;
}
:family(editor):active {
  border-color: #5f9be0;
}
:family(choice):state(pending), :family(editor):state(pending) {
  background: #2e2a20;
  border-color: #e0aa45;
}
:family(choice):invalid, :family(editor):invalid {
  background: #2f2222;
  border-color: #e07070;
}
:family(choice):disabled, :family(editor):disabled {
  background: #30302e;
  border-color: #4f4f4d;
  box-shadow: none;
}

:family(progress) {
  background: linear-gradient(to bottom, #1f1f1d, #272725);
  border: 1px solid #888886;
  border-radius: 2px;
  box-shadow: inset 0 1px 2px #00000066;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #2f6fc0, #2a66b2 40%, #235ba2 41%, #2862aa);
  border-color: #5f9be0;
  box-shadow: none;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(progress):disabled, :family(progress):state(selected):disabled {
  background: #30302e;
  border-color: #4f4f4d;
  box-shadow: none;
}

:family(menu-item) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 2px;
}
:family(menu-item):hover, :family(menu-item):state(selected) {
  background: linear-gradient(to bottom, #6b5320, #5f4819);
  border-color: #e0aa45;
}
:family(menu-item):active {
  background: linear-gradient(to bottom, #5a4416, #6b5019);
  border-color: #e0aa45;
}
:family(menu-item):disabled, :family(menu-item):state(selected):disabled {
  background: #ffffff00;
  border-color: #ffffff00;
}

:family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(selection):hover {
  background: linear-gradient(to bottom, #344459, #2e3d51);
  border-color: #4f8fd6;
}
:family(selection):active, :family(selection):state(selected) {
  background: linear-gradient(to bottom, #2a5583, #264f78);
  border-color: #4f8fd6;
}
:family(selection):state(selected):disabled {
  background: #3a3a38;
  border-color: #4f4f4d;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #8fc0ff;
  glyph-color: #8fc0ff;
}
:family(visited-link) {
  color: #c9a6ff;
  glyph-color: #c9a6ff;
}

:family(badge) {
  background: #2f6fc0;
  border: 1px solid #2f6fc0;
  border-radius: 0;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #3a3a38;
  border-color: #3a3a38;
}

:disabled, :state(selected):disabled {
  color: #939391;
  muted-color: #939391;
  glyph-color: #939391;
}

/* The original accents (owner, 2026-11-10, candidate DX): blue buttons,
   selection, links, progress fill and scroll-thumb hover. */
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected), :family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #264f78;
  border-color: #4f8fd6;
}
:family(button):hover {
  background: linear-gradient(to bottom, #415572, #36475f);
  border-color: #5f9be0;
  keylines: #ffffff22 #00000040 #00000040 #ffffff22;
  box-shadow: 0 1px 3px #00000080;
}
:family(button):state(selected) {
  background: linear-gradient(to bottom, #2f4560, #2a3d55);
  border-color: #5f9be0;
  keylines: #00000060 #ffffff14 #ffffff14 #00000060;
  box-shadow: none;
}
:family(accent-button) {
  background: linear-gradient(to bottom, #2f6fc0, #2a66b2 40%, #235ba2 41%, #2862aa);
  border: 1px solid #5f9be0;
  border-radius: 2px;
  keylines: #ffffff30 #00000040 #00000040 #ffffff30;
  box-shadow: 0 1px 1px #00000066;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):hover {
  background: linear-gradient(to bottom, #2c6bbb, #2864ae 40%, #2259a0 41%, #2660a8);
  box-shadow: 0 1px 3px #00000080;
}
:family(accent-button):active {
  background: linear-gradient(to bottom, #1d4f8e, #235ba2);
  keylines: #00000060 #ffffff20 #ffffff20 #00000060;
  box-shadow: none;
}
:family(accent-button):state(pending) {
  border-color: #e0aa45;
}
:family(accent-button):invalid {
  border-color: #e07070;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #2f6fc0, #2a66b2 40%, #235ba2 41%, #2862aa);
  border-color: #5f9be0;
  box-shadow: none;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(selection):hover {
  background: linear-gradient(to bottom, #344459, #2e3d51);
  border-color: #4f8fd6;
}
:family(selection):active, :family(selection):state(selected) {
  background: linear-gradient(to bottom, #2a5583, #264f78);
  border-color: #4f8fd6;
}
:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #8fc0ff;
  glyph-color: #8fc0ff;
}
:family(visited-link) {
  color: #c9a6ff;
  glyph-color: #c9a6ff;
}
)css";

// The parts the engine paints, light and dark: strips, tabs, the ribbon,
// scroll bars, headers, docking chrome and dialog bands. Compiled over the
// floor; as a base sheet it is the first sheet of a package's cascade
// (ThemeBase::windows_professional).
inline constexpr std::string_view windows_professional_parts_sheet =
    R"css(/* windows-professional: parts (owner, 2026-11-10). */
/* The theme has a light and a dark variant (T4 theme variants): the
   dark media rules below apply in a dark window. */
:root {
  color-scheme: light dark;
}

* {
  font-family: Selawik;
  font-size: 12px;
}

/* Office 2003's graded command bars: a pale top deepening to a
   silver foot, a white top line and a darker foot line. The menu bar
   is the lighter cousin; the status bar is flat with one top line. */
tool-strip::part(face), quick-access-toolbar::part(face), binding-navigator::part(face) {
  background: linear-gradient(to bottom, #ffffff, #f2f2f0 40%, #e5e5e3 60%, #d8d8d6);
  border: 1px solid #bcbcba;
  border-radius: 3px;
  box-shadow: inset 0 1px 0 #ffffff, 0 1px 0 #ffffffb0;
}
menu-strip::part(face) {
  background: linear-gradient(to bottom, #fcfcfa, #f1f1ef 55%, #e8e8e6);
  border: none;
  border-radius: 0;
  box-shadow: inset 0 1px 0 #ffffff, inset 0 -1px 0 #c2c2c0;
}
status-strip::part(face) {
  background: linear-gradient(to bottom, #f0f0ee, #e3e3e1 60%, #dcdcda);
  border: none;
  border-radius: 0;
  box-shadow: inset 0 1px 0 #bdbdbb, inset 0 2px 0 #ffffffa0;
}
tool-strip-panel::part(face) {
  background: linear-gradient(to bottom, #f2f2f0, #e5e5e3);
  border: none;
}

/* Tabs: Office 2007's tabs in silver, rounded, with a crisp frame; a gold
   wash while hot; the selected tab is the page's white with Office 2003's
   orange-gold accent along its top. */
tab-control::part(tab), page-strip::part(tab) {
  background: linear-gradient(to bottom, #ffffff, #f1f1ef 50%, #e6e6e4);
  border: 1px solid #80807e;
  border-radius: 3px;
  box-shadow: inset 0 1px 0 #ffffff;
}
tab-control::part(tab):hover, page-strip::part(tab):hover {
  background: linear-gradient(to bottom, #fffaf0, #ffe9b3);
  border-color: #c08a36;
}
tab-control::part(tab):state(selected), page-strip::part(tab):state(selected) {
  background: linear-gradient(to bottom, #ffffff, #f6f9fd);
  border-color: #6e86a8;
  box-shadow: inset 0 2px 0 #e8a33c;
}

/* The ribbon (Office 2007, restrained): a silver tab rail, the
   command plane a soft gloss in a crisp frame, groups as pale panes with
   a caption strip along their foot, tabs as the rail's light cousins. */
ribbon::part(tab-rail) {
  background: linear-gradient(to bottom, #efefed, #e1e1df);
  border: none;
}
ribbon::part(command-plane) {
  background: linear-gradient(to bottom, #ffffff, #f7f7f5 16%, #ededeb 80%, #e1e1df);
  border: 1px solid #afafad;
  border-radius: 4px;
  box-shadow: inset 0 1px 0 #ffffff, 0 1px 3px #2a2a2833;
}
ribbon::part(tab) {
  background: linear-gradient(to bottom, #fbfbf9, #efefed);
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
ribbon::part(tab):hover {
  background: linear-gradient(to bottom, #fffaf0, #ffecbd);
  border-color: #c08a36;
}
ribbon::part(tab):state(selected) {
  background: linear-gradient(to bottom, #ffffff, #fbfdff);
  border-color: #9fb3cf;
}
ribbon::part(group-region) {
  background: linear-gradient(to bottom, #ffffff, #f7f7f5 70%, #efefed);
  border: 1px solid #c0c0be;
  border-radius: 3px;
  box-shadow: inset 0 1px 0 #ffffff, inset 1px 0 0 #ffffff80;
}
ribbon::part(group-caption) {
  background: linear-gradient(to bottom, #e8e8e6, #d9d9d7);
  box-shadow: inset 0 1px 0 #cdcdcb;
}
ribbon-gallery::part(well) {
  background: #ffffff;
  border: 1px solid #bdbdbb;
  border-radius: 2px;
}
backstage-view::part(column) {
  background: linear-gradient(to bottom, #f3f3f1, #e6e6e4);
}

/* Scroll bars (Windows 10's flat track, with depth kept in the thumb):
   a pale track, a silver gradient thumb in a crisp frame; blue while hot,
   deeper while dragged. Arrow buttons are clear until hot. */
h-scroll-bar::part(track) {
  background: linear-gradient(to bottom, #ececea, #f4f4f2);
  border: none;
}
v-scroll-bar::part(track) {
  background: linear-gradient(to right, #ececea, #f4f4f2);
  border: none;
}
h-scroll-bar::part(thumb), h-scroll-bar::part(increment-arrow):hover, h-scroll-bar::part(decrement-arrow):hover {
  background: linear-gradient(to bottom, #ffffff, #e9eef4 50%, #d8dfe9);
  border: 1px solid #8796ab;
  border-radius: 2px;
  keylines: #ffffff #ffffff00 #ffffff00 #ffffff;
}
v-scroll-bar::part(thumb), v-scroll-bar::part(increment-arrow):hover, v-scroll-bar::part(decrement-arrow):hover {
  background: linear-gradient(to right, #ffffff, #e9eef4 50%, #d8dfe9);
  border: 1px solid #8796ab;
  border-radius: 2px;
  keylines: #ffffff #ffffff00 #ffffff00 #ffffff;
}
h-scroll-bar::part(increment-arrow), h-scroll-bar::part(decrement-arrow),
v-scroll-bar::part(increment-arrow), v-scroll-bar::part(decrement-arrow) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 2px;
}
h-scroll-bar::part(thumb):hover {
  background: linear-gradient(to bottom, #f0f6fe, #d3e3f6);
  border-color: #6f97c9;
}
v-scroll-bar::part(thumb):hover {
  background: linear-gradient(to right, #f0f6fe, #d3e3f6);
  border-color: #6f97c9;
}
h-scroll-bar::part(thumb):active, v-scroll-bar::part(thumb):active {
  background: #c3d6ee;
  border-color: #4f7fb8;
}
h-scroll-bar:disabled::part(thumb), v-scroll-bar:disabled::part(thumb) {
  background: #f4f4f2;
  border-color: #dfdfdd;
}
h-scroll-bar, v-scroll-bar {
  glyph-color: #484846;
}

/* A drop-down menu's icon gutter (Office 2003): a soft band graded across
   behind the icons and check marks, with a light rule along its edge. */
menu-strip::part(gutter) {
  background: linear-gradient(to right, #fbfbf9, #ededeb 85%, #e2e2e0);
  box-shadow: inset -1px 0 0 #d6d6d4;
}

/* Drop and spin buttons: small push buttons with the same bevel. */
combo-box::part(drop-down), numeric-up-down::part(increment-arrow), numeric-up-down::part(decrement-arrow),
domain-up-down::part(up-arrow), domain-up-down::part(down-arrow) {
  background: linear-gradient(to bottom, #ffffff, #f4f4f2 50%, #e7e7e5);
  border: 1px solid #777775;
  border-radius: 1px;
  keylines: #ffffff #ffffff00 #ffffff00 #ffffff;
}
combo-box::part(drop-down):hover, numeric-up-down::part(increment-arrow):hover,
numeric-up-down::part(decrement-arrow):hover, domain-up-down::part(up-arrow):hover,
domain-up-down::part(down-arrow):hover {
  background: linear-gradient(to bottom, #f4f9ff, #d5e6f9);
  border-color: #3a78c2;
}
combo-box::part(drop-down):active, numeric-up-down::part(increment-arrow):active,
numeric-up-down::part(decrement-arrow):active, domain-up-down::part(up-arrow):active,
domain-up-down::part(down-arrow):active {
  background: linear-gradient(to bottom, #c9dcf3, #deebfa);
  border-color: #2f6db6;
}
combo-box:disabled::part(drop-down), numeric-up-down:disabled::part(increment-arrow),
numeric-up-down:disabled::part(decrement-arrow), domain-up-down:disabled::part(up-arrow),
domain-up-down:disabled::part(down-arrow) {
  background: #f6f6f4;
  border-color: #d4d4d2;
}
track-bar::part(thumb) {
  background: linear-gradient(to bottom, #fdfdfb, #e9e9e7);
  border: 1px solid #6b6b69;
  border-radius: 2px;
}

/* Title strips (Office 2003): column headers, property groups, list
   groups, the navigation pane's header, docking captions and the task
   dialog's bands carry a soft vertical gradient with a crisp foot line. */
list-view::part(column-header), tree-list-view::part(column-header), data-grid-view::part(column-header),
object-view::part(column-header) {
  background: linear-gradient(to bottom, #ffffff, #f7f7f5 50%, #ebebe9);
  box-shadow: inset -1px 0 0 #c1c1bf, inset 0 -1px 0 #b3b3b1, inset 1px 1px 0 #ffffff;
}
list-view::part(column-header):hover, tree-list-view::part(column-header):hover,
data-grid-view::part(column-header):hover, object-view::part(column-header):hover {
  background: linear-gradient(to bottom, #f6faff, #dcebfb);
}
property-list::part(group-header), list-view::part(group-band) {
  background: linear-gradient(to bottom, #f2f2f0, #e5e5e3);
  border: none;
  box-shadow: inset 0 -1px 0 #cececc;
}
navigation-pane::part(header) {
  background: linear-gradient(to bottom, #eeeeec, #dbdbd9);
  box-shadow: inset 0 -1px 0 #bdbdbb;
}
navigation-pane::part(nav-page-face):hover {
  background: linear-gradient(to bottom, #fff6d6, #ffd98a);
}
dock-site::part(caption) {
  background: linear-gradient(to bottom, #f9f9f7, #e9e9e7 50%, #dddddb);
  border: 1px solid #b4b4b2;
  border-radius: 0;
  box-shadow: inset 0 1px 0 #ffffff;
}
dock-site::part(caption-accent) {
  background: linear-gradient(to bottom, #ffd98a, #f0a83a);
}
dock-site::part(seam-bar) {
  background: #eaeae8;
}
dock-site::part(flyout-face) {
  background: #fdfdfb;
  border: 1px solid #a8a8a6;
  box-shadow: 0 2px 6px #2a2a2840;
}
dock-site::part(tile) {
  background: linear-gradient(to bottom, #ffffff, #f0f0ee);
  border: 1px solid #70706e;
  border-radius: 2px;
}
splitter::part(face), split-container::part(seam) {
  background: #ededeb;
}
task-dialog::part(caption-band) {
  background: linear-gradient(to bottom, #ffffff, #f5f5f3);
}
task-dialog::part(button-band), task-dialog::part(footnote-band) {
  background: #f5f5f3;
  box-shadow: inset 0 1px 0 #d9d9d7;
}
group-box::part(caption) {
  background: linear-gradient(to right, #eaeae8, #eaeae800);
}
/* The window's outer band, where a program declares one, lights in the
   accent blue. */
window {
  band-lit-color: #555553;
}

/* The original accents (owner, 2026-11-10, candidate DX): blue buttons,
   selection, links, progress fill and scroll-thumb hover. */
h-scroll-bar::part(thumb):hover {
  background: linear-gradient(to bottom, #f0f6fe, #d3e3f6);
  border-color: #6f97c9;
}
v-scroll-bar::part(thumb):hover {
  background: linear-gradient(to right, #f0f6fe, #d3e3f6);
  border-color: #6f97c9;
}
h-scroll-bar::part(thumb):active, v-scroll-bar::part(thumb):active {
  background: #c3d6ee;
  border-color: #4f7fb8;
}

@media (prefers-color-scheme: dark) {
  tool-strip::part(face), quick-access-toolbar::part(face), binding-navigator::part(face) {
    background: linear-gradient(to bottom, #4b4b49, #40403e 40%, #373735 60%, #30302e);
    border-color: #1f1f1d;
    box-shadow: inset 0 1px 0 #ffffff1c, 0 1px 0 #ffffff0c;
  }
  menu-strip::part(face) {
    background: linear-gradient(to bottom, #383836, #31312f);
    box-shadow: inset 0 -1px 0 #1d1d1b;
  }
  status-strip::part(face) {
    background: linear-gradient(to bottom, #343432, #2d2d2b);
    box-shadow: inset 0 1px 0 #4c4c4a;
  }
  tool-strip-panel::part(face) {
    background: linear-gradient(to bottom, #353533, #2e2e2c);
  }
  tab-control::part(tab), page-strip::part(tab) {
    background: linear-gradient(to bottom, #40403e, #353533);
    border-color: #646462;
    box-shadow: inset 0 1px 0 #ffffff12;
  }
  tab-control::part(tab):hover, page-strip::part(tab):hover {
    background: linear-gradient(to bottom, #594827, #4a3c20);
    border-color: #c08a36;
  }
  tab-control::part(tab):state(selected), page-strip::part(tab):state(selected) {
    background: linear-gradient(to bottom, #434341, #3b3b39);
    border-color: #888886;
    box-shadow: inset 0 2px 0 #e0aa45;
  }
  ribbon::part(tab-rail) {
    background: linear-gradient(to bottom, #343432, #2d2d2b);
  }
  ribbon::part(command-plane) {
    background: linear-gradient(to bottom, #484846, #3e3e3c 16%, #353533 80%, #2f2f2d);
    border-color: #61615f;
    box-shadow: inset 0 1px 0 #ffffff1a, 0 1px 3px #00000080;
  }
  ribbon::part(tab) {
    background: linear-gradient(to bottom, #383836, #31312f);
  }
  ribbon::part(tab):hover {
    background: linear-gradient(to bottom, #594827, #4a3c20);
    border-color: #c08a36;
  }
  ribbon::part(tab):state(selected) {
    background: linear-gradient(to bottom, #40403e, #3e3e3c);
    border-color: #575755;
  }
  ribbon::part(group-region) {
    background: linear-gradient(to bottom, #3f3f3d, #383836 70%, #323230);
    border-color: #5d5d5b;
    box-shadow: inset 0 1px 0 #ffffff12, inset 1px 0 0 #ffffff0a;
  }
  ribbon::part(group-caption) {
    background: linear-gradient(to bottom, #31312f, #282826);
    box-shadow: inset 0 1px 0 #51514f;
  }
  ribbon-gallery::part(well) {
    background: #222220;
    border-color: #646462;
  }
  backstage-view::part(column) {
    background: linear-gradient(to bottom, #343432, #2a2a28);
  }
  h-scroll-bar::part(track) {
    background: linear-gradient(to bottom, #222220, #2a2a28);
  }
  v-scroll-bar::part(track) {
    background: linear-gradient(to right, #222220, #2a2a28);
  }
  h-scroll-bar::part(thumb), h-scroll-bar::part(increment-arrow):hover, h-scroll-bar::part(decrement-arrow):hover {
    background: linear-gradient(to bottom, #5c6370, #4e5560 50%, #444a54);
    border-color: #737c8b;
    keylines: #ffffff22 #ffffff00 #ffffff00 #ffffff22;
  }
  v-scroll-bar::part(thumb), v-scroll-bar::part(increment-arrow):hover, v-scroll-bar::part(decrement-arrow):hover {
    background: linear-gradient(to right, #5c6370, #4e5560 50%, #444a54);
    border-color: #737c8b;
    keylines: #ffffff22 #ffffff00 #ffffff00 #ffffff22;
  }
  h-scroll-bar::part(thumb):hover {
    background: linear-gradient(to bottom, #4c6585, #405672);
    border-color: #5f9be0;
  }
  v-scroll-bar::part(thumb):hover {
    background: linear-gradient(to right, #4c6585, #405672);
    border-color: #5f9be0;
  }
  h-scroll-bar::part(thumb):active, v-scroll-bar::part(thumb):active {
    background: #3a5272;
    border-color: #5f9be0;
  }
  h-scroll-bar:disabled::part(thumb), v-scroll-bar:disabled::part(thumb) {
    background: #30302e;
    border-color: #424240;
  }
  h-scroll-bar, v-scroll-bar {
    glyph-color: #cacac8;
  }
  menu-strip::part(gutter) {
    background: linear-gradient(to right, #40403e, #353533 85%, #30302e);
    box-shadow: inset -1px 0 0 #4c4c4a;
  }
  combo-box::part(drop-down), numeric-up-down::part(increment-arrow), numeric-up-down::part(decrement-arrow),
  domain-up-down::part(up-arrow), domain-up-down::part(down-arrow) {
    background: linear-gradient(to bottom, #4b4b49, #3c3c3a);
    border-color: #747472;
  }
  combo-box::part(drop-down):hover, numeric-up-down::part(increment-arrow):hover,
  numeric-up-down::part(decrement-arrow):hover, domain-up-down::part(up-arrow):hover,
  domain-up-down::part(down-arrow):hover {
    background: linear-gradient(to bottom, #415572, #36475f);
    border-color: #5f9be0;
  }
  combo-box::part(drop-down):active, numeric-up-down::part(increment-arrow):active,
  numeric-up-down::part(decrement-arrow):active, domain-up-down::part(up-arrow):active,
  domain-up-down::part(down-arrow):active {
    background: linear-gradient(to bottom, #2b3a50, #33445c);
    border-color: #5f9be0;
  }
  combo-box:disabled::part(drop-down), numeric-up-down:disabled::part(increment-arrow),
  numeric-up-down:disabled::part(decrement-arrow), domain-up-down:disabled::part(up-arrow),
  domain-up-down:disabled::part(down-arrow) {
    background: #353533;
    border-color: #4f4f4d;
  }
  track-bar::part(thumb) {
    background: linear-gradient(to bottom, #5c5c5a, #4b4b49);
    border-color: #888886;
  }
  list-view::part(column-header), tree-list-view::part(column-header), data-grid-view::part(column-header),
  object-view::part(column-header) {
    background: linear-gradient(to bottom, #484846, #3d3d3b 50%, #353533);
    box-shadow: inset -1px 0 0 #1f1f1d, inset 0 -1px 0 #1f1f1d, inset 1px 1px 0 #ffffff14;
  }
  list-view::part(column-header):hover, tree-list-view::part(column-header):hover,
  data-grid-view::part(column-header):hover, object-view::part(column-header):hover {
    background: linear-gradient(to bottom, #3c4a5c, #344052);
  }
  property-list::part(group-header), list-view::part(group-band) {
    background: linear-gradient(to bottom, #3b3b39, #353533);
    box-shadow: inset 0 -1px 0 #51514f;
  }
  navigation-pane::part(header) {
    background: linear-gradient(to bottom, #40403e, #353533);
    box-shadow: inset 0 -1px 0 #575755;
  }
  navigation-pane::part(nav-page-face):hover {
    background: linear-gradient(to bottom, #6b5320, #5f4819);
  }
  dock-site::part(caption) {
    background: linear-gradient(to bottom, #494947, #3d3d3b 50%, #353533);
    border-color: #61615f;
    box-shadow: inset 0 1px 0 #ffffff14;
  }
  dock-site::part(caption-accent) {
    background: linear-gradient(to bottom, #e0aa45, #b8822a);
  }
  dock-site::part(seam-bar) {
    background: #383836;
  }
  dock-site::part(flyout-face) {
    background: #343432;
    border-color: #646462;
    box-shadow: 0 2px 6px #00000080;
  }
  dock-site::part(tile) {
    background: linear-gradient(to bottom, #434341, #383836);
    border-color: #888886;
  }
  splitter::part(face), split-container::part(seam) {
    background: #383836;
  }
  task-dialog::part(caption-band) {
    background: linear-gradient(to bottom, #383836, #31312f);
  }
  task-dialog::part(button-band), task-dialog::part(footnote-band) {
    background: #30302e;
    box-shadow: inset 0 1px 0 #4c4c4a;
  }
  group-box::part(caption) {
    background: linear-gradient(to right, #424240, #42424000);
  }
  window {
    band-lit-color: #5f9be0;
  }

  /* The original dark accents, as in light (candidate DX). */
  h-scroll-bar::part(thumb):hover {
    background: linear-gradient(to bottom, #4c6585, #405672);
    border-color: #5f9be0;
  }
  v-scroll-bar::part(thumb):hover {
    background: linear-gradient(to right, #4c6585, #405672);
    border-color: #5f9be0;
  }
  h-scroll-bar::part(thumb):active, v-scroll-bar::part(thumb):active {
    background: #3a5272;
    border-color: #5f9be0;
  }
}
)css";

} // namespace gui_forms::detail
