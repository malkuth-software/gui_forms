#include "gui_forms/theme/familiar_themes/familiar_themes.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include "../theme_compiler/built_in_theme.hpp"
#include "windows_professional_floor.hpp"
#include "windows_professional_sheets.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// -- the floor sheets -------------------------------------------------------------
//
// Each sheet styles every family in every surface state, as the default
// theme's floor sheet does (gui_forms_theme.cpp): :hover is hot, :active
// pressed, :state(pending), :invalid, :disabled, :state(deactivated) and
// :state(selected). The last rule of each gives disabled text its colour,
// selected or not. Each familiar sheet follows its visual reference where
// the engine can express it; its comment names the reference and the
// values changed to meet the contrast gate on enabled elements (disabled
// controls are exempt).

constexpr std::string_view classic_sheet = R"css(/* classic: the Windows 2000 look (stage T4, phase 7; owner, 2026-11-08:
   authentic, not inspired). The system colours of Windows 2000's
   standard scheme: the face #d4d0c8 (3D face), white highlight, #d4d0c8
   light, #808080 shadow, #404040 dark shadow, #0a246a selection, #ffffe1
   tooltips. The bevels are DrawFrameControl's and DrawEdge's: a push
   button is raised soft (outer white and #404040, inner #d4d0c8 and
   #808080), pressed it is a black frame round a #808080 line; a field, a
   check box and a radio button are sunken (outer #808080 and white, inner
   #404040 and #d4d0c8) round white, the radio's as a circle; a group box
   is etched (the panel's sunken outer edge, #808080 over white); a panel's
   own frame is Fixed3D's sunken edge; a card is a raised panel. Our
   keylines draw the inner ring. The boundary gate reads a bevel by its
   strongest line, its shadow side. One contrast fix: a toolbar button's
   thin edge has a #404040 shadow side (Windows' #808080 is 2.6:1 on the
   face; the gate is 3:1).
   Disabled text is #808080 (Windows embosses it in white; we do not):
   disabled controls are exempt from the gate. Fonts and the scroll bar's
   dithered track are in the parts sheet below. */

* {
  color: #000000;
  muted-color: #404040;
  glyph-color: #000000;
  outline-color: #000000;
  default-outline: 1px #000000;
}

:family(window) {
  background: #d4d0c8;
  border: 1px solid #808080;
  border-radius: 0;
}
:family(panel) {
  background: #d4d0c8;
  border: 1px solid #808080;
  border-color: #808080 #ffffff #ffffff #808080;
  border-radius: 0;
  keylines: #404040 #d4d0c8 #d4d0c8 #404040;
}
:family(card), :family(notice) {
  background: #d4d0c8;
  border: 1px solid #404040;
  border-color: #ffffff #404040 #404040 #ffffff;
  border-radius: 0;
  keylines: #d4d0c8 #808080 #808080 #d4d0c8;
}
:family(tool-tip) {
  background: #ffffe1;
  border: 1px solid #000000;
  border-radius: 0;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #ffffff;
  border-color: #404040;
}

/* Push buttons: raised soft; pressed, a black frame round a shadow line;
   a checked toggle sunken on a lighter face. The default button gains the
   black ring (default-outline); its bevel is in the parts sheet. */
:family(button), :family(accent-button) {
  background: #d4d0c8;
  border: 1px solid #404040;
  border-color: #ffffff #404040 #404040 #ffffff;
  border-radius: 0;
  keylines: #d4d0c8 #808080 #808080 #d4d0c8;
}
:family(button):active, :family(accent-button):active {
  border-color: #000000;
  keylines: #808080 #808080 #808080 #808080;
}
:family(button):state(selected) {
  background: #e8e6e1;
  border-color: #404040 #ffffff #ffffff #404040;
  keylines: #808080 #d4d0c8 #d4d0c8 #808080;
}
:family(button):state(pending), :family(accent-button):state(pending) {
  border-color: #705800;
}
:family(button):invalid, :family(accent-button):invalid {
  border-color: #a00000;
}

/* Toolbar buttons: flat at rest, a thin raised edge while hot, a thin
   sunken one while pressed or checked. */
:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
  keylines: none;
}
:family(command-button):hover {
  border-color: #ffffff #404040 #404040 #ffffff;
}
:family(command-button):active, :family(command-button):state(selected) {
  border-color: #404040 #ffffff #ffffff #404040;
}
:family(command-button):state(selected) {
  background: #e8e6e1;
}

/* Wells: white, sunken two rings deep; disabled on the face. A check box
   or radio button pressed shows the face in its well. */
:family(choice), :family(editor) {
  background: #ffffff;
  border: 1px solid #808080;
  border-color: #808080 #ffffff #ffffff #808080;
  border-radius: 0;
  keylines: #404040 #d4d0c8 #d4d0c8 #404040;
}
:family(choice):active {
  background: #d4d0c8;
}
:family(choice):state(pending), :family(editor):state(pending) {
  background: #fffbe0;
  border-color: #705800;
}
:family(choice):invalid, :family(editor):invalid {
  background: #fff0f0;
  border-color: #a00000;
}
:family(choice):disabled, :family(editor):disabled {
  background: #d4d0c8;
}

/* A progress bar: a thin sunken track on the face, its fill the
   selection blue. */
:family(progress) {
  background: #d4d0c8;
  border: 1px solid #808080;
  border-color: #808080 #ffffff #ffffff #808080;
  border-radius: 0;
}
:family(progress):state(selected) {
  background: #0a246a;
  border-color: #0a246a;
  color: #ffffff;
  muted-color: #d4d0c8;
  glyph-color: #ffffff;
}
:family(progress):state(selected):disabled {
  background: #808080;
  border-color: #808080;
}

/* Menu members and rows: clear at rest, the selection blue with white
   text while hot, pressed or selected; the focus ring turns white on it. */
:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(menu-item):state(selected), :family(selection):hover,
:family(selection):active, :family(selection):state(selected) {
  background: #0a246a;
  border-color: #0a246a;
  color: #ffffff;
  muted-color: #d4d0c8;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(menu-item):state(selected):disabled, :family(selection):state(selected):disabled {
  background: #d4d0c8;
  border-color: #808080;
  outline-color: #000000;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #0000ff;
  glyph-color: #0000ff;
}
:family(visited-link) {
  color: #800080;
  glyph-color: #800080;
}

:family(badge) {
  background: #0a246a;
  border: 1px solid #0a246a;
  border-radius: 0;
  color: #ffffff;
  muted-color: #d4d0c8;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #d4d0c8;
  border-color: #808080;
}

:disabled, :state(selected):disabled {
  color: #808080;
  muted-color: #808080;
  glyph-color: #808080;
}
)css";

// classic's parts sheet (compiled rules, after the floor): every control
// draws the bundled content face at 11 px, standing in for Windows 2000's
// Tahoma at 8 pt (11 px at 96 dpi; we bundle no Tahoma), and a scroll
// bar's track is Windows' 50 % dither of white over the face: stripes at
// 45 degrees one diagonal pixel wide, which fall on alternate pixel
// centres as a checker at scale 1 (a gradient fill takes no tile size).
constexpr std::string_view classic_parts_sheet = R"css(/* classic: parts (Windows 2000). */
* {
  font-family: content;
  font-size: 11px;
}
/* Windows 2000 draws the default button's black ring outside the full
   bevel: two rings of keylines inside it (white and #404040, then
   #d4d0c8 and #808080); pressed, the ring encloses the shadow line as on
   any button. */
button:default::part(face) {
  border-color: #000000;
  keylines: #ffffff #404040 #404040 #ffffff, #d4d0c8 #808080 #808080 #d4d0c8;
}
button:default:active::part(face) {
  keylines: #808080 #808080 #808080 #808080;
}
/* A disabled button is never the default: no ring, the plain bevel. */
button:default:disabled::part(face) {
  border-color: #ffffff #404040 #404040 #ffffff;
  keylines: #d4d0c8 #808080 #808080 #d4d0c8;
}
button:default:disabled {
  default-outline: 0px #000000;
}
/* The scroll track: Windows' 50% dither of white and the face, a
   checkerboard of logical pixels. */
h-scroll-bar::part(track), v-scroll-bar::part(track) {
  background: pattern(checker, #d4d0c8, #ffffff);
  border: none;
}
)css";

constexpr std::string_view modern_flat_sheet = R"css(/* modern-flat: a flat modern look (stage T4, phase 7; M076: boundaries,
   hover, focus and input stay unequivocal). Light grey stock #f3f3f3,
   near-white plates framed in #767676 (the lightest grey at 3:1 on the
   stock), the accent blue #0067c0, a solid two-unit focus ring. Toolbar
   buttons and menu members show a frame while hot, so that the hover
   boundary meets 3:1. */

* {
  color: #1b1b1b;
  muted-color: #5d5d5d;
  glyph-color: #1b1b1b;
  outline-color: #1b1b1b;
  default-outline: 1px #0067c0;
}

:family(window), :family(panel) {
  background: #f3f3f3;
  border: 1px solid #e0e0e0;
  border-radius: 0;
}
:family(card), :family(notice), :family(tool-tip) {
  background: #ffffff;
  border: 1px solid #767676;
  border-radius: 4px;
}
:family(tool-tip) {
  border-radius: 2px;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #cce4f7;
  border-color: #005499;
}

:family(button) {
  background: #fdfdfd;
  border: 1px solid #767676;
  border-radius: 2px;
}
:family(button):hover {
  background: #e5f1fb;
  border-color: #0067c0;
}
:family(button):active, :family(button):state(selected) {
  background: #cce4f7;
  border-color: #005499;
}
:family(button):state(pending) {
  border-color: #8a6100;
}
:family(button):invalid {
  border-color: #c42b1c;
}
:family(button):disabled {
  background: #f5f5f5;
  border-color: #c6c6c6;
}

:family(accent-button) {
  background: #0067c0;
  border: 1px solid #005499;
  border-radius: 2px;
  color: #ffffff;
  muted-color: #f3f8fd;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):hover {
  background: #005fb3;
}
:family(accent-button):active {
  background: #005499;
  border-color: #003e73;
}
:family(accent-button):state(pending) {
  border-color: #8a6100;
}
:family(accent-button):invalid {
  border-color: #c42b1c;
}
:family(accent-button):disabled {
  background: #e0e0e0;
  border-color: #c6c6c6;
  outline-color: #1b1b1b;
}

:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 2px;
}
:family(command-button):hover {
  background: #e5e5e5;
  border-color: #767676;
}
:family(command-button):active {
  background: #d6d6d6;
  border-color: #767676;
}
:family(command-button):state(selected) {
  background: #cce4f7;
  border-color: #005499;
}

:family(choice), :family(editor), :family(progress) {
  background: #ffffff;
  border: 1px solid #767676;
  border-radius: 2px;
}
:family(choice):hover, :family(editor):hover, :family(progress):hover {
  border-color: #1b1b1b;
}
:family(choice):active, :family(editor):active, :family(progress):active {
  border-color: #0067c0;
}
:family(choice):state(pending), :family(editor):state(pending), :family(progress):state(pending) {
  background: #fff8e6;
  border-color: #8a6100;
}
:family(choice):invalid, :family(editor):invalid, :family(progress):invalid {
  background: #fdf0ef;
  border-color: #c42b1c;
}
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled {
  background: #f5f5f5;
  border-color: #c6c6c6;
}
:family(progress):state(selected) {
  background: #0067c0;
  border-color: #0067c0;
  color: #ffffff;
  muted-color: #f3f8fd;
  glyph-color: #ffffff;
}
:family(progress):state(selected):disabled {
  background: #f5f5f5;
  border-color: #c6c6c6;
}

:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 2px;
}
:family(selection) {
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(selection):hover, :family(selection):active {
  background: #e5f1fb;
  border-color: #0067c0;
}
:family(menu-item):state(selected), :family(selection):state(selected) {
  background: #cce4f7;
  border-color: #005499;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #0067c0;
  glyph-color: #0067c0;
}
:family(visited-link) {
  color: #6b2fa0;
  glyph-color: #6b2fa0;
}

:family(badge) {
  background: #0067c0;
  border: 1px solid #0067c0;
  border-radius: 0;
  color: #ffffff;
  muted-color: #f3f8fd;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #e0e0e0;
  border-color: #c6c6c6;
}

:disabled, :state(selected):disabled {
  color: #6e6e6e;
  muted-color: #6e6e6e;
  glyph-color: #6e6e6e;
}
)css";

constexpr std::string_view xp_style_sheet = R"css(/* xp-style: a Luna-like look (stage T4, phase 7), following XP.css
   (https://botoxparty.github.io/XP.css/): the beige stock #ece9d8,
   white-to-beige plates (#ffffff, #ecebe5 at 86%, #d8d0c4) in navy
   #003c74 frames, the orange inner light while hot, the focus blue inner
   ring on the default button, the highlight #2267cb, white wells, the
   green progress fill on a white track framed in #686868. XP.css draws
   its inner lights as four offset inset shadows; our keylines draw them
   as one inner ring of four colours.
   Contrast fixes on enabled elements: the field frame is #4f6f99 (XP.css's
   #7f9db9 is 2.3:1 on the stock; the boundary gate is 3:1), and a card's
   and a notice's frame is #86867b (the fieldset's #d0d0bf is 1.3:1). A
   toolbar button's hover frame is the navy one.
   Disabled text keeps Luna's #aca899: disabled controls are exempt. */

* {
  color: #000000;
  muted-color: #4d4d4d;
  glyph-color: #000000;
  outline-color: #000000;
  default-outline: 1px #003c74;
}

:family(window), :family(panel) {
  background: #ece9d8;
  border: 1px solid #aca899;
  border-radius: 0;
}
:family(card) {
  background: #ffffff;
  border: 1px solid #86867b;
  border-radius: 4px;
}
:family(notice) {
  background: #ffffff;
  border: 1px solid #86867b;
  border-radius: 4px;
}
:family(tool-tip) {
  background: #ffffe1;
  border: 1px solid #000000;
  border-radius: 0;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #dde7f7;
  border-color: #2267cb;
}

:family(button), :family(accent-button) {
  background: linear-gradient(to bottom, #ffffff, #ecebe5 86%, #d8d0c4);
  border: 1px solid #003c74;
  border-radius: 3px;
  keylines: none;
}
:family(accent-button) {
  keylines: #cee7ff #bcd4f6 #89ade4 #98b8ea;
}
:family(button):hover, :family(accent-button):hover {
  keylines: #fff0cf #fbc761 #e5a01a #fdd889;
}
:family(button):active, :family(accent-button):active, :family(button):state(selected) {
  background: linear-gradient(to bottom, #cdcac3, #e3e3db 8%, #e5e5de 94%, #f2f2f1);
  keylines: none;
}
:family(button):state(pending), :family(accent-button):state(pending) {
  border-color: #9c6d00;
}
:family(button):invalid, :family(accent-button):invalid {
  border-color: #b00000;
}
:family(button):disabled, :family(accent-button):disabled {
  background: #f4f3ee;
  border-color: #c9c7ba;
  keylines: none;
}

:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
:family(command-button):hover {
  background: linear-gradient(to bottom, #ffffff, #ecebe5 86%, #d8d0c4);
  border-color: #003c74;
}
:family(command-button):active {
  background: linear-gradient(to bottom, #cdcac3, #e3e3db 8%, #e5e5de 94%, #f2f2f1);
  border-color: #003c74;
}
:family(command-button):state(selected) {
  background: #e3e3db;
  border-color: #003c74;
}

:family(editor) {
  background: #ffffff;
  border: 1px solid #4f6f99;
  border-radius: 0;
}
/* Luna's check box and radio button: a #1c5180 frame round a fill from
   #dcdcd7 to white, the mark green; pressed, the fill darkens. Contrast
   fix: the mark is #107a10 (Luna's #21a121 is 2.5:1 on #dcdcd7, the glyph
   gate is 3:1) and the pressed fill starts at #c4c4bb (Luna's #b0b0a7 under
   the mark is 2.5:1). */
:family(choice) {
  background: linear-gradient(to bottom right, #dcdcd7, #ffffff);
  border: 1px solid #1c5180;
  border-radius: 0;
  glyph-color: #107a10;
}
:family(choice):active {
  background: linear-gradient(to bottom right, #c4c4bb, #e3e1d4);
}
:family(progress) {
  background: #ffffff;
  border: 1px solid #686868;
  border-radius: 4px;
}
:family(editor):hover, :family(progress):hover, :family(editor):active, :family(progress):active {
  border-color: #2267cb;
}
:family(choice):state(pending), :family(editor):state(pending), :family(progress):state(pending) {
  background: #fffbe5;
  border-color: #9c6d00;
}
:family(choice):invalid, :family(editor):invalid, :family(progress):invalid {
  background: #fff0f0;
  border-color: #b00000;
}
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled {
  background: #ece9d8;
  border-color: #c9c7ba;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #acedad, #2ed330 42%, #ffffff);
  border-color: #686868;
  muted-color: #1a1a1a;
}
:family(progress):state(selected):disabled {
  background: #ece9d8;
  border-color: #c9c7ba;
}

:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(menu-item):state(selected), :family(selection):hover,
:family(selection):active, :family(selection):state(selected) {
  background: #316ac5;
  border-color: #316ac5;
  color: #ffffff;
  muted-color: #f0f4fb;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(menu-item):state(selected):disabled, :family(selection):state(selected):disabled {
  background: #ece9d8;
  border-color: #c9c7ba;
  outline-color: #000000;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #0000ff;
  glyph-color: #0000ff;
}
:family(visited-link) {
  color: #800080;
  glyph-color: #800080;
}

:family(badge) {
  background: #316ac5;
  border: 1px solid #316ac5;
  border-radius: 0;
  color: #ffffff;
  muted-color: #f0f4fb;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #ece9d8;
  border-color: #c9c7ba;
}

:disabled, :state(selected):disabled {
  color: #aca899;
  muted-color: #aca899;
  glyph-color: #aca899;
}
)css";

constexpr std::string_view xp_style_parts_sheet = R"css(/* xp-style: parts (Luna). Luna's face is
   Tahoma 8 pt, which is not bundled: the bundled body face stands in.
   Scroll bar thumbs and arrows and the combo box's drop button are Luna's
   pale blue plates with slate arrows; the scroll track is pale stock. The
   up-down arrows take the same plates. A group box's caption is Luna's
   blue #0046d5; its frame keeps the panel's #aca899 (Luna's #d0d0bf is
   1.3:1 on the stock). */
* {
  font-family: content;
  font-size: 11px;
}
h-scroll-bar::part(track) {
  background: linear-gradient(to bottom, #f3f1ec, #fefefb);
  border: none;
}
v-scroll-bar::part(track) {
  background: linear-gradient(to right, #f3f1ec, #fefefb);
  border: none;
}
h-scroll-bar::part(thumb), h-scroll-bar::part(increment-arrow), h-scroll-bar::part(decrement-arrow),
combo-box::part(drop-down) {
  background: linear-gradient(to bottom, #d4e1fd, #b6cbf6);
  border: 1px solid #98b1ea;
  border-radius: 3px;
  keylines: none;
}
v-scroll-bar::part(thumb), v-scroll-bar::part(increment-arrow), v-scroll-bar::part(decrement-arrow) {
  background: linear-gradient(to right, #d4e1fd, #b6cbf6);
  border: 1px solid #98b1ea;
  border-radius: 3px;
  keylines: none;
}
h-scroll-bar::part(thumb):hover, h-scroll-bar::part(increment-arrow):hover, h-scroll-bar::part(decrement-arrow):hover,
v-scroll-bar::part(thumb):hover, v-scroll-bar::part(increment-arrow):hover, v-scroll-bar::part(decrement-arrow):hover,
combo-box::part(drop-down):hover {
  background: linear-gradient(to bottom, #e3edfe, #c4d6fb);
}
h-scroll-bar::part(thumb):active, h-scroll-bar::part(increment-arrow):active, h-scroll-bar::part(decrement-arrow):active,
v-scroll-bar::part(thumb):active, v-scroll-bar::part(increment-arrow):active, v-scroll-bar::part(decrement-arrow):active,
combo-box::part(drop-down):active {
  background: linear-gradient(to bottom, #a9bff0, #c2d4f8);
}
h-scroll-bar:disabled::part(thumb), h-scroll-bar:disabled::part(increment-arrow), h-scroll-bar:disabled::part(decrement-arrow),
v-scroll-bar:disabled::part(thumb), v-scroll-bar:disabled::part(increment-arrow), v-scroll-bar:disabled::part(decrement-arrow),
combo-box:disabled::part(drop-down) {
  background: #f4f3ee;
  border-color: #e3e1d4;
}
h-scroll-bar, v-scroll-bar, combo-box {
  glyph-color: #4d6185;
}
h-scroll-bar:disabled, v-scroll-bar:disabled, combo-box:disabled {
  glyph-color: #c9c7ba;
}
/* Luna's group box caption is blue. */
group-box::part(caption) {
  color: #0046d5;
}
group-box:disabled::part(caption) {
  color: #aca899;
}
/* Luna's up-down arrows: the scroll bar's blue plates and slate arrows. */
numeric-up-down::part(increment-arrow), numeric-up-down::part(decrement-arrow),
domain-up-down::part(up-arrow), domain-up-down::part(down-arrow) {
  background: linear-gradient(to bottom, #d4e1fd, #b6cbf6);
  border: 1px solid #98b1ea;
  border-radius: 2px;
  keylines: none;
}
numeric-up-down::part(increment-arrow):hover, numeric-up-down::part(decrement-arrow):hover,
domain-up-down::part(up-arrow):hover, domain-up-down::part(down-arrow):hover {
  background: linear-gradient(to bottom, #e3edfe, #c4d6fb);
}
numeric-up-down::part(increment-arrow):active, numeric-up-down::part(decrement-arrow):active,
domain-up-down::part(up-arrow):active, domain-up-down::part(down-arrow):active {
  background: linear-gradient(to bottom, #a9bff0, #c2d4f8);
}
numeric-up-down:disabled::part(increment-arrow), numeric-up-down:disabled::part(decrement-arrow),
domain-up-down:disabled::part(up-arrow), domain-up-down:disabled::part(down-arrow) {
  background: #f4f3ee;
  border-color: #e3e1d4;
}
numeric-up-down::part(increment-glyph), numeric-up-down::part(decrement-glyph),
domain-up-down::part(up-glyph), domain-up-down::part(down-glyph) {
  glyph-color: #4d6185;
}
numeric-up-down:disabled::part(increment-glyph), numeric-up-down:disabled::part(decrement-glyph),
domain-up-down:disabled::part(up-glyph), domain-up-down:disabled::part(down-glyph) {
  glyph-color: #c9c7ba;
}
/* Luna's default button: the blue inner light; hot, the orange one. */
button:default::part(face) {
  keylines: #cee7ff #bcd4f6 #89ade4 #98b8ea;
}
button:default:hover::part(face) {
  keylines: #fff0cf #fbc761 #e5a01a #fdd889;
}
button:default:active::part(face), button:default:disabled::part(face) {
  keylines: none;
}
button:default:disabled {
  default-outline: 0px #003c74;
}
)css";

constexpr std::string_view windows7_style_sheet = R"css(/* windows7-style: an Aero-like look (stage T4, phase 7), following 7.css
   (https://khang-nd.github.io/7.css/): the stock #f0f0f0, glossy plates
   split at 45% (#f2f2f2, #ebebeb, #cfcfcf) inside a #ffffffcc inner ring,
   sky-blue hover (#eaf6fd, #bee6fd, #a7d9f5) and press (#e5f4fc, #c4e5f6,
   #98d1ef, #68b3db), the default button framed in #5586a3 on the hover
   gloss, text #222222, white wells with 2-unit corners, the green progress
   fill #0bd82c on a #dddddd track, the menu items' pale gloss. Its window
   glass needs T5's glass material; until then windows are opaque stock.
   Contrast fixes on enabled elements (the boundary gate is 3:1 on the
   stock, text 4.5:1): the button frame is #898a8a (7.css's #8e8f8f is
   2.9:1), the pressed frame #6c8fa9 (#6d91ab, 2.9:1); the field frame is
   #898a8f at rest and #5590ba while hot (7.css's top edges #abadb3 and
   #5794bf are 2.0:1 and 2.9:1, its lighter side edges are not drawn: per
   edge colours need square corners); a card's frame is #858c8e (the
   fieldset's #cdd7db is 1.3:1); a hot menu item's frame is #6e8fa2
   (#aaddfa, 1.3:1); muted text on the press gloss is #3f3f3f; a selected
   row is #2878c9 (white on 7.css's #3399ff is 2.9:1). Disabled text keeps
   7.css's #838383: disabled controls are exempt. */

* {
  color: #222222;
  muted-color: #4c4c4c;
  glyph-color: #222222;
  outline-color: #000000;
  default-outline: 1px #5586a3;
}

:family(window), :family(panel) {
  background: #f0f0f0;
  border: 1px solid #a0a0a0;
  border-radius: 0;
}
:family(card) {
  background: #ffffff;
  border: 1px solid #858c8e;
  border-radius: 3px;
  keylines: #ffffff;
}
:family(notice) {
  background: #ffffff;
  border: 1px solid #858c8e;
  border-radius: 3px;
  keylines: #ffffff;
}
:family(tool-tip) {
  background: linear-gradient(to bottom, #ffffff, #e4e5f0);
  border: 1px solid #767676;
  border-radius: 3px;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #cce8ff;
  border-color: #3c7fb1;
}

:family(button), :family(accent-button) {
  background: linear-gradient(to bottom, #f2f2f2 45%, #ebebeb 45%, #cfcfcf);
  border: 1px solid #898a8a;
  border-radius: 3px;
  keylines: #ffffffcc;
}
:family(accent-button) {
  background: linear-gradient(to bottom, #eaf6fd 45%, #bee6fd 45%, #a7d9f5);
  border-color: #5586a3;
}
:family(button):hover, :family(accent-button):hover {
  background: linear-gradient(to bottom, #eaf6fd 45%, #bee6fd 45%, #a7d9f5);
  border-color: #3c7fb1;
}
:family(button):active, :family(accent-button):active, :family(button):state(selected) {
  background: linear-gradient(to bottom, #e5f4fc, #c4e5f6 30%, #c4e5f6 50%, #98d1ef 50%, #68b3db);
  border-color: #6c8fa9;
  muted-color: #3f3f3f;
}
:family(button):state(pending), :family(accent-button):state(pending) {
  border-color: #8a6100;
}
:family(button):invalid, :family(accent-button):invalid {
  border-color: #c42b1c;
}
:family(button):disabled, :family(accent-button):disabled {
  background: #f4f4f4;
  border-color: #adb2b5;
  keylines: none;
}

:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
:family(command-button):hover {
  background: linear-gradient(to bottom, #eaf6fd 45%, #bee6fd 45%, #a7d9f5);
  border-color: #3c7fb1;
}
:family(command-button):active, :family(command-button):state(selected) {
  background: linear-gradient(to bottom, #e5f4fc, #c4e5f6 30%, #c4e5f6 50%, #98d1ef 50%, #68b3db);
  border-color: #6c8fa9;
  muted-color: #3f3f3f;
}

:family(choice), :family(editor) {
  background: #ffffff;
  border: 1px solid #898a8f;
  border-radius: 2px;
}
:family(progress) {
  background: #dddddd;
  border: 1px solid #8e8f8f;
  border-radius: 3px;
}
:family(choice):hover, :family(editor):hover, :family(progress):hover {
  border-color: #5590ba;
}
:family(choice):active, :family(editor):active, :family(progress):active {
  border-color: #3d7bad;
}
:family(choice):state(pending), :family(editor):state(pending), :family(progress):state(pending) {
  background: #fffbe5;
  border-color: #8a6100;
}
:family(choice):invalid, :family(editor):invalid, :family(progress):invalid {
  background: #fff0f0;
  border-color: #c42b1c;
}
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled {
  background: #f0f0f0;
  border-color: #afafaf;
}
:family(progress):state(selected) {
  background: #0bd82c;
  border-color: #8e8f8f;
  muted-color: #1a1a1a;
}
:family(progress):state(selected):disabled {
  background: #f0f0f0;
  border-color: #afafaf;
}

:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
:family(selection) {
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(menu-item):state(selected) {
  background: linear-gradient(to bottom, #ffffff99, #e6ecf5cc 90%, #ffffffcc);
  border-color: #6e8fa2;
}
:family(selection):hover, :family(selection):active {
  background: #cce8ff;
  border-color: #3c7fb1;
}
:family(selection):state(selected) {
  background: #2878c9;
  border-color: #2878c9;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(selection):state(selected):disabled {
  background: #f0f0f0;
  border-color: #afafaf;
  outline-color: #000000;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #0066cc;
  glyph-color: #0066cc;
}
:family(visited-link) {
  color: #800080;
  glyph-color: #800080;
}

:family(badge) {
  background: #2c628b;
  border: 1px solid #2c628b;
  border-radius: 0;
  color: #ffffff;
  muted-color: #d4e6f5;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #f4f4f4;
  border-color: #c5c5c5;
}

:disabled, :state(selected):disabled {
  color: #838383;
  muted-color: #838383;
  glyph-color: #838383;
}
)css";

constexpr std::string_view windows7_style_parts_sheet = R"css(/* windows7-style: parts (Aero). Aero's face
   is Segoe UI 9 pt, which is not bundled: the bundled body face stands in
   at 12 px. The scroll track is Aero's pale grey; the thumb is the grey
   gloss in a #979797 frame; the arrow buttons are clear until hot, when
   they take the thumb's gloss. The up-down arrows are small gloss
   buttons. */
* {
  font-family: content;
  font-size: 12px;
}
h-scroll-bar::part(track) {
  background: linear-gradient(to bottom, #e3e3e3, #f0f0f0 20%, #f0f0f0 80%, #e3e3e3);
  border: none;
}
v-scroll-bar::part(track) {
  background: linear-gradient(to right, #e3e3e3, #f0f0f0 20%, #f0f0f0 80%, #e3e3e3);
  border: none;
}
h-scroll-bar::part(thumb), h-scroll-bar::part(increment-arrow):hover, h-scroll-bar::part(decrement-arrow):hover {
  background: linear-gradient(to bottom, #f5f5f5, #e9e9e9 45%, #d9d9d9 46%, #cfcfcf);
  border: 1px solid #979797;
  border-radius: 2px;
  keylines: #f8f8f8 #f8f8f8 #f8f8f8 #f8f8f8;
}
v-scroll-bar::part(thumb), v-scroll-bar::part(increment-arrow):hover, v-scroll-bar::part(decrement-arrow):hover {
  background: linear-gradient(to right, #f5f5f5, #e9e9e9 45%, #d9d9d9 46%, #cfcfcf);
  border: 1px solid #979797;
  border-radius: 2px;
  keylines: #f8f8f8 #f8f8f8 #f8f8f8 #f8f8f8;
}
h-scroll-bar::part(increment-arrow), h-scroll-bar::part(decrement-arrow),
v-scroll-bar::part(increment-arrow), v-scroll-bar::part(decrement-arrow) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 2px;
  keylines: none;
}
h-scroll-bar::part(thumb):hover, v-scroll-bar::part(thumb):hover {
  background: linear-gradient(to bottom, #eaf6fd, #d9f0fc 45%, #bee6fd 46%, #a7d9f5);
  border-color: #3c7fb1;
}
v-scroll-bar::part(thumb):hover {
  background: linear-gradient(to right, #eaf6fd, #d9f0fc 45%, #bee6fd 46%, #a7d9f5);
}
h-scroll-bar:disabled::part(thumb), v-scroll-bar:disabled::part(thumb) {
  background: #f0f0f0;
  border-color: #d9d9d9;
  keylines: none;
}
h-scroll-bar, v-scroll-bar {
  glyph-color: #4d4d4d;
}
/* A disabled button is never the default: no ring. */
button:default:disabled {
  default-outline: 0px #5586a3;
}
/* Aero's up-down arrows: small grey gloss buttons, blue while hot. */
numeric-up-down::part(increment-arrow), numeric-up-down::part(decrement-arrow),
domain-up-down::part(up-arrow), domain-up-down::part(down-arrow) {
  background: linear-gradient(to bottom, #f5f5f5, #e9e9e9 45%, #d9d9d9 46%, #cfcfcf);
  border: 1px solid #979797;
  border-radius: 2px;
  keylines: none;
}
numeric-up-down::part(increment-arrow):hover, numeric-up-down::part(decrement-arrow):hover,
domain-up-down::part(up-arrow):hover, domain-up-down::part(down-arrow):hover {
  background: linear-gradient(to bottom, #eaf6fd, #d9f0fc 45%, #bee6fd 46%, #a7d9f5);
  border-color: #3c7fb1;
}
numeric-up-down::part(increment-arrow):active, numeric-up-down::part(decrement-arrow):active,
domain-up-down::part(up-arrow):active, domain-up-down::part(down-arrow):active {
  background: linear-gradient(to bottom, #e5f4fc, #c4e5f6 50%, #98d1ef 50%, #68b3db);
  border-color: #6c8fa9;
}
numeric-up-down:disabled::part(increment-arrow), numeric-up-down:disabled::part(decrement-arrow),
domain-up-down:disabled::part(up-arrow), domain-up-down:disabled::part(down-arrow) {
  background: #f4f4f4;
  border-color: #d9d9d9;
}
numeric-up-down::part(increment-glyph), numeric-up-down::part(decrement-glyph),
domain-up-down::part(up-glyph), domain-up-down::part(down-glyph) {
  glyph-color: #4d4d4d;
}
)css";

constexpr std::string_view macos_style_parts_sheet = R"css(/* macos-style: parts (AppKit). The system face
   is SF Pro 13 pt, which is not bundled: the bundled body face stands in.
   As AppKit: the default push button is the accent with white text; a
   checked check box or radio button is an accent well with a white mark;
   the combo box's drop button is the accent with a white chevron; push
   buttons and wells are white in light mode; scroll bars are a pale track
   with a grey capsule thumb and no arrows (the theme's structure hides
   them; the frames keep the theme's #949494 contrast fix). */
* {
  font-family: content;
  font-size: 13px;
}
button:default::part(face), combo-box::part(drop-down),
check-box:checked::part(well), check-box:indeterminate::part(well), radio-button:checked::part(well) {
  background: #0069f9;
  border-color: #0069f9;
}
button:default:active::part(face), combo-box::part(drop-down):active {
  background: #005bd9;
  border-color: #005bd9;
}
button:default, combo-box, check-box:checked, check-box:indeterminate, radio-button:checked {
  glyph-color: #ffffff;
}
button:default {
  color: #ffffff;
}
combo-box:disabled::part(drop-down), check-box:checked:disabled::part(well),
check-box:indeterminate:disabled::part(well), radio-button:checked:disabled::part(well) {
  background: #0069f966;
  border-color: #0069f900;
}
combo-box:disabled, check-box:checked:disabled, check-box:indeterminate:disabled, radio-button:checked:disabled {
  glyph-color: #ffffffb0;
}
/* A disabled default button loses the accent, as in AppKit. */
button:default:disabled::part(face) {
  background: #0000000a;
  border-color: #0000001f;
}
button:default:disabled {
  color: #00000040;
  default-outline: 1px #0000001f;
}
h-scroll-bar::part(track), v-scroll-bar::part(track) {
  background: #fafafa;
  border: none;
}
h-scroll-bar::part(thumb), v-scroll-bar::part(thumb) {
  background: #c1c1c1;
  border: 1px solid #949494;
  border-radius: 8px;
}
h-scroll-bar:disabled::part(thumb), v-scroll-bar:disabled::part(thumb) {
  background: #e6e6e6;
  border-color: #0000001f;
}
@media (prefers-color-scheme: light) {
  button::part(face), check-box::part(well), radio-button::part(well) {
    background: #ffffff;
  }
  button:active::part(face) {
    background: #e5e5e5;
  }
  button:disabled::part(face), button:default:disabled::part(face), check-box:disabled::part(well),
  radio-button:disabled::part(well) {
    background: #ffffff80;
  }
}
@media (prefers-color-scheme: dark) {
  button:default::part(face), combo-box::part(drop-down),
  check-box:checked::part(well), check-box:indeterminate::part(well), radio-button:checked::part(well) {
    background: #0a6cf0;
    border-color: #0a6cf0;
  }
  button:default:disabled::part(face) {
    background: #ffffff0a;
    border-color: #ffffff1f;
  }
  button:default:disabled {
    color: #ffffff40;
    default-outline: 1px #ffffff1f;
  }
  h-scroll-bar:disabled::part(thumb), v-scroll-bar:disabled::part(thumb) {
    background: #3a3a3c;
    border-color: #ffffff1f;
  }
  h-scroll-bar::part(track), v-scroll-bar::part(track) {
    background: #2a2a2c;
  }
  h-scroll-bar::part(thumb), v-scroll-bar::part(thumb) {
    background: #6b6b6b;
    border-color: #676767;
  }
}
)css";

constexpr std::string_view macos_style_sheet = R"css(/* macos-style, light: a macOS-like look (stage T4, phase 7), following
   Puppertino 2.0 (https://puppertino.com/, puppertino.css): the elevated
   background #ffffff, translucent grey plates (#00000014, hot #0000001c,
   pressed #00000029) with 6-unit corners, text #000000d9, white fields
   with 6-unit corners and a soft shadow, the accent push button, the list
   selection #0165e2, the menu selection #0069f9 with white text, a
   6-unit progress track #00000016 with the accent fill #0088ff, boxes of
   #00000004 with 12-unit corners, help tags of #ececece0 with Puppertino's
   two shadows. Puppertino draws no frames: macOS separates its controls by
   fill and shadow.
   Contrast fixes on enabled elements (the boundary gate is 3:1, text
   4.5:1): plates, fields, cards and help tags gain a #949494 frame, and a
   hot toolbar button or row the same (every Puppertino edge is below 1.5:1
   on white); muted text is #00000090 (#00000080 is 3.7:1 on a pressed
   plate); the accent push button, badge and links are #0069f9 (white on
   the accent #0088ff is 3.5:1; Puppertino's menu selection blue passes)
   and the accent button presses to #005bd9; the focus ring is the opaque
   #0069f9 (Puppertino's #0088ff80 halo is under 3:1 on a plate), white on
   the accent; a pressed check box is #00000029 (muted text on
   Puppertino's #00000030 is 4.4:1). Disabled text keeps Puppertino's tertiary #00000040:
   disabled controls are exempt. */

* {
  color: #000000d9;
  muted-color: #00000090;
  glyph-color: #000000d9;
  outline-color: #0069f9;
  default-outline: 1px #0069f9;
}

:family(window), :family(panel) {
  background: #ffffff;
  border: 1px solid #0000001f;
  border-radius: 0;
}
:family(card) {
  background: #00000004;
  border: 1px solid #949494;
  border-radius: 12px;
}
:family(notice) {
  background: #00000004;
  border: 1px solid #949494;
  border-radius: 12px;
}
:family(tool-tip) {
  background: #ececece0;
  border: 1px solid #949494;
  border-radius: 6px;
  box-shadow: 0 2px 8px #00000033, 0 0 2px #00000066;
  color: #1a1a1a;
  glyph-color: #1a1a1a;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #00000024;
  border-color: #949494;
}

:family(button) {
  background: #00000014;
  border: 1px solid #949494;
  border-radius: 6px;
}
:family(button):hover {
  background: #0000001c;
}
:family(button):active, :family(button):state(selected) {
  background: #00000029;
}
:family(button):state(pending), :family(choice):state(pending) {
  border-color: #8a6100;
}
:family(button):invalid, :family(choice):invalid {
  border-color: #d6513c;
}
:family(button):disabled {
  background: #0000000a;
  border-color: #0000001f;
}

/* Check and radio boxes: Puppertino's #0000001a box with 5-unit corners;
   checked, the mark takes the accent (the box's fill would lie under the
   label too). */
:family(choice) {
  background: #0000001a;
  border: 1px solid #949494;
  border-radius: 5px;
}
:family(choice):active {
  background: #00000029;
}
:family(choice):state(selected) {
  glyph-color: #0069f9;
}
:family(choice):disabled {
  background: #0000000d;
  border-color: #0000001f;
}

:family(accent-button) {
  background: #0069f9;
  border: 1px solid #0069f9;
  border-radius: 6px;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):active {
  background: #005bd9;
  border-color: #005bd9;
}
:family(accent-button):state(pending) {
  border-color: #8a6100;
}
:family(accent-button):invalid {
  border-color: #d6513c;
}
:family(accent-button):disabled {
  background: #0069f966;
  border-color: #0069f900;
  color: #ffffff80;
  muted-color: #ffffff80;
  glyph-color: #ffffff80;
  outline-color: #0069f9;
}

/* Toolbar buttons: Puppertino's capsules, clear at rest, #0000001a hot. */
:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 14px;
}
:family(command-button):hover {
  background: #0000001a;
  border-color: #949494;
}
:family(command-button):active, :family(command-button):state(selected) {
  background: #00000029;
  border-color: #949494;
}

:family(editor) {
  background: #ffffff;
  border: 1px solid #949494;
  border-radius: 6px;
  box-shadow: 0 0.5px 2.5px #0000004d;
}
:family(editor):active {
  border-color: #007aff;
}
:family(editor):state(pending) {
  border-color: #8a6100;
}
:family(editor):invalid {
  border-color: #d6513c;
}
:family(editor):disabled {
  background: #ffffff80;
  border-color: #0000000a;
  box-shadow: none;
}

:family(progress) {
  background: #00000016;
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
:family(progress):state(selected) {
  background: #0088ff;
  color: #000000;
  muted-color: #000000;
  glyph-color: #000000;
}
:family(progress):disabled, :family(progress):state(selected):disabled {
  background: #0000000d;
}

:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 6px;
}
:family(menu-item):hover, :family(menu-item):active, :family(menu-item):state(selected) {
  background: #0069f9;
  border-color: #0069f9;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(selection):hover, :family(selection):active {
  background: #0000000d;
  border-color: #949494;
}
:family(selection):state(selected) {
  background: #0165e2;
  border-color: #0165e2;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(menu-item):state(selected):disabled, :family(selection):state(selected):disabled {
  background: #00000024;
  border-color: #ffffff00;
  outline-color: #0069f9;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
  color: #0069f9;
  glyph-color: #0069f9;
}

:family(badge) {
  background: #0069f9;
  border: 1px solid #0069f9;
  border-radius: 6px;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #0000000a;
  border-color: #ffffff00;
}

:disabled, :state(selected):disabled {
  color: #00000040;
  muted-color: #00000040;
  glyph-color: #00000040;
}
)css";

constexpr std::string_view macos_style_dark_sheet = R"css(/* macos-style, dark: Puppertino 2.0's dark appearance (.p-dark-mode):
   the elevated background #1c1c1e, translucent white plates (#ffffff12,
   hot #ffffff1c, pressed #ffffff29), text #ffffffd9, fields #1e1e1e, the
   list selection #0a6cf0, the menu selection #0069f9, a #ffffff16
   progress track with the accent fill #0091ff, help tags of #00000080
   with a #ffffff26 inner rim. The light sheet's shapes stand.
   Contrast fixes on enabled elements: plates, fields, cards, help tags,
   hot toolbar buttons and rows gain a #676767 frame (Puppertino's edges
   are below 1.5:1 on #1c1c1e); muted text is #ffffff8f (#ffffff8c is
   4.4:1 on a pressed plate); the accent push button and badge are
   #0a6cf0 (white on the accent #0091ff is 3.2:1; Puppertino's dark list
   selection passes), pressing to #0a64e0 (#0a5fd6's edge is 2.9:1 on
   #1c1c1e); a pressed check box is
   #ffffff29 (the accent mark on #ffffff30 is 2.9:1). Disabled text keeps
   Puppertino's #ffffff40: disabled controls are exempt. */

* {
  color: #ffffffd9;
  muted-color: #ffffff8f;
  glyph-color: #ffffffd9;
  outline-color: #0091ff;
  default-outline: 1px #0091ff;
}

:family(window), :family(panel) {
  background: #1c1c1e;
  border: 1px solid #ffffff1a;
  border-radius: 0;
}
:family(card) {
  background: #ffffff04;
  border: 1px solid #676767;
  border-radius: 12px;
}
:family(notice) {
  background: #ffffff04;
  border: 1px solid #676767;
  border-radius: 12px;
}
:family(tool-tip) {
  background: #00000080;
  border: 1px solid #676767;
  border-radius: 6px;
  keylines: #ffffff26;
  color: #f5f5f5;
  glyph-color: #f5f5f5;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #ffffff24;
  border-color: #676767;
}

:family(button) {
  background: #ffffff12;
  border: 1px solid #676767;
  border-radius: 6px;
}
:family(button):hover {
  background: #ffffff1c;
}
:family(button):active, :family(button):state(selected) {
  background: #ffffff29;
}
:family(button):state(pending), :family(choice):state(pending) {
  border-color: #e0a000;
}
:family(button):invalid, :family(choice):invalid {
  border-color: #ff4245;
}
:family(button):disabled {
  background: #ffffff0a;
  border-color: #ffffff1a;
}

:family(choice) {
  background: #ffffff1a;
  border: 1px solid #676767;
  border-radius: 5px;
}
:family(choice):active {
  background: #ffffff29;
}
:family(choice):state(selected) {
  glyph-color: #0091ff;
}
:family(choice):disabled {
  background: #ffffff0a;
  border-color: #ffffff1a;
}

:family(accent-button) {
  background: #0a6cf0;
  border: 1px solid #0a6cf0;
  border-radius: 6px;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):active {
  background: #0a64e0;
  border-color: #0a64e0;
}
:family(accent-button):state(pending) {
  border-color: #e0a000;
}
:family(accent-button):invalid {
  border-color: #ff4245;
}
:family(accent-button):disabled {
  background: #0a6cf066;
  border-color: #0a6cf000;
  color: #ffffff80;
  muted-color: #ffffff80;
  glyph-color: #ffffff80;
  outline-color: #0091ff;
}

:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 14px;
}
:family(command-button):hover {
  background: #ffffff17;
  border-color: #676767;
}
:family(command-button):active, :family(command-button):state(selected) {
  background: #ffffff29;
  border-color: #676767;
}

:family(editor) {
  background: #1e1e1e;
  border: 1px solid #676767;
  border-radius: 6px;
  box-shadow: 0 0.5px 2.5px #0000004d;
}
:family(editor):active {
  border-color: #0091ff;
}
:family(editor):state(pending) {
  border-color: #e0a000;
}
:family(editor):invalid {
  border-color: #ff4245;
}
:family(editor):disabled {
  background: #1e1e1e80;
  border-color: #ffffff0a;
  box-shadow: none;
}

:family(progress) {
  background: #ffffff16;
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
:family(progress):state(selected) {
  background: #0091ff;
  color: #000000;
  muted-color: #000000;
  glyph-color: #000000;
}
:family(progress):disabled, :family(progress):state(selected):disabled {
  background: #ffffff0e;
}

:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 6px;
}
:family(menu-item):hover, :family(menu-item):active, :family(menu-item):state(selected) {
  background: #0069f9;
  border-color: #0069f9;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(selection):hover, :family(selection):active {
  background: #ffffff0d;
  border-color: #676767;
}
:family(selection):state(selected) {
  background: #0a6cf0;
  border-color: #0a6cf0;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(menu-item):state(selected):disabled, :family(selection):state(selected):disabled {
  background: #ffffff24;
  border-color: #ffffff00;
  outline-color: #0091ff;
}

:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
  color: #0091ff;
  glyph-color: #0091ff;
}

:family(badge) {
  background: #0a6cf0;
  border: 1px solid #0a6cf0;
  border-radius: 6px;
  color: #ffffff;
  muted-color: #ffffff;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #ffffff0a;
  border-color: #ffffff00;
}

:disabled, :state(selected):disabled {
  color: #ffffff40;
  muted-color: #ffffff40;
  glyph-color: #ffffff40;
}
)css";

// What a theme's sheet cannot say: its focus ring (dotted or solid, its
// width) and whether pressed content moves; and the compatibility colours
// controls outside the role model read.
struct FamiliarStructure final {
    bool dotted_focus{};
    double focus_width{1.0};
    Point pressed_content_offset{};
    Color face{};
    Color face_light{};
    Color paper{};
    Color border{};
    Color dark_border{};
    Color text{};
    Color disabled_text{};
    Color accent{};
    Color accent_light{};
    Color link{};
    Color visited_link{};
    // Whether scroll bars draw arrow buttons (ThemeGeometryTokens).
    bool scroll_arrows{true};
};

// One theme's description: its id, its floor sheet, its dark floor sheet
// (empty for a light-only theme) and its structure.
struct FamiliarDescription final {
    std::string_view id{};
    std::string_view sheet{};
    std::string_view file{};
    std::string_view dark_sheet{};
    std::string_view dark_file{};
    // Compiled rules over the evaluated floor (parts and fonts the floor's
    // recipes cannot carry); empty for none.
    std::string_view parts_sheet{};
    std::string_view parts_file{};
    FamiliarStructure structure{};
};

constexpr std::array<FamiliarDescription, familiar_theme_count> descriptions{{
    {.id = "classic",
     .sheet = classic_sheet,
     .file = "classic/floor.css",
     .parts_sheet = classic_parts_sheet,
     .parts_file = "classic/parts.css",
     .structure = {.dotted_focus = true,
                   .focus_width = 1.0,
                   .pressed_content_offset = Point{1.0, 1.0},
                   .face = Color::rgba(212, 208, 200),
                   .face_light = Color::rgba(255, 255, 255),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(128, 128, 128),
                   .dark_border = Color::rgba(64, 64, 64),
                   .text = Color::rgba(0, 0, 0),
                   .disabled_text = Color::rgba(128, 128, 128),
                   .accent = Color::rgba(10, 36, 106),
                   .accent_light = Color::rgba(212, 208, 200),
                   .link = Color::rgba(0, 0, 255),
                   .visited_link = Color::rgba(128, 0, 128)}},
    {.id = "modern-flat",
     .sheet = modern_flat_sheet,
     .file = "modern-flat/floor.css",
     .structure = {.dotted_focus = false,
                   .focus_width = 2.0,
                   .pressed_content_offset = Point{},
                   .face = Color::rgba(243, 243, 243),
                   .face_light = Color::rgba(253, 253, 253),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(118, 118, 118),
                   .dark_border = Color::rgba(27, 27, 27),
                   .text = Color::rgba(27, 27, 27),
                   .disabled_text = Color::rgba(110, 110, 110),
                   .accent = Color::rgba(0, 103, 192),
                   .accent_light = Color::rgba(204, 228, 247),
                   .link = Color::rgba(0, 103, 192),
                   .visited_link = Color::rgba(107, 47, 160)}},
    {.id = "xp-style",
     .sheet = xp_style_sheet,
     .file = "xp-style/floor.css",
     .parts_sheet = xp_style_parts_sheet,
     .parts_file = "xp-style/parts.css",
     .structure = {.dotted_focus = true,
                   .focus_width = 1.0,
                   .pressed_content_offset = Point{1.0, 1.0},
                   .face = Color::rgba(236, 233, 216),
                   .face_light = Color::rgba(244, 243, 238),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(79, 111, 153),
                   .dark_border = Color::rgba(0, 60, 116),
                   .text = Color::rgba(0, 0, 0),
                   .disabled_text = Color::rgba(172, 168, 153),
                   .accent = Color::rgba(34, 103, 203),
                   .accent_light = Color::rgba(221, 231, 247),
                   .link = Color::rgba(0, 0, 255),
                   .visited_link = Color::rgba(128, 0, 128)}},
    {.id = "windows7-style",
     .sheet = windows7_style_sheet,
     .file = "windows7-style/floor.css",
     .parts_sheet = windows7_style_parts_sheet,
     .parts_file = "windows7-style/parts.css",
     .structure = {.dotted_focus = true,
                   .focus_width = 1.0,
                   .pressed_content_offset = Point{},
                   .face = Color::rgba(240, 240, 240),
                   .face_light = Color::rgba(246, 246, 246),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(137, 138, 143),
                   .dark_border = Color::rgba(137, 138, 138),
                   .text = Color::rgba(34, 34, 34),
                   .disabled_text = Color::rgba(131, 131, 131),
                   .accent = Color::rgba(44, 98, 139),
                   .accent_light = Color::rgba(204, 232, 255),
                   .link = Color::rgba(0, 102, 204),
                   .visited_link = Color::rgba(128, 0, 128)}},
    {.id = "macos-style",
     .sheet = macos_style_sheet,
     .file = "macos-style/floor.css",
     .dark_sheet = macos_style_dark_sheet,
     .dark_file = "macos-style/floor-dark.css",
     .parts_sheet = macos_style_parts_sheet,
     .parts_file = "macos-style/parts.css",
     .structure = {.dotted_focus = false,
                   .focus_width = 2.0,
                   .pressed_content_offset = Point{},
                   .face = Color::rgba(235, 235, 235),
                   .face_light = Color::rgba(255, 255, 255),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(148, 148, 148),
                   .dark_border = Color::rgba(148, 148, 148),
                   .text = Color::rgba(38, 38, 38),
                   .disabled_text = Color::rgba(191, 191, 191),
                   .accent = Color::rgba(0, 105, 249),
                   .accent_light = Color::rgba(214, 214, 214),
                   .link = Color::rgba(0, 105, 249),
                   .visited_link = Color::rgba(0, 105, 249),
                   .scroll_arrows = false}},
    // windows-professional (owner, 2026-11-10): its sheets and their notes
    // are in windows_professional_sheets.hpp. A solid one-unit focus
    // rectangle (Windows 10) and Windows 2000's pressed content offset.
    {.id = "windows-professional",
     .sheet = detail::windows_professional_floor_sheet,
     .file = "windows-professional/floor.css",
     .dark_sheet = detail::windows_professional_dark_floor_sheet,
     .dark_file = "windows-professional/floor-dark.css",
     .parts_sheet = detail::windows_professional_parts_sheet,
     .parts_file = "windows-professional/parts.css",
     .structure = {.dotted_focus = false,
                   .focus_width = 1.0,
                   .pressed_content_offset = Point{1.0, 1.0},
                   .face = Color::rgba(238, 242, 248),
                   .face_light = Color::rgba(251, 252, 254),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(138, 153, 174),
                   .dark_border = Color::rgba(110, 127, 152),
                   .text = Color::rgba(27, 37, 51),
                   .disabled_text = Color::rgba(106, 122, 144),
                   .accent = Color::rgba(46, 107, 181),
                   .accent_light = Color::rgba(219, 234, 250),
                   .link = Color::rgba(31, 79, 143),
                   .visited_link = Color::rgba(106, 61, 153)}},
}};

// The recipe the sheet's fields are evaluated onto: the theme's focus
// ring and pressed offset, and an empty material.
ControlVisualRecipe recipe_structure(const FamiliarStructure& structure) {
    ControlVisualRecipe result{};
    result.material.fills.clear();
    result.material.border.reset();
    result.focus_width = structure.focus_width;
    result.dotted_focus = structure.dotted_focus;
    result.pressed_content_offset = structure.pressed_content_offset;
    return result;
}

// A theme's evaluated floor: its definition with the floor sheet's
// records and, when it has a dark sheet that compiles, the dark sheet's as
// its authored dark records; and what evaluating them found.
struct FamiliarFloor final {
    std::unique_ptr<ThemeDefinition> definition{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::uint64_t operations{};
};

FamiliarFloor evaluate_familiar_floor(const FamiliarDescription& description) {
    FamiliarFloor result{};
    // Some 100 KB of recipes: built on the heap, not the stack
    // (fill_professional_role's note in theme.cpp). Windows-professional's
    // structural definition supplies the structure tokens, accent ramp and
    // overlay shadow.
    result.definition = std::make_unique<ThemeDefinition>(windows_professional_theme_definition());
    ThemeDefinition& definition = *result.definition;
    definition.id = std::string(description.id);
    const FamiliarStructure& structure = description.structure;
    definition.structure.geometry.scroll_arrows = structure.scroll_arrows;
    definition.compatibility.face = structure.face;
    definition.compatibility.face_light = structure.face_light;
    definition.compatibility.paper = structure.paper;
    definition.compatibility.border = structure.border;
    definition.compatibility.dark_border = structure.dark_border;
    definition.compatibility.text = structure.text;
    definition.compatibility.disabled_text = structure.disabled_text;
    definition.compatibility.accent = structure.accent;
    definition.compatibility.accent_light = structure.accent_light;
    definition.compatibility.link = structure.link;
    definition.compatibility.visited_link = structure.visited_link;
    detail::FloorSheetEvaluation evaluated =
        detail::evaluate_floor_sheet(description.file, description.sheet, recipe_structure(structure), definition);
    result.diagnostics = std::move(evaluated.diagnostics);
    result.operations = evaluated.operations;
    // Light only unless the theme has a dark sheet (the header): then the
    // dark sheet is evaluated the same way onto a copy of the definition,
    // and its ordinary and selected records become the theme's authored
    // dark ones. A refused dark sheet leaves the theme light only.
    definition.dark_variant = ThemeDarkVariant::none;
    definition.colour_schemes = ColourSchemeSet{.light = true, .dark = false};
    if (!description.dark_sheet.empty()) {
        const std::unique_ptr<ThemeDefinition> dark = std::make_unique<ThemeDefinition>(definition);
        detail::FloorSheetEvaluation dark_evaluated = detail::evaluate_floor_sheet(
            description.dark_file, description.dark_sheet, recipe_structure(structure), *dark);
        result.diagnostics.insert(result.diagnostics.end(), dark_evaluated.diagnostics.begin(),
                                  dark_evaluated.diagnostics.end());
        result.operations += dark_evaluated.operations;
        if (dark_evaluated.evaluated) {
            for (std::size_t index = 0U; index < control_visual_role_count; ++index) {
                definition.roles[index].dark = (*dark).roles[index].ordinary;
                definition.roles[index].dark_selected = (*dark).roles[index].selected;
            }
            definition.dark_variant = ThemeDarkVariant::authored;
            definition.colour_schemes = ColourSchemeSet{.light = true, .dark = true};
        }
    }
    return result;
}

FamiliarThemeBuild build_familiar_theme(const FamiliarDescription& description) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    FamiliarThemeBuild result{};
    FamiliarFloor floor = evaluate_familiar_floor(description);
    result.diagnostics = std::move(floor.diagnostics);
    result.compile_operations = floor.operations;
    const std::unique_ptr<ThemeDefinition> definition = std::move(floor.definition);
    result.theme = Theme::create(*definition);
    if (!description.parts_sheet.empty()) {
        // A refused parts sheet leaves the theme its floor alone.
        detail::BuiltInThemeCompilation compiled = detail::compile_built_in_theme(
            *definition, description.parts_file, description.parts_sheet, stock_control_anatomies());
        result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(), compiled.diagnostics.end());
        result.compile_operations += compiled.operations;
        if (compiled.theme != nullptr) result.theme = std::move(compiled.theme);
    }
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    result.build_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return result;
}

// Each theme's build, made on its first use. Function-local statics make
// each initialization thread-safe; a build is never changed afterwards.
const FamiliarThemeBuild& classic_build() {
    static const FamiliarThemeBuild build = build_familiar_theme(descriptions[0]);
    return build;
}
const FamiliarThemeBuild& modern_flat_build() {
    static const FamiliarThemeBuild build = build_familiar_theme(descriptions[1]);
    return build;
}
const FamiliarThemeBuild& xp_style_build() {
    static const FamiliarThemeBuild build = build_familiar_theme(descriptions[2]);
    return build;
}
const FamiliarThemeBuild& windows7_style_build() {
    static const FamiliarThemeBuild build = build_familiar_theme(descriptions[3]);
    return build;
}
const FamiliarThemeBuild& macos_style_build() {
    static const FamiliarThemeBuild build = build_familiar_theme(descriptions[4]);
    return build;
}
const FamiliarThemeBuild& windows_professional_build() {
    static const FamiliarThemeBuild build = build_familiar_theme(descriptions[5]);
    return build;
}

} // namespace

std::string_view familiar_theme_id(FamiliarTheme theme) noexcept {
    const std::size_t index = static_cast<std::size_t>(theme);
    if (index >= familiar_theme_count) return {};
    return descriptions[index].id;
}

std::optional<FamiliarTheme> familiar_theme_named(std::string_view id) noexcept {
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        if (descriptions[index].id == id) return static_cast<FamiliarTheme>(index);
    }
    return std::nullopt;
}

std::string_view familiar_theme_floor_sheet(FamiliarTheme theme) noexcept {
    const std::size_t index = static_cast<std::size_t>(theme);
    if (index >= familiar_theme_count) return {};
    return descriptions[index].sheet;
}

std::string_view familiar_theme_dark_floor_sheet(FamiliarTheme theme) noexcept {
    const std::size_t index = static_cast<std::size_t>(theme);
    if (index >= familiar_theme_count) return {};
    return descriptions[index].dark_sheet;
}

const FamiliarThemeBuild& familiar_theme_build(FamiliarTheme theme) {
    switch (theme) {
    case FamiliarTheme::modern_flat: return modern_flat_build();
    case FamiliarTheme::xp_style: return xp_style_build();
    case FamiliarTheme::windows7_style: return windows7_style_build();
    case FamiliarTheme::macos_style: return macos_style_build();
    case FamiliarTheme::windows_professional: return windows_professional_build();
    case FamiliarTheme::classic:
    case FamiliarTheme::count:
    // A value outside the themes (a caller's error, the header) is the
    // classic theme: the one fallback.
    default: break;
    }
    return classic_build();
}

std::shared_ptr<const Theme> familiar_theme(FamiliarTheme theme) {
    const std::shared_ptr<const Theme> result = familiar_theme_build(theme).theme;
    return result;
}

namespace detail {

const ThemeDefinition& windows_professional_floor_definition() {
    // Evaluated once, on the first call's thread (C++ makes the
    // initialization thread-safe), and never changed afterwards. Its
    // diagnostics are the theme build's (familiar_theme_build), which the
    // familiar themes tests require to be empty.
    static const std::unique_ptr<ThemeDefinition> definition =
        std::move(evaluate_familiar_floor(descriptions[static_cast<std::size_t>(FamiliarTheme::windows_professional)])
                      .definition);
    return *definition;
}

} // namespace detail

} // namespace gui_forms
