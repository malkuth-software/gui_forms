#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"

#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include "../theme_compiler/built_in_theme.hpp"

#include <array>
#include <cstddef>
#include <utility>

namespace gui_forms {

namespace {

// -- Plan Paint's values (src/forms/ribbon.cpp at 5e28a5e) --------------------
//
// Each colour below is read from Plan Paint's source unless its comment
// says otherwise; a value changed to meet the contrast gate says so.

constexpr std::size_t state_index(ControlSurfaceState state) noexcept {
    return static_cast<std::size_t>(state);
}

constexpr std::size_t role_index(ControlVisualRole role) noexcept {
    return static_cast<std::size_t>(role);
}

// Ink on the near-white stock and on paper (design language §5.4).
constexpr Color stock_ink = Color::rgba(27, 39, 51);
// Plan Paint's action ink, `action_material`'s text.
constexpr Color action_ink = Color::rgba(21, 55, 101);
// Muted text: darker than windows-professional's #76808A, which fails
// 4.5:1 on its own plates (phase 1 report); it meets 4.5:1 on every face
// below.
constexpr Color muted_ink = Color::rgba(74, 84, 95);
// Muted text on an action plate, whose faces run darker than the stock.
constexpr Color action_muted_ink = Color::rgba(44, 58, 78);
// Muted text on the accent blue.
constexpr Color accent_muted_ink = Color::rgba(244, 247, 252);
// Disabled text. Plan Paint's (98,121,151) reaches only 2.6:1 on its own
// disabled face; this one keeps its hue and meets 3:1 on every disabled face
// (the contrast gate; M104: reduced, still visible).
constexpr Color disabled_ink = Color::rgba(82, 99, 122);
// The near-white stock (§5 of the rendering reference: "ivory" meaning
// Windows' near-white, never a cream).
constexpr Color stock = Color::rgba(244, 244, 245);
constexpr Color paper = Color::rgba(255, 255, 255);
constexpr Color clear = Color::rgba(255, 255, 255, 0);
// The focus ring: dark ink, a non-colour cue, dotted (design language).
constexpr Color focus_ink = Color::rgba(27, 39, 51);
// The default button's second perimeter (M105), Plan Paint's default ring.
constexpr Color default_perimeter = Color::rgba(33, 75, 129);

// One plate's colours: a vertical gradient from `top` to `bottom` inside a
// one-unit `border`.
struct PlateColours final {
    Color top{};
    Color bottom{};
    Color border{};
};

// The inner lines of a relieved plate: `upper` along the top and left,
// `lower` along the right and bottom, one unit inside the border.
struct Relief final {
    Color upper{};
    Color lower{};
};

SurfaceMaterial plate(PlateColours colours, double radius) {
    SurfaceMaterial result{};
    if (colours.top == colours.bottom) {
        result.fills = {MaterialFillLayer::solid(colours.top)};
    } else {
        result.fills = {MaterialFillLayer::linear({0.0, 0.0}, {0.0, 1.0}, {{0.0, colours.top}, {1.0, colours.bottom}})};
    }
    result.border = MaterialBorder{colours.border, 1.0};
    result.corner_radius = radius;
    return result;
}

void add_relief(SurfaceMaterial& material, Relief relief) {
    material.keylines.push_back(MaterialKeyline{MaterialEdge::top, relief.upper, 1.0, 1.0});
    material.keylines.push_back(MaterialKeyline{MaterialEdge::left, relief.upper, 1.0, 1.0});
    material.keylines.push_back(MaterialKeyline{MaterialEdge::right, relief.lower, 1.0, 1.0});
    material.keylines.push_back(MaterialKeyline{MaterialEdge::bottom, relief.lower, 1.0, 1.0});
}

// Plan Paint's contact shadow: one unit below, hard.
void add_contact_shadow(SurfaceMaterial& material) {
    MaterialShadow shadow{};
    shadow.offset = Point{0.0, 1.0};
    shadow.blur_radius = 0.0;
    shadow.color = Color::rgba(41, 75, 119, 85);
    material.shadows.push_back(shadow);
}

// A recessed well's inner top shade (rendering reference §5: recess is the
// only deep depth).
void add_well_shade(SurfaceMaterial& material) {
    MaterialShadow shadow{};
    shadow.offset = Point{0.0, 1.0};
    shadow.blur_radius = 2.0;
    shadow.color = Color::rgba(41, 75, 119, 48);
    shadow.inset = true;
    material.shadows.push_back(shadow);
}

ControlVisualRecipe recipe_of(SurfaceMaterial material, Color text) {
    ControlVisualRecipe result{};
    result.material = std::move(material);
    result.text = text;
    result.muted_text = muted_ink;
    result.glyph = text;
    result.focus_ring = focus_ink;
    result.focus_width = 1.0;
    result.dotted_focus = true;
    result.default_ring = default_perimeter;
    result.default_width = 1.0;
    return result;
}

// -- action plates ----------------------------------------------------------------

ControlVisualRecipe action_recipe(SurfaceMaterial material) {
    ControlVisualRecipe result = recipe_of(std::move(material), action_ink);
    result.muted_text = action_muted_ink;
    return result;
}

// Plan Paint's `action_material` in `state`, radius 3.
ControlVisualRecipe action_plate(ControlSurfaceState state) {
    const Relief raised{Color::rgba(255, 255, 255, 230), Color::rgba(49, 86, 134, 135)};
    switch (state) {
    case ControlSurfaceState::hot: {
        SurfaceMaterial material =
            plate(PlateColours{Color::rgba(218, 233, 252), Color::rgba(177, 204, 242), Color::rgba(53, 94, 150)}, 3.0);
        add_relief(material, raised);
        add_contact_shadow(material);
        return action_recipe(std::move(material));
    }
    case ControlSurfaceState::pressed: {
        SurfaceMaterial material =
            plate(PlateColours{Color::rgba(152, 181, 218), Color::rgba(181, 204, 235), Color::rgba(99, 132, 178)}, 3.0);
        // Pressed inverts the inner lines and drops the contact shadow.
        add_relief(material, Relief{Color::rgba(44, 86, 132, 130), Color::rgba(255, 255, 255, 155)});
        return action_recipe(std::move(material));
    }
    case ControlSurfaceState::pending: {
        // Not in Plan Paint: windows-professional's pending tint, relieved.
        SurfaceMaterial material =
            plate(PlateColours{Color::rgba(255, 249, 218), Color::rgba(246, 229, 157), Color::rgba(173, 132, 31)}, 3.0);
        add_relief(material, raised);
        add_contact_shadow(material);
        return action_recipe(std::move(material));
    }
    case ControlSurfaceState::invalid: {
        // Not in Plan Paint: windows-professional's invalid tint, relieved.
        SurfaceMaterial material =
            plate(PlateColours{Color::rgba(255, 239, 239), Color::rgba(247, 207, 207), Color::rgba(178, 54, 54)}, 3.0);
        add_relief(material, raised);
        add_contact_shadow(material);
        return action_recipe(std::move(material));
    }
    case ControlSurfaceState::disabled: {
        SurfaceMaterial material = plate(
            PlateColours{Color::rgba(190, 207, 228), Color::rgba(181, 199, 222), Color::rgba(143, 166, 196, 100)}, 3.0);
        add_relief(material, Relief{Color::rgba(255, 255, 255, 100), Color::rgba(49, 86, 134, 135)});
        ControlVisualRecipe result = recipe_of(std::move(material), disabled_ink);
        result.muted_text = disabled_ink;
        return result;
    }
    case ControlSurfaceState::normal:
    case ControlSurfaceState::deactivated:
    case ControlSurfaceState::count:
    default: break;
    }
    SurfaceMaterial material =
        plate(PlateColours{Color::rgba(210, 228, 251), Color::rgba(163, 193, 236), Color::rgba(99, 132, 178)}, 3.0);
    add_relief(material, raised);
    add_contact_shadow(material);
    return action_recipe(std::move(material));
}

// Plan Paint's active tool (`gallery_material`'s gold), radius 2, for a
// selected action in `state`. Its border is darker than Plan Paint's
// #C49332, which reaches only 2.5:1 on the stock (the boundary gate is 3:1).
constexpr Color gold_border = Color::rgba(166, 120, 34);

ControlVisualRecipe gold_plate(ControlSurfaceState state) {
    if (state == ControlSurfaceState::disabled) return action_plate(state);
    SurfaceMaterial material =
        plate(PlateColours{Color::rgba(255, 237, 186), Color::rgba(255, 207, 102), gold_border}, 2.0);
    if (state == ControlSurfaceState::pressed) {
        add_relief(material, Relief{Color::rgba(44, 86, 132, 130), Color::rgba(255, 255, 255, 155)});
    } else {
        add_relief(material, Relief{Color::rgba(255, 255, 255, 185), Color::rgba(49, 86, 134, 110)});
    }
    return action_recipe(std::move(material));
}

// Fills `result` (caller storage: a family is some 9 KB) with action
// plates, gold when selected. A command member (`flat_at_rest`) draws no
// plate at rest, disabled or deactivated: dividers and strip shading carry
// its structure (design language §12.3), and hover and press bring the
// full plate.
void fill_action_family(ControlRoleRecipes& result, bool flat_at_rest) {
    result = ControlRoleRecipes{};
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        result.ordinary[index] = action_plate(state);
        result.selected[index] = gold_plate(state);
    }
    if (!flat_at_rest) return;
    const std::array<ControlSurfaceState, 3> resting{ControlSurfaceState::normal, ControlSurfaceState::disabled,
                                                     ControlSurfaceState::deactivated};
    for (const ControlSurfaceState state : resting) {
        ControlVisualRecipe& value = result.ordinary[state_index(state)];
        value.material = plate(PlateColours{clear, clear, clear}, 3.0);
        // On the strip's stock, not the plate: stock ink.
        value.text = state == ControlSurfaceState::disabled ? disabled_ink : stock_ink;
        value.glyph = value.text;
    }
}

// The accent button: Plan Paint's File-tab blue (#528BE0 to #173F88),
// started darker (#2F69BE) so that white text meets 4.5:1 on the whole
// gradient.
void fill_accent_family(ControlRoleRecipes& result) {
    fill_action_family(result, false);
    const Relief raised{Color::rgba(255, 255, 255, 90), Color::rgba(10, 30, 70, 120)};
    const std::array<PlateColours, control_surface_state_count> plates{
        PlateColours{Color::rgba(47, 105, 190), Color::rgba(23, 63, 136), Color::rgba(18, 51, 111)},
        PlateColours{Color::rgba(56, 113, 196), Color::rgba(30, 72, 148), Color::rgba(12, 40, 92)},
        PlateColours{Color::rgba(23, 63, 136), Color::rgba(40, 92, 170), Color::rgba(18, 51, 111)},
        PlateColours{Color::rgba(47, 105, 190), Color::rgba(23, 63, 136), Color::rgba(173, 132, 31)},
        PlateColours{Color::rgba(47, 105, 190), Color::rgba(23, 63, 136), Color::rgba(178, 54, 54)},
        PlateColours{Color::rgba(190, 207, 228), Color::rgba(181, 199, 222), Color::rgba(143, 166, 196, 100)},
        PlateColours{Color::rgba(47, 105, 190), Color::rgba(23, 63, 136), Color::rgba(18, 51, 111)}};
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        SurfaceMaterial material = plate(plates[index], 3.0);
        const bool disabled = state == ControlSurfaceState::disabled;
        if (state == ControlSurfaceState::pressed) {
            add_relief(material, Relief{raised.lower, raised.upper});
        } else {
            add_relief(material, raised);
        }
        if (state != ControlSurfaceState::pressed && !disabled) add_contact_shadow(material);
        ControlVisualRecipe value = recipe_of(std::move(material), disabled ? disabled_ink : paper);
        value.muted_text = disabled ? disabled_ink : accent_muted_ink;
        // The house ring lies inside the plate, so on the accent blue it is
        // white.
        value.focus_ring = paper;
        result.ordinary[index] = value;
        result.selected[index] = std::move(value);
    }
}

// -- wells, stock and lists ------------------------------------------------------

// A recessed well: entry fields (`editor`) and choice indicators. The
// border is Plan Paint's action border, not its gallery well's #94ACC8,
// which reaches only 2.2:1 on the stock (the boundary gate is 3:1).
void fill_well_family(ControlRoleRecipes& result, Color face) {
    result = ControlRoleRecipes{};
    const std::array<Color, control_surface_state_count> borders{
        Color::rgba(99, 132, 178), Color::rgba(53, 94, 150),  Color::rgba(53, 94, 150), Color::rgba(173, 132, 31),
        Color::rgba(178, 54, 54),  Color::rgba(154, 170, 190), Color::rgba(99, 132, 178)};
    const std::array<Color, control_surface_state_count> faces{
        face, face, Color::rgba(232, 241, 252), Color::rgba(255, 250, 225), Color::rgba(255, 242, 242), stock, face};
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        const bool disabled = state == ControlSurfaceState::disabled;
        SurfaceMaterial material = plate(PlateColours{faces[index], faces[index], borders[index]}, 2.0);
        if (!disabled) add_well_shade(material);
        ControlVisualRecipe value = recipe_of(std::move(material), disabled ? disabled_ink : stock_ink);
        if (disabled) value.muted_text = disabled_ink;
        result.ordinary[index] = value;
        result.selected[index] = std::move(value);
    }
}

// Flat stock (windows, panels): one solid face with a quiet line. A
// selected panel takes the pale selection.
void fill_stock_family(ControlRoleRecipes& result, Color face, Color line, double radius) {
    result = ControlRoleRecipes{};
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        const bool disabled = state == ControlSurfaceState::disabled;
        ControlVisualRecipe value =
            recipe_of(plate(PlateColours{face, face, line}, radius), disabled ? disabled_ink : stock_ink);
        if (disabled) value.muted_text = disabled_ink;
        result.ordinary[index] = value;
        value.material = plate(PlateColours{Color::rgba(207, 226, 250), Color::rgba(207, 226, 250),
                                            Color::rgba(99, 132, 178)},
                               radius);
        result.selected[index] = std::move(value);
    }
}

// Rows, items and menu members: clear at rest, a pale action face while
// hot or pressed, the pale selection when selected.
void fill_item_family(ControlRoleRecipes& result, double radius) {
    result = ControlRoleRecipes{};
    const Color hot_top = Color::rgba(228, 238, 252);
    const Color hot_bottom = Color::rgba(207, 226, 250);
    const Color line = Color::rgba(99, 132, 178);
    const Color selected = Color::rgba(207, 226, 250);
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        const bool disabled = state == ControlSurfaceState::disabled;
        const bool lit = state == ControlSurfaceState::hot || state == ControlSurfaceState::pressed;
        const PlateColours face = lit ? PlateColours{hot_top, hot_bottom, line} : PlateColours{clear, clear, clear};
        ControlVisualRecipe value = recipe_of(plate(face, radius), disabled ? disabled_ink : stock_ink);
        if (disabled) value.muted_text = disabled_ink;
        result.ordinary[index] = value;
        const PlateColours chosen = state == ControlSurfaceState::pressed
                                        ? PlateColours{hot_bottom, hot_bottom, line}
                                        : PlateColours{selected, selected, line};
        value.material = plate(chosen, radius);
        result.selected[index] = std::move(value);
    }
}

// A progress bar: a recessed track; selected is the bar's fill, in the
// accent blue with white text.
void fill_progress_family(ControlRoleRecipes& result) {
    fill_well_family(result, Color::rgba(248, 251, 255));
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        if (state == ControlSurfaceState::disabled) continue;
        ControlVisualRecipe& value = result.selected[index];
        value.material =
            plate(PlateColours{Color::rgba(47, 105, 190), Color::rgba(23, 63, 136), Color::rgba(18, 51, 111)}, 2.0);
        value.text = paper;
        value.glyph = paper;
        value.muted_text = accent_muted_ink;
    }
}

// Tooltips: windows-professional's pale yellow plate (owner decision RB-38)
// with this theme's text colours.
void fill_tool_tip_family(ControlRoleRecipes& result) {
    fill_stock_family(result, Color::rgba(255, 255, 225), Color::rgba(104, 111, 118), 0.0);
}

// Links: no plate; the link colour in every enabled state (owner decision
// W-Q5), disabled text when disabled.
void fill_link_family(ControlRoleRecipes& result, Color text) {
    result = ControlRoleRecipes{};
    for (std::size_t index = 0U; index < control_surface_state_count; ++index) {
        const ControlSurfaceState state = static_cast<ControlSurfaceState>(index);
        const bool disabled = state == ControlSurfaceState::disabled;
        ControlVisualRecipe value = recipe_of(plate(PlateColours{clear, clear, clear}, 0.0), disabled ? disabled_ink : text);
        if (disabled) value.muted_text = disabled_ink;
        result.ordinary[index] = value;
        result.selected[index] = std::move(value);
    }
}

// -- the light sheet ----------------------------------------------------------------

constexpr std::string_view light_sheet = R"css(/* gui-forms: the default theme's light sheet (stage T4, phase 3).
   Colour roles of design language 5.4 in Plan Paint's palette
   (malkuth-software/plan-paint at 5e28a5e, src/forms/ribbon.cpp) and the
   rules for parts the engine paints. Controls painting from their family's
   recipe draw the theme's floor (gui_forms_theme_definition). */

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
  disabled-ink-on-chrome: #52637a on #becfe4;
  disabled-ink-on-paper: #52637a on #ffffff;
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
tab-control::part(tab), page-strip::part(tab) {
  background-color: #9bb6da;
  background-image: linear-gradient(to bottom, rgb(255 255 255 / 49%) 1px, transparent 1px),
    linear-gradient(to bottom, rgb(34 70 116 / 15%), rgb(34 70 116 / 0%) 72%);
  border: none;
  border-radius: 4px;
  box-shadow: inset -1px 0 0 #496c9f, inset -2px 0 0 #d1e0f5, 3px 1px 2px rgb(24 56 97 / 22%), 2px 0 0 #496c9f;
}
tab-control::part(tab):hover, page-strip::part(tab):hover {
  background-color: #acc6e7;
}
tab-control::part(tab):state(selected), page-strip::part(tab):state(selected) {
  background-color: #e8f2ff;
  background-image: linear-gradient(to bottom, #ffffff 1px, transparent 1px),
    linear-gradient(to bottom, rgb(255 255 255 / 57%), rgb(255 255 255 / 0%) 72%);
}
/* A disabled tab rests on the chrome face, the ground the disabled ink is
   declared against (#52637A on #9BB6DA would read at 2.95:1). */
tab-control::part(tab):disabled, page-strip::part(tab):disabled {
  background: #becfe4;
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
)css";

constexpr std::string_view light_sheet_file = "gui-forms/light.css";

GuiFormsThemeBuild build_gui_forms_theme() {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    GuiFormsThemeBuild result{};
    // The floor is some 100 KB of recipes: built on the heap, not the
    // stack (fill_professional_role's note in theme.cpp).
    const std::unique_ptr<ThemeDefinition> floor = std::make_unique<ThemeDefinition>(gui_forms_theme_definition());
    detail::BuiltInThemeCompilation compiled =
        detail::compile_built_in_theme(*floor, light_sheet_file, light_sheet, stock_control_anatomies());
    result.diagnostics = std::move(compiled.diagnostics);
    result.compile_operations = compiled.operations;
    result.theme = compiled.theme != nullptr ? std::move(compiled.theme) : Theme::create(*floor);
    const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    result.build_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    return result;
}

} // namespace

ThemeDefinition gui_forms_theme_definition() {
    // Every family is restyled; windows-professional's definition supplies
    // the structure tokens, accent ramp and overlay shadow.
    ThemeDefinition result = windows_professional_theme_definition();
    result.id = "gui-forms";
    result.compatibility.face = stock;
    result.compatibility.face_light = Color::rgba(250, 250, 251);
    result.compatibility.paper = paper;
    result.compatibility.border = Color::rgba(148, 172, 200);
    result.compatibility.dark_border = Color::rgba(73, 108, 159);
    result.compatibility.text = stock_ink;
    result.compatibility.disabled_text = disabled_ink;
    result.compatibility.accent = Color::rgba(47, 105, 190);
    result.compatibility.accent_light = Color::rgba(207, 226, 250);
    fill_stock_family(result.roles[role_index(ControlVisualRole::window)], stock, Color::rgba(180, 186, 194), 0.0);
    fill_stock_family(result.roles[role_index(ControlVisualRole::panel)], stock, Color::rgba(205, 209, 214), 0.0);
    fill_stock_family(result.roles[role_index(ControlVisualRole::card)], paper, Color::rgba(112, 130, 156), 4.0);
    fill_action_family(result.roles[role_index(ControlVisualRole::button)], false);
    fill_accent_family(result.roles[role_index(ControlVisualRole::accent_button)]);
    fill_action_family(result.roles[role_index(ControlVisualRole::command_button)], true);
    fill_well_family(result.roles[role_index(ControlVisualRole::choice)], Color::rgba(248, 251, 255));
    fill_well_family(result.roles[role_index(ControlVisualRole::editor)], paper);
    fill_item_family(result.roles[role_index(ControlVisualRole::menu_item)], 2.0);
    fill_item_family(result.roles[role_index(ControlVisualRole::selection)], 0.0);
    fill_progress_family(result.roles[role_index(ControlVisualRole::progress)]);
    fill_tool_tip_family(result.roles[role_index(ControlVisualRole::tool_tip)]);
    fill_link_family(result.roles[role_index(ControlVisualRole::link)], result.compatibility.link);
    fill_link_family(result.roles[role_index(ControlVisualRole::visited_link)], result.compatibility.visited_link);
    // The dark variant, derived by rule (T4 phase 5; design language 5.8).
    result.dark_variant = ThemeDarkVariant::derived;
    return result;
}

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
