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
// selected or not. Each sheet's comment names the values changed to meet
// the contrast gate.

constexpr std::string_view classic_sheet = R"css(/* classic: the Windows 2000 look (stage T4, phase 7).
   The button face #d4d0c8, bevels of white and #808080 one unit inside
   the frame, sunken wells, navy #0a246a highlights. Visual reference:
   XP.css's Windows 98 sheet (https://botoxparty.github.io/XP.css/).
   Changed for the contrast gate: the outer frame of buttons and wells is
   the dark shadow #404040 on every side (the period bevel's white top edge
   is 1.5:1 on the face; the gate's boundary is 3:1); a toolbar button's
   thin bevel has a #6d6d6d frame. Kept at the period value by the owner
   (2026-11-08), an accepted exception in the familiar themes' gate:
   disabled text #808080 (2.6:1 on the face, under the 3:1 gate). */

* {
  color: #000000;
  muted-color: #3c3c3c;
  glyph-color: #000000;
  outline-color: #000000;
  default-outline: 1px #000000;
}

:family(window), :family(panel) {
  background: #d4d0c8;
  border: 1px solid #808080;
  border-radius: 0;
}
:family(card), :family(notice) {
  background: #ffffff;
  border: 1px solid #404040;
  border-radius: 0;
}
:family(tool-tip) {
  background: #ffffe1;
  border: 1px solid #000000;
  border-radius: 0;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #e8e6e2;
  border-color: #404040;
}

/* Push buttons: raised, sunken while pressed or checked (a checked toggle
   is the lighter face). The default button's frame is black. */
:family(button), :family(accent-button) {
  background: #d4d0c8;
  border: 1px solid #404040;
  border-radius: 0;
  keylines: #ffffff #808080 #808080 #ffffff;
}
:family(accent-button) {
  border-color: #000000;
}
:family(button):active, :family(accent-button):active, :family(button):state(selected) {
  keylines: #808080 #d4d0c8 #d4d0c8 #808080;
}
:family(button):state(selected) {
  background: #e8e6e2;
}
:family(button):state(pending), :family(accent-button):state(pending) {
  border-color: #8a6d00;
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
  border-color: #6d6d6d;
  keylines: #ffffff #808080 #808080 #ffffff;
}
:family(command-button):active, :family(command-button):state(selected) {
  keylines: #808080 #ffffff #ffffff #808080;
}
:family(command-button):state(selected) {
  background: #e8e6e2;
}

/* Wells: white fields sunken two units deep. A progress bar's fill is the
   navy highlight. */
:family(choice), :family(editor), :family(progress) {
  background: #ffffff;
  border: 1px solid #404040;
  border-radius: 0;
  keylines: #808080 #d4d0c8 #d4d0c8 #808080;
}
:family(choice):state(pending), :family(editor):state(pending), :family(progress):state(pending) {
  background: #fffbe0;
  border-color: #8a6d00;
}
:family(choice):invalid, :family(editor):invalid, :family(progress):invalid {
  background: #fff0f0;
  border-color: #a00000;
}
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled {
  background: #d4d0c8;
  border-color: #808080;
}
:family(progress):state(selected) {
  background: #0a246a;
  border-color: #0a246a;
  keylines: none;
  color: #ffffff;
  muted-color: #d4d0c8;
  glyph-color: #ffffff;
}
:family(progress):state(selected):disabled {
  background: #d4d0c8;
  border-color: #808080;
  keylines: #808080 #d4d0c8 #d4d0c8 #808080;
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

constexpr std::string_view xp_style_sheet = R"css(/* xp-style: a Luna-like look (stage T4, phase 7), recreated: the beige
   stock #ece9d8, white-to-beige plates in navy #003c74 frames, an orange
   inner light while hot, the default button's blue inner frame, the
   highlight #316ac5 and the green progress fill. Visual reference: XP.css
   (https://botoxparty.github.io/XP.css/). Changed for the contrast gate:
   the field frame is #4f6f99 (Luna's #7f9db9 is 2.3:1 on the stock); a
   toolbar button's hover frame is the navy one. Kept at the period value
   by the owner (2026-11-08), an accepted exception in the familiar
   themes' gate: disabled text #aca899 (1.9:1 on the stock). */

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
  border: 1px solid #4f6f99;
  border-radius: 0;
}
:family(notice) {
  background: #ffffff;
  border: 1px solid #4f6f99;
  border-radius: 3px;
}
:family(tool-tip) {
  background: #ffffe1;
  border: 1px solid #000000;
  border-radius: 0;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(notice):state(selected), :family(tool-tip):state(selected) {
  background: #dde7f7;
  border-color: #316ac5;
}

:family(button), :family(accent-button) {
  background: linear-gradient(to bottom, #ffffff, #ece9d8);
  border: 1px solid #003c74;
  border-radius: 3px;
  keylines: none;
}
:family(accent-button) {
  keylines: #cee7ff #6982ee #6982ee #cee7ff;
}
:family(button):hover, :family(accent-button):hover {
  keylines: #fdd889 #f1a432 #f1a432 #fdd889;
}
:family(button):active, :family(accent-button):active, :family(button):state(selected) {
  background: linear-gradient(to bottom, #e3e2da, #f0efea);
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
  background: linear-gradient(to bottom, #fefefd, #ebeae4);
  border-color: #003c74;
}
:family(command-button):active {
  background: #e3e2da;
  border-color: #003c74;
}
:family(command-button):state(selected) {
  background: #e1e0d8;
  border-color: #003c74;
}

:family(choice), :family(editor), :family(progress) {
  background: #ffffff;
  border: 1px solid #4f6f99;
  border-radius: 0;
}
:family(choice):hover, :family(editor):hover, :family(progress):hover, :family(choice):active,
:family(editor):active, :family(progress):active {
  border-color: #316ac5;
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
  background: #2fb02f;
  border-color: #2fb02f;
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

constexpr std::string_view windows7_style_sheet = R"css(/* windows7-style: an Aero-like look (stage T4, phase 7), recreated:
   the stock #f0f0f0, grey glossy plates in #707070 frames, sky-blue hover
   and press, pale blue item highlights, the green progress fill. Its
   window glass needs T5's glass material; until then windows are opaque
   stock. Visual reference: 7.css (https://khang-nd.github.io/7.css/).
   Changed for the contrast gate: the field frame is #7a7a7a (the period's
   #abadb3 is 2.0:1 on the stock); the press gradient ends at #98d1ef
   (muted text on the period's #6db3df is 3.7:1). Kept at the period value
   by the owner (2026-11-08), an accepted exception in the familiar
   themes' gate: the default button's frame #3399ff (2.6:1 on the stock,
   under the 3:1 boundary gate). */

* {
  color: #000000;
  muted-color: #4c4c4c;
  glyph-color: #000000;
  outline-color: #000000;
  default-outline: 1px #3399ff;
}

:family(window), :family(panel) {
  background: #f0f0f0;
  border: 1px solid #a0a0a0;
  border-radius: 0;
}
:family(card) {
  background: #ffffff;
  border: 1px solid #7a7a7a;
  border-radius: 0;
}
:family(notice) {
  background: #ffffff;
  border: 1px solid #7a7a7a;
  border-radius: 3px;
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
  background: linear-gradient(to bottom, #f6f6f6, #d9d9d9);
  border: 1px solid #707070;
  border-radius: 3px;
  keylines: #ffffff #e5e5e5 #e5e5e5 #ffffff;
}
:family(accent-button) {
  border-color: #3399ff;
  keylines: #bfe5f9 #8ccbf2 #8ccbf2 #bfe5f9;
}
:family(button):hover, :family(accent-button):hover {
  background: linear-gradient(to bottom, #eaf6fd, #a7d9f5);
  border-color: #3c7fb1;
}
:family(button):active, :family(accent-button):active, :family(button):state(selected) {
  background: linear-gradient(to bottom, #c4e5f6, #98d1ef);
  border-color: #2c628b;
  keylines: none;
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
  background: linear-gradient(to bottom, #fafbfd, #e1edf9);
  border-color: #3c7fb1;
}
:family(command-button):active, :family(command-button):state(selected) {
  background: linear-gradient(to bottom, #c4e5f6, #98d1ef);
  border-color: #2c628b;
}

:family(choice), :family(editor), :family(progress) {
  background: #ffffff;
  border: 1px solid #7a7a7a;
  border-radius: 0;
}
:family(choice):hover, :family(editor):hover, :family(progress):hover, :family(choice):active,
:family(editor):active, :family(progress):active {
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
  background: #f4f4f4;
  border-color: #c5c5c5;
}
:family(progress):state(selected) {
  background: #06b025;
  border-color: #06b025;
  muted-color: #1a1a1a;
}
:family(progress):state(selected):disabled {
  background: #f4f4f4;
  border-color: #c5c5c5;
}

:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
}
:family(selection) {
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(selection):hover, :family(selection):active {
  background: linear-gradient(to bottom, #f1f7fe, #d8ebfb);
  border-color: #3c7fb1;
}
:family(menu-item):state(selected), :family(selection):state(selected) {
  background: #cce8ff;
  border-color: #3c7fb1;
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
  color: #6d6d6d;
  muted-color: #6d6d6d;
  glyph-color: #6d6d6d;
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
                   .face = Color::rgba(212, 208, 200),
                   .face_light = Color::rgba(232, 230, 226),
                   .paper = Color::rgba(255, 255, 255),
                   .border = Color::rgba(128, 128, 128),
                   .dark_border = Color::rgba(64, 64, 64),
                   .text = Color::rgba(0, 0, 0),
                   .disabled_text = Color::rgba(128, 128, 128),
                   .accent = Color::rgba(10, 36, 106),
                   .accent_light = Color::rgba(232, 230, 226),
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
                   .accent = Color::rgba(49, 106, 197),
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
                   .border = Color::rgba(122, 122, 122),
                   .dark_border = Color::rgba(112, 112, 112),
                   .text = Color::rgba(0, 0, 0),
                   .disabled_text = Color::rgba(109, 109, 109),
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
