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
    appearing.enter_from = 0.0;
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
// there is no brightness lift.
PropertyTransition ribbon_rise() {
    PropertyTransition rise{};
    rise.property = "amplitude";
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = 1.0;
    PresentationState rest{};
    rest.target = 0.0;
    rise.states.push_back(hot);
    rise.states.push_back(rest);
    rise.curve = MotionCurve::follow();
    return rise;
}

ThemeRule built_in_rule(std::string label, std::string_view control_type, std::string_view part) {
    ThemeRule rule{};
    rule.label = std::move(label);
    rule.level = CascadeLevel::built_in;
    rule.selector.control_type = std::string(control_type);
    rule.selector.part = std::string(part);
    return rule;
}

// A ribbon part that takes the dithered button (RB decision 23: the large
// elements that conventionally show a hover square), and the states in
// which it keeps today's plate: a checked toggle and an open menu are told
// apart by the plate, which the bulge alone would not show.
void add_ribbon_button(ThemeRuleSheet& sheet, std::string_view part, bool checkable) {
    ThemeRule rise = built_in_rule("ribbon_rise:" + std::string(part), "ribbon", part);
    rise.declarations.push_back(RuleDeclaration::primitive("dithered_bulge"));
    rise.transitions.push_back(ribbon_rise());
    sheet.rules.push_back(std::move(rise));
    if (checkable) {
        ThemeRule checked = built_in_rule("ribbon_checked:" + std::string(part), "ribbon", part);
        checked.selector.states.push_back(AnatomyState::checked);
        checked.declarations.push_back(RuleDeclaration::primitive(built_in_primitive_name));
        sheet.rules.push_back(std::move(checked));
    }
    ThemeRule open = built_in_rule("ribbon_open:" + std::string(part), "ribbon", part);
    open.selector.states.push_back(AnatomyState::open);
    open.declarations.push_back(RuleDeclaration::primitive(built_in_primitive_name));
    sheet.rules.push_back(std::move(open));
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
    shadow.selector.role = PartRole::shadow;
    shadow.declarations.push_back(RuleDeclaration::primitive("overlay_shadow"));
    sheet.rules.push_back(std::move(shadow));
    return sheet;
}

} // namespace

const ThemeRuleSheet& built_in_theme_rules() {
    static const ThemeRuleSheet rules = make_built_in_rules();
    return rules;
}

} // namespace gui_forms
