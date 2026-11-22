// The engine's built-in rules (T2 plan, phase E7): the motion the default
// draws before T4, compiled below every theme's rules
// (CompiledThemeRules::compile_with_built_in), so that any theme rule for
// the same parts takes precedence.

#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <array>
#include <chrono>

namespace gui_forms {
namespace {

// The docking tiles' fade (GIVEN C1-1, C2-1): DK D3 publishes `expanding`
// on the hovered pane's tiles while their 100 ms fade runs; entering it
// stands the tile at opacity 0 and fades it to 1, linearly over the theme's
// quick token, as D3's own motion value runs. A tile out of it is opaque.
constexpr std::chrono::milliseconds tile_fade_duration{100};

PropertyTransition tile_fade() {
    PropertyTransition fade{};
    fade.property = std::string(opacity_property);
    PresentationState appearing{};
    appearing.when.push_back(AnatomyState::expanding);
    appearing.target = 1.0;
    appearing.enter_from.set(0.0);
    PresentationState shown{};
    shown.target = 1.0;
    fade.states.push_back(appearing);
    fade.states.push_back(shown);
    fade.curve = MotionCurve::timed(tile_fade_duration, MotionEasing::linear);
    return fade;
}

// The ribbon's rise (GIVEN D3-1, G4-1; rendering reference §2 and §3): a
// ribbon button is flat at rest, and while it is hot its dithered bulge's
// amplitude rises from 0 to 1 by the exponential approach (the follow
// curve, 75 ms), falling back when it is no longer hot. Hover is the rise:
// there is no brightness lift. A press is not a hover: pressed, the part
// stands at full amplitude at once (the dent, reference §2 "Pressed is a
// dent"), so that a press without a settled hover (a click that arrives
// with the pointer, a touch, a capture) still shows the dent rather than
// the flat plane it would rise from (owner, 2026-11-22).
PropertyTransition ribbon_rise() {
    PropertyTransition rise{};
    rise.property = "amplitude";
    PresentationState pressed{};
    pressed.when.push_back(AnatomyState::pressed);
    pressed.target = 1.0;
    pressed.enter_from.set(1.0);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = 1.0;
    PresentationState rest{};
    rest.target = 0.0;
    rise.states.push_back(pressed);
    rise.states.push_back(hot);
    rise.states.push_back(rest);
    rise.curve = MotionCurve::follow();
    return rise;
}

// The selected look stands (rendering reference §2, "Active tool"): a
// checked command or a selected gallery item shows its bulge at full
// amplitude at rest as under the pointer, rising there by the same follow
// curve when it becomes checked and falling by the hover's machine when it
// no longer is.
PropertyTransition selected_stands() {
    PropertyTransition stand{};
    stand.property = "amplitude";
    PresentationState shown{};
    shown.target = 1.0;
    stand.states.push_back(shown);
    stand.curve = MotionCurve::follow();
    return stand;
}

ThemeRule built_in_rule(std::string label, std::string_view control_type, std::string_view part) {
    ThemeRule rule{};
    rule.label = std::move(label);
    rule.level = CascadeLevel::built_in;
    rule.selector.control_type = std::string(control_type);
    rule.selector.part = std::string(part);
    return rule;
}

// A part whose hover is the dithered rise (the soft dither: flat at rest,
// the bulge rising while hot): `control_type`'s `part`, without the plate
// states a command face keeps.
void add_dithered_rise(ThemeRuleSheet& sheet, std::string_view control_type, std::string_view part) {
    ThemeRule rise = built_in_rule("ribbon_rise:" + std::string(part), control_type, part);
    rise.declarations.push_back(RuleDeclaration::primitive("dithered_bulge"));
    rise.transitions.push_back(ribbon_rise());
    sheet.rules.push_back(std::move(rise));
}

// A part that shows `state` (a checked command, a selected gallery item)
// in the dithered bulge's gold, standing at full amplitude (ledger R12;
// rendering reference §2 and §3, "Active tool"). Pressed, it is the gold
// dent: the primitive reads `pressed` in every variant. Before this rule a
// checked command took the menu-item family's plate, which is clear at rest
// in the stock theme, and a selected gallery item took the hover rise
// alone: neither showed its state (owner, 2026-11-22).
void add_selected_gold(ThemeRuleSheet& sheet, std::string_view control_type, std::string_view part,
                       AnatomyState state) {
    ThemeRule gold = built_in_rule("ribbon_gold:" + std::string(part), control_type, part);
    gold.selector.states.push_back(state);
    gold.declarations.push_back(RuleDeclaration::primitive("dithered_bulge"));
    gold.declarations.push_back(RuleDeclaration::number_value("gold", 1.0));
    gold.transitions.push_back(selected_stands());
    sheet.rules.push_back(std::move(gold));
}

// A ribbon part that takes the dithered button (RB decision 23: the large
// elements that conventionally show a hover square): the rise, a dent while
// pressed (the primitive reads `pressed`) and the gold bulge while checked.
// While its menu is open the ribbon paints the part pressed, so the dent
// stands for the open menu; the plate it took before showed as a flat blue
// plate where the study draws the dent (owner, 2026-11-22).
void add_ribbon_button(ThemeRuleSheet& sheet, std::string_view part, bool checkable) {
    add_dithered_rise(sheet, "ribbon", part);
    if (checkable) add_selected_gold(sheet, "ribbon", part, AnatomyState::checked);
}

// A menu item (the menu strip anatomy's `menu_item` part, which the menu
// bar's items and the rows of the menus they open both paint): the designed
// hover, the ribbon's dithered bulge, flat at rest and rising while the item
// is hot (owner: the soft dither is the hover for menu items and buttons;
// design language §3.5 and §12.6). An item that shows its menu (open, or
// pressed on the bar) or is selected keeps the plate, which tells it apart.
void add_menu_item(ThemeRuleSheet& sheet) {
    ThemeRule rise = built_in_rule("menu_rise:menu_item", "menu_strip", "menu_item");
    rise.declarations.push_back(RuleDeclaration::primitive("dithered_bulge"));
    rise.transitions.push_back(ribbon_rise());
    sheet.rules.push_back(std::move(rise));
    const std::array<AnatomyState, 3> plated{AnatomyState::open, AnatomyState::pressed, AnatomyState::selected};
    for (const AnatomyState state : plated) {
        ThemeRule plate = built_in_rule("menu_plate:menu_item", "menu_strip", "menu_item");
        plate.selector.states.push_back(state);
        plate.declarations.push_back(RuleDeclaration::primitive(built_in_primitive_name));
        sheet.rules.push_back(std::move(plate));
    }
}

ThemeRuleSheet make_built_in_rules() {
    ThemeRuleSheet sheet{};
    add_ribbon_button(sheet, "command_face", true);
    add_ribbon_button(sheet, "group_button", false);
    // A split's arrow half hovers as the soft dither too: without a rule of
    // its own it took the button family's hover plate, a flat light-blue
    // plate with a border under the bulge of the other half. While the split
    // is pressed or its menu is open the ribbon paints the face across both
    // halves and leaves the arrow out, so the dent covers the whole button.
    add_dithered_rise(sheet, "ribbon", "command_arrow");
    // The soft dither is every ribbon hover (owner, 2026-11-09): a gallery's
    // items, and the tabs through their hover layer, which lies over the
    // tab's own face (a theme's tab rules choose the face's material, so the
    // rise cannot be the face itself).
    add_dithered_rise(sheet, "ribbon_gallery", "item_face");
    add_selected_gold(sheet, "ribbon_gallery", "item_face", AnatomyState::selected);
    add_dithered_rise(sheet, "ribbon", "tab_hover");
    add_menu_item(sheet);
    // The tiles' plates are engine primitives (E8): the pill plate, the gold
    // plate while lit, so that a tile's fade composites one engine raster
    // and its pixels are the same on every host. Their quiet look (dark
    // plates, reduced strength) remains the theme's (T2-4).
    ThemeRule tile = built_in_rule("dock_tile_fade:tile", "dock_site", "tile");
    tile.declarations.push_back(RuleDeclaration::primitive("pill_plate"));
    tile.transitions.push_back(tile_fade());
    sheet.rules.push_back(std::move(tile));
    ThemeRule lit = built_in_rule("dock_tile_lit:tile", "dock_site", "tile");
    lit.selector.states.push_back(AnatomyState::selected);
    lit.declarations.push_back(RuleDeclaration::primitive("gold_plate"));
    sheet.rules.push_back(std::move(lit));
    ThemeRule glyph = built_in_rule("dock_tile_fade:tile_glyph", "dock_site", "tile_glyph");
    glyph.transitions.push_back(tile_fade());
    sheet.rules.push_back(std::move(glyph));
    // The overlay shadow (E9, GIVEN E1-1): every shadow part, whatever its
    // control, is drawn by the engine's overlay shadow primitive, with the
    // first-party blur, rather than by Painter::draw_box_shadow.
    ThemeRule shadow{};
    shadow.label = "overlay_shadow";
    shadow.level = CascadeLevel::built_in;
    shadow.selector.role.set(PartRole::shadow);
    shadow.declarations.push_back(RuleDeclaration::primitive("overlay_shadow"));
    sheet.rules.push_back(std::move(shadow));
    return sheet;
}

} // namespace

const ThemeRuleSheet& built_in_theme_rules() {
    // Never destroyed (rule 1: process termination releases none of our state).
    static const ThemeRuleSheet* const rules = new const ThemeRuleSheet(make_built_in_rules());
    return *rules;
}

} // namespace gui_forms
