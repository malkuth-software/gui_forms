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
// - Office 2003: blue-silver tool strips and menu bars, orange-gold hover,
//   pressed and checked command highlights, gradient title strips on
//   panels' captions, group headers and docking captions.
// - Office 2007: restrained gloss (a split gradient) on commands and the
//   accent button, light blue ribbon tabs and command plane, rounded
//   command highlights.
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
   Stock #eef2f8 (a low-saturation blue-silver), windows #f2f4f8, paper
   white; ink #1b2533, muted #3d4757; boundaries #6c7d95 and darker
   (3:1 or better on the stock); the command highlight is Office's
   orange-gold with a #a0681a frame; the selection is a pale blue with a
   #336fb3 frame; the accent is a restrained blue gloss. */

* {
  color: #1b2533;
  muted-color: #3d4757;
  glyph-color: #1b2533;
  outline-color: #1b2533;
  default-outline: 1px #2f6db6;
}

/* Flat large surfaces (Windows 10): one quiet face, one line. */
:family(window) {
  background: #f2f4f8;
  border: 1px solid #8a99ae;
  border-radius: 0;
}
:family(panel) {
  background: #eef2f8;
  border: 1px solid #b7c2d1;
  border-radius: 0;
}
/* A card and a notice lift off the stock: paper with a gentle shadow. */
:family(card), :family(notice) {
  background: #ffffff;
  border: 1px solid #7c8ca3;
  border-radius: 3px;
  box-shadow: 0 1px 3px #1d335524;
}
/* Office 2007's tooltip: white deepening to a pale blue. */
:family(tool-tip) {
  background: linear-gradient(to bottom, #ffffff, #e4ecf7);
  border: 1px solid #6e7f98;
  border-radius: 2px;
  box-shadow: 0 2px 4px #1d335533;
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
  background: linear-gradient(to bottom, #fbfcfe, #e3e8f0);
  border: 1px solid #6e7f98;
  border-radius: 2px;
  keylines: #ffffff #c3ccd9 #c3ccd9 #ffffff;
  box-shadow: 0 1px 1px #1d33551f;
}
:family(button):hover {
  background: linear-gradient(to bottom, #f4f9ff, #d5e6f9);
  border-color: #3a78c2;
  keylines: #ffffff #b3cdea #b3cdea #ffffff;
  box-shadow: 0 1px 3px #2a5fa040;
}
:family(button):active {
  background: linear-gradient(to bottom, #c9dcf3, #deebfa);
  border-color: #2f6db6;
  keylines: #9fb9d9 #ffffff80 #ffffff80 #9fb9d9;
  box-shadow: none;
}
:family(button):state(selected) {
  background: linear-gradient(to bottom, #d3e4f7, #c2d8f2);
  border-color: #2f6db6;
  keylines: #9fb9d9 #ffffff80 #ffffff80 #9fb9d9;
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
  background: #f1f3f6;
  border-color: #b9c2ce;
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
:family(accent-button):disabled {
  background: #c9d6e6;
  border-color: #b9c2ce;
  keylines: none;
  box-shadow: none;
  color: #6a7a90;
  muted-color: #6a7a90;
  glyph-color: #6a7a90;
}

/* Wells: Windows 2000's sunken field, softened to one crisp frame over a
   faint inner shade. Hover and focus (pressed) turn the frame blue. */
:family(choice), :family(editor) {
  border: 1px solid #6c7d95;
  border-radius: 1px;
  box-shadow: inset 0 1px 2px #1d33552a;
}
:family(choice) {
  background: linear-gradient(to bottom, #f6f8fb, #ffffff);
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
  background: #f1f3f6;
  border-color: #b9c2ce;
  box-shadow: none;
}

/* A progress bar: a sunken pale track; the fill (selected) is the accent
   blue, glossed, with white text. */
:family(progress) {
  background: linear-gradient(to bottom, #e3e8f0, #f2f5f9);
  border: 1px solid #6c7d95;
  border-radius: 2px;
  box-shadow: inset 0 1px 2px #1d33552a;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #3773c0, #2e6bb5 40%, #255fa6 41%, #2c66ad);
  border-color: #12336f;
  box-shadow: none;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(progress):disabled, :family(progress):state(selected):disabled {
  background: #f1f3f6;
  border-color: #b9c2ce;
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
  background: #e7ebf1;
  border-color: #b9c2ce;
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
  background: #2e6bb5;
  border: 1px solid #2e6bb5;
  border-radius: 0;
  color: #ffffff;
  muted-color: #e8f0fa;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #c9d6e6;
  border-color: #c9d6e6;
}

:disabled, :state(selected):disabled {
  color: #6a7a90;
  muted-color: #6a7a90;
  glyph-color: #6a7a90;
}
)css";

// The floor (dark): the same vocabulary on graphite. The highlight stays
// gold, darkened so light ink reads on it; the selection is a deep blue.
inline constexpr std::string_view windows_professional_dark_floor_sheet =
    R"css(/* windows-professional: the floor, dark (owner, 2026-11-10).
   Stock #2b2f36, windows #25282e, paper #1e2126; ink #e6e9ef, muted
   #d0d5de; boundaries #7d8899 and lighter (3:1 or better on the stock);
   the command highlight a dark amber framed in #e0aa45; the selection
   #264f78 framed in #4f8fd6. */

* {
  color: #e6e9ef;
  muted-color: #d0d5de;
  glyph-color: #e6e9ef;
  outline-color: #e6e9ef;
  default-outline: 1px #5f9be0;
}

:family(window) {
  background: #25282e;
  border: 1px solid #4a515c;
  border-radius: 0;
}
:family(panel) {
  background: #2b2f36;
  border: 1px solid #434a55;
  border-radius: 0;
}
:family(card), :family(notice) {
  background: #32363e;
  border: 1px solid #7d8899;
  border-radius: 3px;
  box-shadow: 0 1px 3px #00000066;
}
:family(tool-tip) {
  background: linear-gradient(to bottom, #3a3f48, #30343c);
  border: 1px solid #7d8899;
  border-radius: 2px;
  box-shadow: 0 2px 4px #00000080;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #264f78;
  border-color: #4f8fd6;
}

:family(button) {
  background: linear-gradient(to bottom, #454b56, #363b44);
  border: 1px solid #7d8899;
  border-radius: 2px;
  keylines: #ffffff1c #00000040 #00000040 #ffffff1c;
  box-shadow: 0 1px 1px #00000055;
}
:family(button):hover {
  background: linear-gradient(to bottom, #415572, #36475f);
  border-color: #5f9be0;
  keylines: #ffffff22 #00000040 #00000040 #ffffff22;
  box-shadow: 0 1px 3px #00000080;
}
:family(button):active {
  background: linear-gradient(to bottom, #2b3a50, #33445c);
  border-color: #5f9be0;
  keylines: #00000060 #ffffff14 #ffffff14 #00000060;
  box-shadow: none;
}
:family(button):state(selected) {
  background: linear-gradient(to bottom, #2f4560, #2a3d55);
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
  background: #30343b;
  border-color: #484f5a;
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
  background: #343a44;
  border-color: #484f5a;
  keylines: none;
  box-shadow: none;
  color: #8a93a1;
  muted-color: #8a93a1;
  glyph-color: #8a93a1;
}

:family(choice), :family(editor) {
  border: 1px solid #7d8899;
  border-radius: 1px;
  box-shadow: inset 0 1px 2px #00000066;
}
:family(choice) {
  background: linear-gradient(to bottom, #1b1e22, #23262c);
}
:family(editor) {
  background: #1e2126;
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
  background: #2b2f36;
  border-color: #484f5a;
  box-shadow: none;
}

:family(progress) {
  background: linear-gradient(to bottom, #1b1e22, #23262c);
  border: 1px solid #7d8899;
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
  background: #2b2f36;
  border-color: #484f5a;
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
  background: #343a44;
  border-color: #484f5a;
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
  background: #343a44;
  border-color: #343a44;
}

:disabled, :state(selected):disabled {
  color: #8a93a1;
  muted-color: #8a93a1;
  glyph-color: #8a93a1;
}
)css";

// The parts the engine paints, light and dark: strips, tabs, the ribbon,
// scroll bars, headers, docking chrome and dialog bands. Compiled over the
// floor; as a base sheet it is the first sheet of a package's cascade
// (ThemeBase::windows_professional).
inline constexpr std::string_view windows_professional_parts_sheet =
    R"css(/* windows-professional: parts (owner, 2026-11-10). */
* {
  font-family: Selawik;
  font-size: 12px;
}

/* Office 2003's blue-silver command bars: a pale top deepening to a
   silver-blue foot, a white top line and a darker foot line. The menu bar
   is the lighter cousin; the status bar is flat with one top line. */
tool-strip::part(face), quick-access-toolbar::part(face), binding-navigator::part(face) {
  background: linear-gradient(to bottom, #f6f9fd, #dce7f5 60%, #c9d9ef);
  border: none;
  border-radius: 0;
  box-shadow: inset 0 1px 0 #ffffff, inset 0 -1px 0 #9fb3cf;
}
menu-strip::part(face) {
  background: linear-gradient(to bottom, #fbfcfe, #e6edf7);
  border: none;
  border-radius: 0;
  box-shadow: inset 0 -1px 0 #b6c6dc;
}
status-strip::part(face) {
  background: linear-gradient(to bottom, #e9eff8, #d9e3f1);
  border: none;
  border-radius: 0;
  box-shadow: inset 0 1px 0 #b5c4d9;
}
tool-strip-panel::part(face) {
  background: linear-gradient(to bottom, #e9eff8, #d6e1f0);
  border: none;
}

/* Tabs: Office 2007's light blue tabs, rounded, with a crisp frame; a gold
   wash while hot; the selected tab is the page's white with Office 2003's
   orange-gold accent along its top. */
tab-control::part(tab), page-strip::part(tab) {
  background: linear-gradient(to bottom, #f5f9fe, #dde8f6);
  border: 1px solid #8ea3c0;
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

/* The ribbon (Office 2007, restrained): a light blue tab rail, the
   command plane a soft gloss in a crisp frame, groups as pale panes with
   a caption strip along their foot, tabs as the rail's light cousins. */
ribbon::part(tab-rail) {
  background: linear-gradient(to bottom, #e9f0f9, #d7e3f3);
  border: none;
}
ribbon::part(command-plane) {
  background: linear-gradient(to bottom, #fbfdff, #edf3fb 18%, #e1eaf6 82%, #d3e0f1);
  border: 1px solid #9fb3cf;
  border-radius: 3px;
  box-shadow: inset 0 1px 0 #ffffff, 0 1px 2px #1d33551f;
}
ribbon::part(tab) {
  background: linear-gradient(to bottom, #f7fafe, #e4edf8);
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
  background: linear-gradient(to bottom, #fdfeff, #eaf0f9);
  border: 1px solid #b6c6dc;
  border-radius: 3px;
}
ribbon::part(group-caption) {
  background: linear-gradient(to bottom, #dce7f5, #c9d9ee);
}
ribbon-gallery::part(well) {
  background: #ffffff;
  border: 1px solid #9fb3cf;
  border-radius: 2px;
}
backstage-view::part(column) {
  background: linear-gradient(to bottom, #eaf1fa, #d6e2f2);
}

/* Scroll bars (Windows 10's flat track, with depth kept in the thumb):
   a pale track, a silver gradient thumb in a crisp frame; blue while hot,
   deeper while dragged. Arrow buttons are clear until hot. */
h-scroll-bar::part(track), v-scroll-bar::part(track) {
  background: #eef1f6;
  border: none;
}
h-scroll-bar::part(thumb), h-scroll-bar::part(increment-arrow):hover, h-scroll-bar::part(decrement-arrow):hover {
  background: linear-gradient(to bottom, #f9fafc, #dfe5ee);
  border: 1px solid #9aa8bb;
  border-radius: 2px;
}
v-scroll-bar::part(thumb), v-scroll-bar::part(increment-arrow):hover, v-scroll-bar::part(decrement-arrow):hover {
  background: linear-gradient(to right, #f9fafc, #dfe5ee);
  border: 1px solid #9aa8bb;
  border-radius: 2px;
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
  background: #eef1f6;
  border-color: #d3d9e2;
}
h-scroll-bar, v-scroll-bar {
  glyph-color: #4a5568;
}

/* Drop and spin buttons: small push buttons with the same bevel. */
combo-box::part(drop-down), numeric-up-down::part(increment-arrow), numeric-up-down::part(decrement-arrow),
domain-up-down::part(up-arrow), domain-up-down::part(down-arrow) {
  background: linear-gradient(to bottom, #fbfcfe, #e3e8f0);
  border: 1px solid #8a99ae;
  border-radius: 1px;
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
  background: #f1f3f6;
  border-color: #c4ccd7;
}
track-bar::part(thumb) {
  background: linear-gradient(to bottom, #fbfcfe, #dfe5ee);
  border: 1px solid #6e7f98;
  border-radius: 2px;
}

/* Title strips (Office 2003): column headers, property groups, list
   groups, the navigation pane's header, docking captions and the task
   dialog's bands carry a soft vertical gradient with a crisp foot line. */
list-view::part(column-header), tree-list-view::part(column-header), data-grid-view::part(column-header),
object-view::part(column-header) {
  background: linear-gradient(to bottom, #ffffff, #e9eef5);
  box-shadow: inset -1px 0 0 #c3cdda, inset 0 -1px 0 #b0bccc;
}
list-view::part(column-header):hover, tree-list-view::part(column-header):hover,
data-grid-view::part(column-header):hover, object-view::part(column-header):hover {
  background: linear-gradient(to bottom, #f6faff, #dcebfb);
}
property-list::part(group-header), list-view::part(group-band) {
  background: linear-gradient(to bottom, #e9f0f9, #d5e1f1);
  border: none;
  box-shadow: inset 0 -1px 0 #b6c6dc;
}
navigation-pane::part(header) {
  background: linear-gradient(to bottom, #e3ebf7, #c6d6ec);
  box-shadow: inset 0 -1px 0 #9fb3cf;
}
navigation-pane::part(nav-page-face):hover {
  background: linear-gradient(to bottom, #fff6d6, #ffd98a);
}
dock-site::part(caption) {
  background: linear-gradient(to bottom, #edf2f9, #d5e1f1);
  border: 1px solid #a7b6cb;
  border-radius: 0;
}
dock-site::part(caption-accent) {
  background: linear-gradient(to bottom, #ffd98a, #f0a83a);
}
dock-site::part(seam-bar) {
  background: #dfe6f0;
}
dock-site::part(flyout-face) {
  background: #fbfcfe;
  border: 1px solid #8a99ae;
  box-shadow: 0 2px 6px #1d335540;
}
dock-site::part(tile) {
  background: linear-gradient(to bottom, #ffffff, #e6edf7);
  border: 1px solid #6e86a8;
  border-radius: 2px;
}
splitter::part(face), split-container::part(seam) {
  background: #e3e9f2;
}
task-dialog::part(caption-band) {
  background: linear-gradient(to bottom, #ffffff, #eef3fa);
}
task-dialog::part(button-band), task-dialog::part(footnote-band) {
  background: #eef2f8;
  box-shadow: inset 0 1px 0 #c9d3e0;
}
group-box::part(caption) {
  background: linear-gradient(to right, #dce7f5, #dce7f500);
}
/* The window's outer band, where a program declares one, lights in the
   accent blue. */
window {
  band-lit-color: #2f6db6;
}

@media (prefers-color-scheme: dark) {
  tool-strip::part(face), quick-access-toolbar::part(face), binding-navigator::part(face) {
    background: linear-gradient(to bottom, #3a3f48, #30343c 60%, #2a2e35);
    box-shadow: inset 0 1px 0 #ffffff14, inset 0 -1px 0 #1a1c20;
  }
  menu-strip::part(face) {
    background: linear-gradient(to bottom, #33373f, #2c3037);
    box-shadow: inset 0 -1px 0 #1a1c20;
  }
  status-strip::part(face) {
    background: linear-gradient(to bottom, #2f333a, #292c32);
    box-shadow: inset 0 1px 0 #454c57;
  }
  tool-strip-panel::part(face) {
    background: linear-gradient(to bottom, #30343b, #2a2e34);
  }
  tab-control::part(tab), page-strip::part(tab) {
    background: linear-gradient(to bottom, #3a3f48, #30343c);
    border-color: #5a6474;
    box-shadow: inset 0 1px 0 #ffffff12;
  }
  tab-control::part(tab):hover, page-strip::part(tab):hover {
    background: linear-gradient(to bottom, #594827, #4a3c20);
    border-color: #c08a36;
  }
  tab-control::part(tab):state(selected), page-strip::part(tab):state(selected) {
    background: linear-gradient(to bottom, #3d434d, #353a43);
    border-color: #7d8899;
    box-shadow: inset 0 2px 0 #e0aa45;
  }
  ribbon::part(tab-rail) {
    background: linear-gradient(to bottom, #2f333a, #292c32);
  }
  ribbon::part(command-plane) {
    background: linear-gradient(to bottom, #3a3f48, #33373f 18%, #2e3239 82%, #2a2e34);
    border-color: #4f5763;
    box-shadow: inset 0 1px 0 #ffffff14, 0 1px 2px #00000066;
  }
  ribbon::part(tab) {
    background: linear-gradient(to bottom, #33373f, #2c3037);
  }
  ribbon::part(tab):hover {
    background: linear-gradient(to bottom, #594827, #4a3c20);
    border-color: #c08a36;
  }
  ribbon::part(tab):state(selected) {
    background: linear-gradient(to bottom, #3a3f48, #383d46);
    border-color: #4f5763;
  }
  ribbon::part(group-region) {
    background: linear-gradient(to bottom, #363a42, #2f333a);
    border-color: #4a515c;
  }
  ribbon::part(group-caption) {
    background: linear-gradient(to bottom, #30343b, #2a2e34);
  }
  ribbon-gallery::part(well) {
    background: #1e2126;
    border-color: #5a6474;
  }
  backstage-view::part(column) {
    background: linear-gradient(to bottom, #2f333a, #262a30);
  }
  h-scroll-bar::part(track), v-scroll-bar::part(track) {
    background: #24272c;
  }
  h-scroll-bar::part(thumb), h-scroll-bar::part(increment-arrow):hover, h-scroll-bar::part(decrement-arrow):hover {
    background: linear-gradient(to bottom, #555c68, #474d57);
    border-color: #6b7483;
  }
  v-scroll-bar::part(thumb), v-scroll-bar::part(increment-arrow):hover, v-scroll-bar::part(decrement-arrow):hover {
    background: linear-gradient(to right, #555c68, #474d57);
    border-color: #6b7483;
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
    background: #2b2f36;
    border-color: #3c424c;
  }
  h-scroll-bar, v-scroll-bar {
    glyph-color: #c4cad4;
  }
  combo-box::part(drop-down), numeric-up-down::part(increment-arrow), numeric-up-down::part(decrement-arrow),
  domain-up-down::part(up-arrow), domain-up-down::part(down-arrow) {
    background: linear-gradient(to bottom, #454b56, #363b44);
    border-color: #6b7483;
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
    background: #30343b;
    border-color: #484f5a;
  }
  track-bar::part(thumb) {
    background: linear-gradient(to bottom, #555c68, #454b56);
    border-color: #7d8899;
  }
  list-view::part(column-header), tree-list-view::part(column-header), data-grid-view::part(column-header),
  object-view::part(column-header) {
    background: linear-gradient(to bottom, #383c44, #30343b);
    box-shadow: inset -1px 0 0 #4a515c, inset 0 -1px 0 #4f5763;
  }
  list-view::part(column-header):hover, tree-list-view::part(column-header):hover,
  data-grid-view::part(column-header):hover, object-view::part(column-header):hover {
    background: linear-gradient(to bottom, #3c4a5c, #344052);
  }
  property-list::part(group-header), list-view::part(group-band) {
    background: linear-gradient(to bottom, #363a42, #30343b);
    box-shadow: inset 0 -1px 0 #4a515c;
  }
  navigation-pane::part(header) {
    background: linear-gradient(to bottom, #3a3f48, #30343c);
    box-shadow: inset 0 -1px 0 #4f5763;
  }
  navigation-pane::part(nav-page-face):hover {
    background: linear-gradient(to bottom, #6b5320, #5f4819);
  }
  dock-site::part(caption) {
    background: linear-gradient(to bottom, #383c44, #30343b);
    border-color: #4f5763;
  }
  dock-site::part(caption-accent) {
    background: linear-gradient(to bottom, #e0aa45, #b8822a);
  }
  dock-site::part(seam-bar) {
    background: #33373e;
  }
  dock-site::part(flyout-face) {
    background: #2f333a;
    border-color: #5a6474;
    box-shadow: 0 2px 6px #00000080;
  }
  dock-site::part(tile) {
    background: linear-gradient(to bottom, #3d434d, #33383f);
    border-color: #7d8899;
  }
  splitter::part(face), split-container::part(seam) {
    background: #33373e;
  }
  task-dialog::part(caption-band) {
    background: linear-gradient(to bottom, #33373f, #2c3037);
  }
  task-dialog::part(button-band), task-dialog::part(footnote-band) {
    background: #2b2f36;
    box-shadow: inset 0 1px 0 #454c57;
  }
  group-box::part(caption) {
    background: linear-gradient(to right, #3a4250, #3a425000);
  }
  window {
    band-lit-color: #5f9be0;
  }
}
)css";

} // namespace gui_forms::detail
