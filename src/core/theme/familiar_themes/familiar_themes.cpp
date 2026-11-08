#include "gui_forms/theme/familiar_themes/familiar_themes.hpp"

#include "../theme_compiler/built_in_theme.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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

constexpr std::string_view classic_sheet = R"css(/* classic: the Windows 98 look (stage T4, phase 7), following XP.css's
   Windows 98 sheet (https://botoxparty.github.io/XP.css/, 98.css): the
   silver face #c0c0c0, two-ring bevels (white and #dfdfdf against #808080
   and #0a0a0a), sunken white wells, navy #000080 highlights. Our keylines
   draw one inner ring, so each bevel is its frame plus one ring.
   Contrast fixes on enabled elements: a button's frame is #0a0a0a on
   every side (98.css's white top-left edge is 1.8:1 on silver; the
   boundary gate is 3:1), and a well's or group's dark top-left edge is
   #696969 (98.css's #808080 is 2.2:1); a toolbar button's thin bevel has
   a #696969 frame; the pending frame is #705800 (#8a6d00 is 2.7:1 on
   silver). Disabled text keeps 98.css's #808080: disabled controls are
   exempt from the gate. */

* {
  color: #000000;
  muted-color: #3c3c3c;
  glyph-color: #000000;
  outline-color: #000000;
  default-outline: 1px #0a0a0a;
}

:family(window), :family(panel) {
  background: #c0c0c0;
  border: 1px solid #808080;
  border-radius: 0;
}
:family(card), :family(notice) {
  background: #c0c0c0;
  border: 1px solid #696969;
  border-color: #696969 #ffffff #ffffff #696969;
  border-radius: 0;
  keylines: #ffffff #808080 #808080 #ffffff;
}
:family(tool-tip) {
  background: #ffffe1;
  border: 1px solid #000000;
  border-radius: 0;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #dfdfdf;
  border-color: #0a0a0a;
}

/* Push buttons: raised, sunken while pressed or checked (a checked toggle
   is the lighter face). The default button gains 98.css's outer black
   ring (default-outline). */
:family(button), :family(accent-button) {
  background: #c0c0c0;
  border: 1px solid #0a0a0a;
  border-radius: 0;
  keylines: #ffffff #808080 #808080 #ffffff;
}
:family(button):active, :family(accent-button):active, :family(button):state(selected) {
  keylines: #808080 #dfdfdf #dfdfdf #808080;
}
:family(button):state(selected) {
  background: #dfdfdf;
}
:family(button):state(pending), :family(accent-button):state(pending) {
  border-color: #705800;
}
:family(button):invalid, :family(accent-button):invalid {
  border-color: #a00000;
}
:family(button):disabled, :family(accent-button):disabled {
  border-color: #808080;
}

/* Toolbar buttons: flat at rest, a thin raised bevel while hot, sunken
   while pressed or checked. */
:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
  keylines: none;
}
:family(command-button):hover, :family(command-button):active, :family(command-button):state(selected) {
  border-color: #696969;
  keylines: #ffffff #808080 #808080 #ffffff;
}
:family(command-button):active, :family(command-button):state(selected) {
  keylines: #808080 #ffffff #ffffff #808080;
}
:family(command-button):state(selected) {
  background: #dfdfdf;
}

/* Wells: white fields sunken two rings deep, as 98.css's (outer ring
   #808080 top-left and white bottom-right, inner ring #0a0a0a and
   #dfdfdf). A progress bar's fill is the navy highlight. */
:family(choice), :family(editor), :family(progress) {
  background: #ffffff;
  border: 1px solid #696969;
  border-color: #696969 #ffffff #ffffff #696969;
  border-radius: 0;
  keylines: #0a0a0a #dfdfdf #dfdfdf #0a0a0a;
}
:family(choice):state(pending), :family(editor):state(pending), :family(progress):state(pending) {
  background: #fffbe0;
  border-color: #705800;
}
:family(choice):invalid, :family(editor):invalid, :family(progress):invalid {
  background: #fff0f0;
  border-color: #a00000;
}
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled {
  background: #c0c0c0;
  border-color: #808080 #ffffff #ffffff #808080;
}
:family(progress):state(selected) {
  background: #000080;
  border-color: #000080;
  keylines: none;
  color: #ffffff;
  muted-color: #dfdfdf;
  glyph-color: #ffffff;
}
:family(progress):state(selected):disabled {
  background: #c0c0c0;
  border-color: #808080 #ffffff #ffffff #808080;
  keylines: #0a0a0a #dfdfdf #dfdfdf #0a0a0a;
}

/* Menu members and rows: clear at rest, the navy highlight with white
   text while hot, pressed or selected; the focus ring turns white on it. */
:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(menu-item):state(selected), :family(selection):hover,
:family(selection):active, :family(selection):state(selected) {
  background: #000080;
  border-color: #000080;
  color: #ffffff;
  muted-color: #dfdfdf;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(menu-item):state(selected):disabled, :family(selection):state(selected):disabled {
  background: #c0c0c0;
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
  background: #000080;
  border: 1px solid #000080;
  border-radius: 0;
  color: #ffffff;
  muted-color: #dfdfdf;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #c0c0c0;
  border-color: #808080;
}

:disabled, :state(selected):disabled {
  color: #808080;
  muted-color: #808080;
  glyph-color: #808080;
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
   #7f9db9 is 2.3:1 on the stock; the boundary gate is 3:1) and a card's
   frame #86867b (the fieldset's #d0d0bf is 1.3:1); a notice, ungated,
   keeps #d0d0bf. A toolbar button's hover frame is the navy one.
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
  border: 1px solid #d0d0bf;
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

:family(choice), :family(editor) {
  background: #ffffff;
  border: 1px solid #4f6f99;
  border-radius: 0;
}
:family(progress) {
  background: #ffffff;
  border: 1px solid #686868;
  border-radius: 4px;
}
:family(choice):hover, :family(editor):hover, :family(progress):hover, :family(choice):active,
:family(editor):active, :family(progress):active {
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
  background: #2267cb;
  border-color: #2267cb;
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
  background: #2267cb;
  border: 1px solid #2267cb;
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
  border: 1px solid #cdd7db;
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
};

// One theme's description: its id, its floor sheet and its structure.
struct FamiliarDescription final {
    std::string_view id{};
    std::string_view sheet{};
    std::string_view file{};
    FamiliarStructure structure{};
};

constexpr std::array<FamiliarDescription, familiar_theme_count> descriptions{{
    {.id = "classic",
     .sheet = classic_sheet,
     .file = "classic/floor.css",
     .structure = {.dotted_focus = true,
                   .focus_width = 1.0,
                   .pressed_content_offset = Point{1.0, 1.0},
                   .face = Color::rgba(192, 192, 192),
                   .face_light = Color::rgba(223, 223, 223),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(128, 128, 128),
                   .dark_border = Color::rgba(10, 10, 10),
                   .text = Color::rgba(0, 0, 0),
                   .disabled_text = Color::rgba(128, 128, 128),
                   .accent = Color::rgba(0, 0, 128),
                   .accent_light = Color::rgba(223, 223, 223),
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

FamiliarThemeBuild build_familiar_theme(const FamiliarDescription& description) {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    FamiliarThemeBuild result{};
    // Some 100 KB of recipes: built on the heap, not the stack
    // (fill_professional_role's note in theme.cpp). Windows-professional
    // supplies the structure tokens, accent ramp and overlay shadow.
    const std::unique_ptr<ThemeDefinition> definition =
        std::make_unique<ThemeDefinition>(windows_professional_theme_definition());
    (*definition).id = std::string(description.id);
    const FamiliarStructure& structure = description.structure;
    (*definition).compatibility.face = structure.face;
    (*definition).compatibility.face_light = structure.face_light;
    (*definition).compatibility.paper = structure.paper;
    (*definition).compatibility.border = structure.border;
    (*definition).compatibility.dark_border = structure.dark_border;
    (*definition).compatibility.text = structure.text;
    (*definition).compatibility.disabled_text = structure.disabled_text;
    (*definition).compatibility.accent = structure.accent;
    (*definition).compatibility.accent_light = structure.accent_light;
    (*definition).compatibility.link = structure.link;
    (*definition).compatibility.visited_link = structure.visited_link;
    detail::FloorSheetEvaluation evaluated =
        detail::evaluate_floor_sheet(description.file, description.sheet, recipe_structure(structure), *definition);
    result.diagnostics = std::move(evaluated.diagnostics);
    result.compile_operations = evaluated.operations;
    // Light only (the header): the ordinary records serve a dark context.
    (*definition).dark_variant = ThemeDarkVariant::none;
    (*definition).colour_schemes = ColourSchemeSet{.light = true, .dark = false};
    result.theme = Theme::create(*definition);
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

const FamiliarThemeBuild& familiar_theme_build(FamiliarTheme theme) {
    switch (theme) {
    case FamiliarTheme::modern_flat: return modern_flat_build();
    case FamiliarTheme::xp_style: return xp_style_build();
    case FamiliarTheme::windows7_style: return windows7_style_build();
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

} // namespace gui_forms
