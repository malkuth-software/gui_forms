#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"

#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include "../theme_compiler/built_in_theme.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// -- the floor sheet -----------------------------------------------------------------
//
// The families' recipes, written in the theme language (T4 floor sheet,
// owner option 1) and evaluated into the floor's recipes when the theme is
// built (detail::fill_family_recipes). Each value is Plan Paint's
// (src/forms/ribbon.cpp at 5e28a5e) unless its comment says otherwise; a
// value changed to meet the contrast gate says so. The C++ keeps only the
// structure (recipe_structure).

constexpr std::string_view floor_sheet = R"css(/* gui-forms: the default theme's floor (stage T4, owner option 1).
   Each family's recipe in every surface state: :hover is hot, :active
   pressed, :state(pending), :invalid, :disabled, :state(deactivated), and
   :state(selected) the selected records. Evaluated once, when the theme is
   built, into the recipes every control painting from its family's recipe
   draws; the light sheet then styles the parts the engine paints.

   Inks: #1b2733 on the near-white stock and on paper (design language
   5.4); #153765 Plan Paint's action ink; muted #4a545f, darker than
   windows-professional's #76808a, which fails 4.5:1 on its own plates,
   #2c3a4e on action plates and #f4f7fc on the accent blue; disabled
   Plan Paint's own #627997 (disabled text is not held to the contrast
   gate, as WCAG exempts inactive components; owner, 2026-11-08; M104:
   reduced, still visible).
   The focus ring is dark ink, a non-colour cue, dotted (the structure);
   the default button's second perimeter (M105) is Plan Paint's. */

* {
  muted-color: #4a545f;
  outline-color: #1b2733;
  default-outline: 1px #214b81;
  scrim-color: rgb(27 39 51 / 40%);
}

/* The dial parts (row 2.5; owner option (a)): a gauge's or dial's arcs are
   6 units thick, its needle a blade 4 units wide at the hub. The track arc
   is the progress border's colour, the value arc the selected fill's, an
   alarm segment the invalid or pending border's. */
:family(progress) {
  stroke-width: 6px;
  needle-shape: tapered;
}

/* Flat stock (windows, panels, cards and tooltips): one solid face with a
   quiet line. A selected one takes the pale selection. Tooltips are
   windows-professional's pale yellow (owner decision RB-38), with black
   text (owner mandate 2026-11-11). */
:family(window), :family(panel), :family(card), :family(tool-tip) {
  color: #1b2733;
  glyph-color: #1b2733;
}
:family(window) {
  background: #f4f4f5;
  border: 1px solid #b4bac2;
  border-radius: 0;
}
:family(panel) {
  background: #f4f4f5;
  border: 1px solid #cdd1d6;
  border-radius: 0;
}
:family(card) {
  background: #ffffff;
  border: 1px solid #70829c;
  border-radius: 4px;
}
:family(tool-tip) {
  background: #ffffe1;
  border: 1px solid #686f76;
  border-radius: 0;
  color: #000000;
}
:family(window):state(selected), :family(panel):state(selected), :family(card):state(selected),
:family(tool-tip):state(selected) {
  background: #cfe2fa;
  border-color: #6384b2;
}

/* Plan Paint's action plate, radius 3: a vertical gradient inside a
   one-unit border, relieved by inner lines (light along the top and left,
   dark along the right and bottom) and set on its hard contact shadow one
   unit below. Pressed inverts the inner lines and drops the shadow. The
   pending and invalid tints are windows-professional's, relieved. */
:family(button) {
  background: linear-gradient(to bottom, #d2e4fb, #a3c1ec);
  border: 1px solid #6384b2;
  border-radius: 3px;
  keylines: #ffffffe6 #31568687 #31568687 #ffffffe6;
  box-shadow: 0 1px 0 #294b7755;
  color: #153765;
  muted-color: #2c3a4e;
  glyph-color: #153765;
}
:family(button):hover {
  background: linear-gradient(to bottom, #dae9fc, #b1ccf2);
  border-color: #355e96;
}
:family(button):active {
  background: linear-gradient(to bottom, #98b5da, #b5cceb);
  keylines: #2c568482 #ffffff9b #ffffff9b #2c568482;
  box-shadow: none;
}
:family(button):state(pending) {
  background: linear-gradient(to bottom, #fff9da, #f6e59d);
  border-color: #ad841f;
}
:family(button):invalid {
  background: linear-gradient(to bottom, #ffefef, #f7cfcf);
  border-color: #b23636;
}

/* A command member draws no plate at rest, disabled or deactivated:
   dividers and strip shading carry its structure (design language 12.3),
   and hover and press bring the full plate. At rest its ink is the
   strip's stock ink. */
:family(command-button) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 3px;
  keylines: none;
  box-shadow: none;
  color: #1b2733;
  muted-color: #2c3a4e;
  glyph-color: #1b2733;
}
:family(command-button):hover, :family(command-button):active, :family(command-button):state(pending),
:family(command-button):invalid {
  keylines: #ffffffe6 #31568687 #31568687 #ffffffe6;
  box-shadow: 0 1px 0 #294b7755;
  color: #153765;
  glyph-color: #153765;
}
:family(command-button):hover {
  background: linear-gradient(to bottom, #dae9fc, #b1ccf2);
  border-color: #355e96;
}
:family(command-button):active {
  background: linear-gradient(to bottom, #98b5da, #b5cceb);
  border-color: #6384b2;
  keylines: #2c568482 #ffffff9b #ffffff9b #2c568482;
  box-shadow: none;
}
:family(command-button):state(pending) {
  background: linear-gradient(to bottom, #fff9da, #f6e59d);
  border-color: #ad841f;
}
:family(command-button):invalid {
  background: linear-gradient(to bottom, #ffefef, #f7cfcf);
  border-color: #b23636;
}

/* A disabled action is the disabled plate: no contact shadow, softer
   lines, disabled ink. */
:family(button):disabled, :family(command-button):disabled {
  color: #627997;
  muted-color: #627997;
  glyph-color: #627997;
}
:family(button):disabled {
  background: linear-gradient(to bottom, #becfe4, #b5c7de);
  border-color: #8fa6c464;
  keylines: #ffffff64 #31568687 #31568687 #ffffff64;
  box-shadow: none;
}

/* A selected action is Plan Paint's active tool (its gallery gold), radius
   2, without a contact shadow. Its border is darker than Plan Paint's
   #c49332, which reaches only 2.5:1 on the stock (the boundary gate is
   3:1). A selected disabled action is the disabled plate. */
:family(button):state(selected), :family(command-button):state(selected) {
  background: linear-gradient(to bottom, #ffedba, #ffcf66);
  border-color: #a67822;
  border-radius: 2px;
  keylines: #ffffffb9 #3156866e #3156866e #ffffffb9;
  box-shadow: none;
  color: #153765;
  glyph-color: #153765;
}
:family(button):state(selected):active, :family(command-button):state(selected):active {
  keylines: #2c568482 #ffffff9b #ffffff9b #2c568482;
}
:family(button):state(selected):disabled, :family(command-button):state(selected):disabled {
  background: linear-gradient(to bottom, #becfe4, #b5c7de);
  border-color: #8fa6c464;
  border-radius: 3px;
  keylines: #ffffff64 #31568687 #31568687 #ffffff64;
  color: #627997;
  muted-color: #627997;
  glyph-color: #627997;
}

/* The accent button: Plan Paint's File-tab blue (#528be0 to #173f88),
   started darker (#2f69be) so that white text meets 4.5:1 on the whole
   gradient. Selected draws as ordinary. The house ring lies inside the
   plate, so on the accent blue it is white. */
:family(accent-button) {
  background: linear-gradient(to bottom, #2f69be, #173f88);
  border: 1px solid #12336f;
  border-radius: 3px;
  keylines: #ffffff5a #0a1e4678 #0a1e4678 #ffffff5a;
  box-shadow: 0 1px 0 #294b7755;
  color: #ffffff;
  muted-color: #f4f7fc;
  glyph-color: #ffffff;
  outline-color: #ffffff;
}
:family(accent-button):hover {
  background: linear-gradient(to bottom, #3871c4, #1e4894);
  border-color: #0c285c;
}
:family(accent-button):active {
  background: linear-gradient(to bottom, #173f88, #285caa);
  keylines: #0a1e4678 #ffffff5a #ffffff5a #0a1e4678;
  box-shadow: none;
}
:family(accent-button):state(pending) {
  border-color: #ad841f;
}
:family(accent-button):invalid {
  border-color: #b23636;
}
:family(accent-button):disabled {
  background: linear-gradient(to bottom, #becfe4, #b5c7de);
  border-color: #8fa6c464;
  box-shadow: none;
  color: #627997;
  muted-color: #627997;
  glyph-color: #627997;
}

/* A recessed well, radius 2, with an inner top shade (rendering reference
   5: recess is the only deep depth): entry fields (editor) and choice
   indicators. The border is Plan Paint's action border, not its gallery
   well's #94acc8, which reaches only 2.2:1 on the stock (the boundary gate
   is 3:1). Selected draws as ordinary. A progress bar is a recessed track;
   selected is its fill, in the accent blue with white text. */
:family(choice), :family(editor), :family(progress) {
  border: 1px solid #6384b2;
  border-radius: 2px;
  box-shadow: inset 0 1px 2px #294b7730;
  color: #1b2733;
  glyph-color: #1b2733;
}
:family(choice), :family(progress) {
  background: #f8fbff;
}
:family(editor) {
  background: #ffffff;
}
:family(choice):hover, :family(editor):hover, :family(progress):hover {
  border-color: #355e96;
}
:family(choice):active, :family(editor):active, :family(progress):active {
  background: #e8f1fc;
  border-color: #355e96;
}
:family(choice):state(pending), :family(editor):state(pending), :family(progress):state(pending) {
  background: #fffae1;
  border-color: #ad841f;
}
:family(choice):invalid, :family(editor):invalid, :family(progress):invalid {
  background: #fff2f2;
  border-color: #b23636;
}
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled {
  background: #f4f4f5;
  border-color: #9aaabe;
  box-shadow: none;
}
:family(progress):state(selected) {
  background: linear-gradient(to bottom, #2f69be, #173f88);
  border-color: #12336f;
  box-shadow: none;
  color: #ffffff;
  muted-color: #f4f7fc;
  glyph-color: #ffffff;
}
:family(progress):state(selected):disabled {
  background: #f4f4f5;
  border-color: #9aaabe;
}

/* Rows, items and menu members: clear at rest, a pale action face while
   hot or pressed, the pale selection when selected. */
:family(menu-item), :family(selection) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  color: #1b2733;
  glyph-color: #1b2733;
}
:family(menu-item) {
  border-radius: 2px;
}
:family(selection) {
  border-radius: 0;
}
:family(menu-item):hover, :family(menu-item):active, :family(selection):hover, :family(selection):active {
  background: linear-gradient(to bottom, #e4eefc, #cfe2fa);
  border-color: #6384b2;
}
:family(menu-item):state(selected), :family(selection):state(selected) {
  background: #cfe2fa;
  border-color: #6384b2;
}

/* Links: no plate; the link colour in every enabled state (owner decision
   W-Q5). */
:family(link), :family(visited-link) {
  background: #ffffff00;
  border: 1px solid #ffffff00;
  border-radius: 0;
}
:family(link) {
  color: #19528b;
  glyph-color: #19528b;
}
:family(visited-link) {
  color: #5d4191;
  glyph-color: #5d4191;
}

/* A notice (the face of an InfoBanner or a Toast, W-4) is a stock plate
   on paper with the card's line, radius 2. Its severity is the control's
   own custom state, :state(information), :state(warning), :state(success)
   or :state(error): the control draws the keyline and icon in the
   severity's colour as an authored foreground, so the plate is the same
   for every severity and the floor has no severity records. */
:family(notice) {
  background: #ffffff;
  border: 1px solid #70829c;
  border-radius: 2px;
  color: #1b2733;
  glyph-color: #1b2733;
}
:family(notice):state(selected) {
  background: #cfe2fa;
  border-color: #6384b2;
}

/* A badge (W-4) is the accent blue chip with white text in every state but
   disabled, which takes the disabled accent face; it takes no input, so it
   has no hot or pressed look. The control rounds the chip to a pill
   (PartPaint::corner_radius). */
:family(badge) {
  background: #2f69be;
  border: 1px solid #2f69be;
  border-radius: 0;
  color: #ffffff;
  muted-color: #f4f7fc;
  glyph-color: #ffffff;
}
:family(badge):disabled {
  background: #becfe4;
  border-color: #becfe4;
}

/* Disabled text on the stock, wells, rows, links, notices and badges. */
:family(window):disabled, :family(panel):disabled, :family(card):disabled, :family(tool-tip):disabled,
:family(choice):disabled, :family(editor):disabled, :family(progress):disabled, :family(menu-item):disabled,
:family(selection):disabled, :family(link):disabled, :family(visited-link):disabled, :family(notice):disabled,
:family(badge):disabled {
  color: #627997;
  muted-color: #627997;
  glyph-color: #627997;
}
)css";

constexpr std::string_view floor_sheet_file = "gui-forms/floor.css";

// The near-white stock (§5 of the rendering reference: "ivory" meaning
// Windows' near-white, never a cream), paper, and the inks the
// compatibility colours name; the floor sheet's values.
constexpr Color stock = Color::rgba(244, 244, 245);
constexpr Color paper = Color::rgba(255, 255, 255);
constexpr Color stock_ink = Color::rgba(27, 39, 51);
constexpr Color disabled_ink = Color::rgba(98, 121, 151);

// What the floor sheet cannot say: the house focus ring, one unit wide and
// dotted (design language: the default theme's ring is dotted), and an
// empty material for the sheet's fields to be assembled onto. Every colour
// is the sheet's.
ControlVisualRecipe recipe_structure() {
    ControlVisualRecipe result{};
    result.material.fills.clear();
    result.material.border.reset();
    result.focus_width = 1.0;
    result.dotted_focus = true;
    return result;
}

// The floor definition and what evaluating the floor sheet found. When the
// sheet is refused, `definition` keeps windows-professional's recipes and
// `diagnostics` says why.
struct FloorEvaluation final {
    std::unique_ptr<ThemeDefinition> definition{};
    std::vector<ThemeCompileDiagnostic> diagnostics{};
    std::uint64_t operations{};
};

FloorEvaluation evaluate_floor() {
    FloorEvaluation result{};
    // Every family is restyled; windows-professional's definition supplies
    // the structure tokens, accent ramp and overlay shadow. Some 100 KB of
    // recipes: built on the heap, not the stack (fill_professional_role's
    // note in theme.cpp).
    result.definition = std::make_unique<ThemeDefinition>(windows_professional_theme_definition());
    ThemeDefinition& definition = *result.definition;
    definition.id = "gui-forms";
    definition.compatibility.face = stock;
    definition.compatibility.face_light = Color::rgba(250, 250, 251);
    definition.compatibility.paper = paper;
    definition.compatibility.border = Color::rgba(148, 172, 200);
    definition.compatibility.dark_border = Color::rgba(73, 108, 159);
    definition.compatibility.text = stock_ink;
    definition.compatibility.disabled_text = disabled_ink;
    definition.compatibility.accent = Color::rgba(47, 105, 190);
    definition.compatibility.accent_light = Color::rgba(207, 226, 250);
    // The sheet is compiled onto windows-professional's floor, whose
    // recipes it never reads: only its rules are evaluated.
    detail::FloorSheetEvaluation evaluated =
        detail::evaluate_floor_sheet(floor_sheet_file, floor_sheet, recipe_structure(), definition);
    result.diagnostics = std::move(evaluated.diagnostics);
    result.operations = evaluated.operations;
    // The dark variant, derived by rule (T4 phase 5; design language 5.8).
    definition.dark_variant = ThemeDarkVariant::derived;
    definition.colour_schemes = ColourSchemeSet{.light = true, .dark = true};
    return result;
}

const FloorEvaluation& floor_evaluation() {
    // Evaluated once, on the first call's thread; C++ makes the
    // initialization thread-safe, and it is never changed afterwards.
    static const FloorEvaluation evaluation = evaluate_floor();
    return evaluation;
}

// -- the light sheet ----------------------------------------------------------------

constexpr std::string_view light_sheet = R"css(/* gui-forms: the default theme's light sheet (stage T4, phase 3).
   Colour roles of design language 5.4 in Plan Paint's palette
   (malkuth-software/plan-paint at 5e28a5e, src/forms/ribbon.cpp) and the
   rules for parts the engine paints. Controls painting from their family's
   recipe draw the theme's floor (gui_forms_theme_definition). The signal
   group (design language 5.4: signal, success, pending and invalid) is
   the warm coral and its siblings deepened to text contrast on paper. */

:root {
  ink-on-paper: #1b2733 on #ffffff;
  muted-ink-on-paper: #4a545f on #ffffff;
  ink-on-soft-paper: #1b2733 on #f8fbff;
  ink-on-chrome: #153765 on #d2e4fb;
  ink-on-chrome-low: #153765 on #a3c1ec;
  ink-on-near-white-stock: #1b2733 on #f4f4f5;
  selection-ink-on-selection: #1b2733 on #cfe2fa;
  ink-on-gold: #153765 on #ffcf66;
  on-accent-on-accent: #ffffff on #2f69be;
  signal-on-paper: #b4413c on #ffffff;
  success-on-paper: #2e7d4f on #ffffff;
  pending-ink-on-tint: #6d5200 on #fff4d6;
  invalid-ink-on-tint: #9b1c1c on #fde4e1;
  disabled-ink-on-chrome: #627997 on #becfe4;
  disabled-ink-on-paper: #627997 on #ffffff;
  command-link-ink-on-paper: #19528b on #ffffff;
  chrome-highlight: #ffffff;
  chrome: #cadefb;
  chrome-middle: #a5c4f1;
  chrome-low: #769fdf;
  ordinary-line: #6384b2;
  dark-line: #496c9f;
  shadow: #294b77;
  selection-line: #6384b2;
  focus: #1b2733;
}

/* Plan Paint's tab stack (make_tab_theme): faces #9BB6DA at rest, #ACC6E7
   hot, #E8F2FF selected, radius 4, no border; a shade over the top 72 %
   (a white sheen when selected); a white top line (49 %, 100 % selected);
   on the right the dark keyline #496C9F with the light #D1E0F5 inside it;
   the hard two-unit cast and the soft shadow to the right. A material
   holds four shadows, so the top line is a one-unit band of the
   background; the keylines are inset shadows. Against Plan Paint's own
   material the largest channel difference is 112, at the two right
   corners only, where an inset shadow follows the radius and a keyline
   does not; elsewhere it is 16 or less (T4 phase 4). The tab's ink is its
   family's recipe text (a part takes no `color`). */
tab-control::part(tab), page-strip::part(tab), ribbon::part(tab) {
  background-color: #9bb6da;
  background-image: linear-gradient(to bottom, rgb(255 255 255 / 49%) 1px, transparent 1px),
    linear-gradient(to bottom, rgb(34 70 116 / 15%), rgb(34 70 116 / 0%) 72%);
  border: none;
  border-radius: 4px;
  box-shadow: inset -1px 0 0 #496c9f, inset -2px 0 0 #d1e0f5, 3px 1px 2px rgb(24 56 97 / 22%), 2px 0 0 #496c9f;
}
tab-control::part(tab):hover, page-strip::part(tab):hover, ribbon::part(tab):hover {
  background-color: #acc6e7;
}
tab-control::part(tab):state(selected), page-strip::part(tab):state(selected),
ribbon::part(tab):state(selected) {
  background-color: #e8f2ff;
  background-image: linear-gradient(to bottom, #ffffff 1px, transparent 1px),
    linear-gradient(to bottom, rgb(255 255 255 / 57%), rgb(255 255 255 / 0%) 72%);
}

/* The Ribbon's tab row (rendering reference §4; design language §11 item
   4; the owner's folder corrections, 2026-11-09): a flat #93ADD0 rail 27
   units high; the tabs stand three units below the rail's top, the
   selected one standing to the top and running one unit into the plane,
   whose top it matches, so that it flows into the plane with no lip and
   no line between them. Every tab is a folder, and the folders stack:
   each overlaps the next by five units (a negative gap), its right edge
   tapering five units from foot to top in the keyline #496C9F and lying
   over the next tab's left end, and a soft shadow falls off over ten
   units from that edge onto the tab beneath. The selected tab lies over
   both its neighbours, its label bold in #173F79 and a little larger. The
   keylines and casts of the shared tab rule give way to the taper and its
   shadow. Only File (the blue gradient tab with white ink) and a
   contextual tab (its set's tint) carry a colour of their own. */
ribbon::part(tab-rail) {
  background: #93add0;
  border: none;
  height: 27px;
}
ribbon::part(tab) {
  border-radius: 4px 4px 0 0;
  box-shadow: none;
  gap: -5px;
  margin-top: 3px;
  taper: 5px #496c9f;
  edge-shadow: 10px rgb(24 56 97 / 45%);
}
ribbon::part(tab):state(selected) {
  margin-top: 0;
  margin-bottom: -1px;
}
ribbon::part(tab-label):state(selected) {
  color: #173f79;
  font-weight: bold;
  font-size: 1.1em;
}
ribbon::part(application-button) {
  background: linear-gradient(to bottom, #528be0, #173f88);
  border: none;
  border-radius: 4px 4px 0 0;
  margin-top: 3px;
}
ribbon::part(application-label) {
  color: #ffffff;
}

/* The Ribbon's command plane (rendering reference §5): Plan Paint's
   #E8F2FF, #CADEFB, #A5C4F1 and #769FDF and a shade line at its foot; no
   line along its top, where the selected tab flows into it. */
ribbon::part(command-plane) {
  background: linear-gradient(to bottom, #e8f2ff, #cadefb 28%, #a5c4f1 62%, #769fdf);
  border: none;
  box-shadow: inset 0 -1px 0 #496c9f;
}

/* The Ribbon's groups (rendering reference §5; design language §11 item
   4): captions in #485B70 beneath thin etched dividers, a shade line with
   a white line beside it, fading out at both ends. */
ribbon::part(group-caption) {
  color: #485b70;
}
ribbon::part(group-divider) {
  background: linear-gradient(to bottom, rgb(99 132 178 / 0%), #6384b2 50%, rgb(99 132 178 / 0%));
  border: none;
  box-shadow: 1px 0 0 rgb(255 255 255 / 60%);
}

/* Backstage (RB decision 26): the column of commands and topics in the
   command plane's blue, set apart from the white page, as the owner's
   note on Office 2010 asks. */
backstage-view::part(column) {
  background: linear-gradient(to bottom, #e8f2ff, #cadefb);
  border: none;
  box-shadow: inset -1px 0 0 #6384b2;
}
backstage-view::part(page) {
  background: #ffffff;
  border: none;
}

/* The command plane: near-white deepening toward Plan Paint's royale blue
   at its foot, a white top line and a shade line at its foot, drawn as
   inset lights (no border). */
tool-strip::part(face), menu-strip::part(face) {
  background: linear-gradient(to bottom, #e8f2ff, #cadefb);
  border: none;
  box-shadow: inset 0 1px 0 #ffffff, inset 0 -1px 0 #a5c4f1;
}

/* The status strip: flat and pale, with no sunken cells; its top line is
   Plan Paint's #C3D3E8. */
status-strip::part(face) {
  background: #eef3fa;
  border: none;
  box-shadow: inset 0 1px 0 #c3d3e8;
}

/* A knob's pointer is a plain line across its face, thinner than a
   gauge's needle (row 2.5; owner option (a)). */
dial::part(needle) {
  needle-shape: line;
  stroke-width: 3px;
}

/* Indicator lamps (skeudoindustrial surfaces, follow-up 4): restrained
   chrome, so that a lit lens is the only saturated light on the form. The
   bezel is a quiet grey ring seated into the panel; the lens is dark glass
   with a faint highlight. Lit, a hot centre falls to the lamp's colour at
   the rim with a soft glow inside the lens only: green for on, red for
   error. */
indicator-lamp::part(bezel) {
  background: linear-gradient(to bottom, #b4bac1, #eef0f2);
  border: 1px solid #8d949c;
  seat: -10%;
}
indicator-lamp::part(lens) {
  background: radial-gradient(circle at 40% 35%, #7a8088, #2c3035 70%);
  border: 1px solid #24282c;
  box-shadow: inset 0 1px 1px #ffffff30;
}
indicator-lamp::part(lens):state(on) {
  background: radial-gradient(circle at 40% 35%, #eaffdc, #46c14a 45%, #1d7424);
  border: 1px solid #17591c;
  box-shadow: inset 0 0 3px #eaffdcb0;
}
indicator-lamp::part(lens):state(error) {
  background: radial-gradient(circle at 40% 35%, #ffe4da, #e5432e 45%, #8c1a0f);
  border: 1px solid #6b130a;
  box-shadow: inset 0 0 3px #ffe4dab0;
}

/* The danger key (skeudoindustrial surfaces, follow-up 5): a button that
   marks a dangerous or irreversible action (Button::danger) is lit red from
   inside its face, standing out from the quiet chrome around it. The glow
   is placed over the face's fills, inside its edge, so nothing reaches a
   neighbour; the button pulses the lit face slowly over the plain one and
   holds it steady under reduced motion. The warning mark that leads the
   label is the cue that does not rest on colour. */
button::part(face):state(danger) {
  background: radial-gradient(circle at 50% 65%, #f3bdb3, #f8e7e4 80%);
  border-color: #a5463b;
  glow: #e2412c 4px;
  glow-placement: over;
}
button::part(label):state(danger) {
  color: #7a1a10;
}

/* The node canvas (cp31; NODE_GRAPH_SPEC section 3): a paper canvas with
   a blueprint grid in the ordinary line's blue, white nodes outlined in
   it under chrome headers, links in the dark line with flowing dots, the
   selection in the accent blue with a soft halo, the signal group for an
   invalid link, a rejected drop and a lamp in error. The grid variants
   are classes on the canvas: paper (the default), blue (white lines on
   the royale blue), green and grey. The dark variant is derived by rule
   (resolve_canvas_look), so these rules are written in light terms. */
node-canvas::part(region) {
  fill-color: #fbfcfd;
  grid-spacing: 16px;
  grid-band: 6px;
}
node-canvas::part(grid-minor) {
  stroke-color: rgb(99 132 178 / 22%);
  stroke-width: 1px;
}
node-canvas::part(grid-major) {
  stroke-color: rgb(99 132 178 / 45%);
}
node-canvas.blue::part(region) {
  fill-color: #1f4f8f;
}
node-canvas.blue::part(grid-minor) {
  stroke-color: rgb(255 255 255 / 16%);
}
node-canvas.blue::part(grid-major) {
  stroke-color: rgb(255 255 255 / 34%);
}
node-canvas.green::part(region) {
  fill-color: #e9f2e4;
}
node-canvas.green::part(grid-minor) {
  stroke-color: rgb(46 125 79 / 20%);
}
node-canvas.green::part(grid-major) {
  stroke-color: rgb(46 125 79 / 42%);
}
node-canvas.grey::part(region) {
  fill-color: #8a8f96;
}
node-canvas.grey::part(grid-minor) {
  stroke-color: rgb(27 39 51 / 18%);
}
node-canvas.grey::part(grid-major) {
  stroke-color: rgb(27 39 51 / 36%);
}
/* The links, ports, selection and marquee on the two deep variants: on
   the royale blue, pale links, gold for the selection and a coral for an
   invalid link; on the mid grey, ink links and white for the selection.
   Each reads at 3:1 or more on its canvas. The green variant is pale
   and keeps the paper's. */
node-canvas.blue::part(link), node-canvas.blue::part(dashed-link) {
  stroke-color: #e8f2ff;
}
node-canvas.blue::part(link):hover, node-canvas.blue::part(dashed-link):hover {
  stroke-color: #ffffff;
}
node-canvas.blue::part(link):state(selected), node-canvas.blue::part(dashed-link):state(selected) {
  stroke-color: #ffcf66;
  halo: 4px rgb(255 207 102 / 35%);
}
node-canvas.blue::part(link):invalid, node-canvas.blue::part(dashed-link):invalid {
  stroke-color: #ffb4a8;
}
node-canvas.blue::part(port) {
  stroke-color: #173f79;
}
node-canvas.blue::part(node-selection) {
  stroke-color: #ffcf66;
}
node-canvas.blue::part(marquee) {
  fill-color: rgb(255 255 255 / 14%);
  stroke-color: #ffffff;
}
node-canvas.grey::part(link), node-canvas.grey::part(dashed-link) {
  stroke-color: #1b2733;
}
node-canvas.grey::part(link):hover, node-canvas.grey::part(dashed-link):hover {
  stroke-color: #000000;
}
node-canvas.grey::part(link):state(selected), node-canvas.grey::part(dashed-link):state(selected) {
  stroke-color: #ffffff;
  halo: 4px rgb(255 255 255 / 35%);
}
node-canvas.grey::part(link):invalid, node-canvas.grey::part(dashed-link):invalid {
  stroke-color: #6e0f0f;
}
node-canvas.grey::part(port) {
  stroke-color: #1b2733;
}
node-canvas.grey::part(node-selection) {
  stroke-color: #ffffff;
}
node-canvas.grey::part(marquee) {
  fill-color: rgb(255 255 255 / 18%);
  stroke-color: #ffffff;
}
node-canvas::part(node) {
  fill-color: #ffffff;
  stroke-color: #6384b2;
  stroke-width: 1px;
  corner-radius: 6px;
}
node-canvas::part(node):hover {
  stroke-color: #496c9f;
}
node-canvas::part(node):state(selected) {
  stroke-color: #2f69be;
}
node-canvas::part(node):disabled {
  fill-color: #f4f4f5;
  stroke-color: #becfe4;
}
node-canvas::part(node-header) {
  fill-color: #cadefb;
  stroke-color: #a5c4f1;
  stroke-width: 1px;
  corner-radius: 6px;
  color: #153765;
}
node-canvas::part(node-header):hover {
  fill-color: #d2e4fb;
}
node-canvas::part(node-header):state(selected) {
  fill-color: #a5c4f1;
}
node-canvas::part(node-header):disabled {
  fill-color: #becfe4;
  color: #627997;
}
node-canvas::part(node-selection) {
  stroke-color: #2f69be;
  stroke-width: 2px;
  corner-radius: 8px;
}
node-canvas::part(port) {
  fill-color: #ffffff;
  stroke-color: #496c9f;
  stroke-width: 1.5px;
}
node-canvas::part(port):hover {
  fill-color: #ffcf66;
}
node-canvas::part(port):state(drop-accepted) {
  fill-color: #2e7d4f;
}
node-canvas::part(port):state(drop-rejected) {
  fill-color: #fde4e1;
  stroke-color: #9b1c1c;
}
node-canvas::part(link), node-canvas::part(dashed-link) {
  stroke-color: #496c9f;
  stroke-width: 2px;
}
node-canvas::part(link) {
  flow: dots;
  flow-speed: 48;
  flow-spacing: 16px;
}
node-canvas::part(dashed-link) {
  stroke-dash: 6px 4px;
}
node-canvas::part(link):hover, node-canvas::part(dashed-link):hover {
  stroke-color: #2f69be;
  stroke-width: 2.5px;
}
node-canvas::part(link):state(selected), node-canvas::part(dashed-link):state(selected) {
  stroke-color: #2f69be;
  halo: 4px rgb(47 105 190 / 30%);
}
node-canvas::part(link):invalid, node-canvas::part(dashed-link):invalid {
  stroke-color: #b4413c;
  stroke-dash: 2px 3px;
}
node-canvas::part(lamp) {
  fill-color: #becfe4;
  stroke-color: #496c9f;
  stroke-width: 1px;
}
node-canvas::part(lamp):checked {
  fill-color: #3fae5a;
  stroke-color: #2e7d4f;
}
node-canvas::part(lamp):invalid {
  fill-color: #d9463e;
  stroke-color: #9b1c1c;
}
node-canvas::part(marquee) {
  fill-color: rgb(47 105 190 / 12%);
  stroke-color: #2f69be;
  stroke-width: 1px;
  stroke-dash: 4px 3px;
}
node-canvas.blue::part(link-preview) {
  stroke-color: #e8f2ff;
}
node-canvas.blue::part(knife) {
  stroke-color: #ffb4a8;
}
node-canvas.blue::part(port-label), node-canvas.grey::part(port-label) {
  color: #ffffff;
}
node-canvas.grey::part(link-preview) {
  stroke-color: #1b2733;
}
)css";

constexpr std::string_view light_sheet_file = "gui-forms/light.css";

GuiFormsThemeBuild build_gui_forms_theme() {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    GuiFormsThemeBuild result{};
    const FloorEvaluation& floor = floor_evaluation();
    result.diagnostics = floor.diagnostics;
    result.floor_compile_operations = floor.operations;
    detail::BuiltInThemeCompilation compiled =
        detail::compile_built_in_theme(*floor.definition, light_sheet_file, light_sheet, stock_control_anatomies());
    result.diagnostics.insert(result.diagnostics.end(), compiled.diagnostics.begin(), compiled.diagnostics.end());
    result.compile_operations = compiled.operations;
    result.theme = compiled.theme != nullptr ? std::move(compiled.theme) : Theme::create(*floor.definition);
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    result.build_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return result;
}

} // namespace

const ThemeDefinition& gui_forms_theme_definition() {
    // S3: the evaluated floor, kept for the process, by reference: a caller
    // that edits it copies it.
    const ThemeDefinition& result = *floor_evaluation().definition;
    return result;
}

std::string_view gui_forms_floor_sheet() noexcept { return floor_sheet; }

std::string_view gui_forms_light_sheet() noexcept { return light_sheet; }

const GuiFormsThemeBuild& gui_forms_theme_build() {
    // Built once, on the first call's thread; C++ makes the initialization
    // thread-safe, and the build is never changed afterwards.
    static const GuiFormsThemeBuild build = build_gui_forms_theme();
    return build;
}

std::shared_ptr<const Theme> gui_forms_theme() {
    const std::shared_ptr<const Theme> result = gui_forms_theme_build().theme;
    return result;
}

} // namespace gui_forms
