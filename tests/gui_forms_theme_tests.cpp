// The default theme `gui-forms` (stage T4, phase 3): the stock anatomy
// table, the light sheet compiled at first use with no diagnostics and
// within its work bound, the sheet's byte-preserving round trip (T3 P11
// item 5), and the light contrast report passing every gated pair (design
// language 5.7). With GUI_FORMS_CONTRAST_REPORT=<path> it writes the
// theme's light report as tab-separated rows there, for the stage record.

#include "../src/core/theme/contrast/theme_contrast.hpp"
#include "../src/core/theme/dark_variant/dark_variant.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/theme.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;
using gui_forms::detail::ContrastCounts;
using gui_forms::detail::ContrastRow;
using gui_forms::detail::ContrastVerdict;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool anatomy_named(std::span<const ControlAnatomy> anatomies, std::string_view id) {
    for (const ControlAnatomy& anatomy : anatomies) {
        if (anatomy.control_id == id) return true;
    }
    return false;
}

bool id_less(const ControlAnatomy& left, const ControlAnatomy& right) noexcept {
    return left.control_id < right.control_id;
}

bool id_equal(const ControlAnatomy& left, const ControlAnatomy& right) noexcept {
    return left.control_id == right.control_id;
}

void stock_anatomies() {
    const std::span<const ControlAnatomy> anatomies = stock_control_anatomies();
    require(anatomies.size() >= 90U, "the table holds every stock control type");
    require(anatomy_named(anatomies, "button"), "the table holds Button");
    require(anatomy_named(anatomies, "tab_control"), "the table holds TabControl");
    std::vector<ControlAnatomy> sorted(anatomies.begin(), anatomies.end());
    std::sort(sorted.begin(), sorted.end(), id_less);
    require(std::adjacent_find(sorted.begin(), sorted.end(), id_equal) == sorted.end(),
            "each control type appears once");
    for (const ControlAnatomy& anatomy : anatomies) {
        require(!anatomy.parts.empty(), "each stock control declares its parts");
    }
}

// The work bound (T3 P11 1a): at most 4 x maximum_variable_combinations
// operations per byte plus 4,096; the light sheet is far inside it, so the
// stated bound here is 64 operations per byte.
constexpr std::uint64_t operations_per_byte_bound = 64U;

void compiled_at_first_use() {
    const GuiFormsThemeBuild& build = gui_forms_theme_build();
    for (const ThemeCompileDiagnostic& diagnostic : build.diagnostics) {
        std::cerr << diagnostic.code << ' ' << diagnostic.name << ' ' << diagnostic.line << ':' << diagnostic.column
                  << ' ' << diagnostic.rule << ' ' << diagnostic.property << ' ' << diagnostic.detail << '\n';
    }
    require(build.diagnostics.empty(), "the floor and light sheets compile with no diagnostics");
    require(build.theme != nullptr, "the build makes a theme");
    require((*build.theme).id() == "gui-forms", "the theme's id is gui-forms (T4-2)");
    require(!(*build.theme).definition().rules.rules.empty(), "the theme holds the sheet's rules");
    require(&gui_forms_theme_build() == &build, "the theme is built once");
    require(gui_forms_theme() == build.theme, "gui_forms_theme() is the build's theme");
    const std::uint64_t bytes = gui_forms_light_sheet().size();
    require(build.compile_operations > 0U, "the compiler's work is counted");
    require(build.compile_operations <= bytes * operations_per_byte_bound, "the compile stays within its work bound");
    const double milliseconds = std::chrono::duration<double, std::milli>(build.build_time).count();
    char line[160]{};
    std::snprintf(line, sizeof(line), "gui-forms build: %llu sheet bytes, %llu operations, %.3f ms\n",
                  static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(build.compile_operations),
                  milliseconds);
    std::cout << line;
}

// -- the floor's reference (owner option 1) ------------------------------------
//
// The C++ builders the floor sheet replaced, kept verbatim as the
// reference its evaluation is held to (floor_matches_reference).
namespace reference {

constexpr std::size_t state_index(ControlSurfaceState state) noexcept {
    return static_cast<std::size_t>(state);
}

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

// The reference floor's families, as gui_forms_theme_definition filled
// them before the floor sheet.
void fill_families(ControlRoleTable& roles) {
    fill_stock_family(roles[static_cast<std::size_t>(ControlVisualRole::window)], stock, Color::rgba(180, 186, 194), 0.0);
    fill_stock_family(roles[static_cast<std::size_t>(ControlVisualRole::panel)], stock, Color::rgba(205, 209, 214), 0.0);
    fill_stock_family(roles[static_cast<std::size_t>(ControlVisualRole::card)], paper, Color::rgba(112, 130, 156), 4.0);
    fill_action_family(roles[static_cast<std::size_t>(ControlVisualRole::button)], false);
    fill_accent_family(roles[static_cast<std::size_t>(ControlVisualRole::accent_button)]);
    fill_action_family(roles[static_cast<std::size_t>(ControlVisualRole::command_button)], true);
    fill_well_family(roles[static_cast<std::size_t>(ControlVisualRole::choice)], Color::rgba(248, 251, 255));
    fill_well_family(roles[static_cast<std::size_t>(ControlVisualRole::editor)], paper);
    fill_item_family(roles[static_cast<std::size_t>(ControlVisualRole::menu_item)], 2.0);
    fill_item_family(roles[static_cast<std::size_t>(ControlVisualRole::selection)], 0.0);
    fill_progress_family(roles[static_cast<std::size_t>(ControlVisualRole::progress)]);
    fill_tool_tip_family(roles[static_cast<std::size_t>(ControlVisualRole::tool_tip)]);
    fill_link_family(roles[static_cast<std::size_t>(ControlVisualRole::link)], Color::rgba(25, 82, 139));
    fill_link_family(roles[static_cast<std::size_t>(ControlVisualRole::visited_link)], Color::rgba(93, 65, 145));
}

} // namespace reference

// The forms of one drawing the sheet and the reference spell differently:
// a fully transparent solid of any colour, and a downward linear gradient
// in normalized coordinates as endpoints (0,0) to (0,1) or as CSS's 180
// degrees. Each is rewritten to one spelling, in place.
void canonical_layer(MaterialFillLayer& layer) {
    if (layer.kind == MaterialFillKind::solid && layer.color.alpha == 0U) layer.color = Color::rgba(0, 0, 0, 0);
    if (layer.kind != MaterialFillKind::linear_gradient) return;
    if (layer.coordinate_space != MaterialCoordinateSpace::normalized) return;
    const bool endpoints = layer.linear_geometry == MaterialLinearGeometry::endpoints &&
                           layer.start == Point{0.0, 0.0} && layer.end == Point{0.0, 1.0};
    const bool angle = layer.linear_geometry == MaterialLinearGeometry::css_angle && layer.angle_degrees == 180.0;
    if (!endpoints && !angle) return;
    layer.linear_geometry = MaterialLinearGeometry::css_angle;
    layer.angle_degrees = 180.0;
    layer.start = Point{};
    layer.end = Point{1.0, 0.0};
}

ControlVisualRecipe canonical_recipe(const ControlVisualRecipe& recipe) {
    ControlVisualRecipe result = recipe;
    for (MaterialFillLayer& layer : result.material.fills) canonical_layer(layer);
    return result;
}

// Prints what differs between `evaluated` and `expected` for one record.
void print_difference(ControlVisualRole role, std::size_t state, bool selected, const ControlVisualRecipe& evaluated,
                      const ControlVisualRecipe& expected) {
    std::cerr << "floor differs: " << control_visual_role_name(role) << ' '
              << detail::control_surface_state_name(static_cast<ControlSurfaceState>(state))
              << (selected ? " selected:" : ":");
    const SurfaceMaterial& left = evaluated.material;
    const SurfaceMaterial& right = expected.material;
    if (left.fills != right.fills) std::cerr << " fills(" << left.fills.size() << '/' << right.fills.size() << ')';
    if (left.border != right.border) std::cerr << " border";
    if (!(left.border_edges == right.border_edges)) std::cerr << " border_edges";
    if (left.keylines != right.keylines) {
        std::cerr << " keylines(" << left.keylines.size() << '/' << right.keylines.size() << ')';
    }
    if (left.shadows != right.shadows) std::cerr << " shadows(" << left.shadows.size() << '/' << right.shadows.size() << ')';
    if (left.corner_radius != right.corner_radius || left.corner_radii != right.corner_radii) std::cerr << " corners";
    if (!(left == right)) std::cerr << " material";
    if (evaluated.text != expected.text) std::cerr << " text";
    if (evaluated.muted_text != expected.muted_text) std::cerr << " muted_text";
    if (evaluated.glyph != expected.glyph) std::cerr << " glyph";
    if (evaluated.focus_ring != expected.focus_ring) std::cerr << " focus_ring";
    if (evaluated.default_ring != expected.default_ring) std::cerr << " default_ring";
    if (evaluated.focus_width != expected.focus_width) std::cerr << " focus_width";
    if (evaluated.default_width != expected.default_width) std::cerr << " default_width";
    if (evaluated.dotted_focus != expected.dotted_focus) std::cerr << " dotted_focus";
    std::cerr << '\n';
}

// The floor sheet (owner option 1): it compiles with no diagnostics within
// the work bound, round-trips, and its evaluation draws every family's
// ordinary and selected record exactly as the C++ builders it replaced.
void floor_matches_reference() {
    const GuiFormsThemeBuild& build = gui_forms_theme_build();
    const std::uint64_t bytes = gui_forms_floor_sheet().size();
    require(build.floor_compile_operations > 0U, "the floor sheet's work is counted");
    require(build.floor_compile_operations <= bytes * operations_per_byte_bound,
            "the floor sheet's compile stays within its work bound");
    const std::unique_ptr<ThemeDefinition> expected = std::make_unique<ThemeDefinition>();
    reference::fill_families((*expected).roles);
    const std::unique_ptr<ThemeDefinition> evaluated = std::make_unique<ThemeDefinition>(gui_forms_theme_definition());
    std::size_t differences = 0U;
    for (std::size_t role = 0U; role < control_visual_role_count; ++role) {
        const ControlRoleRecipes& left = (*evaluated).roles[role];
        const ControlRoleRecipes& right = (*expected).roles[role];
        for (std::size_t state = 0U; state < control_surface_state_count; ++state) {
            for (std::size_t chosen = 0U; chosen < 2U; ++chosen) {
                const bool selected = chosen == 1U;
                const ControlVisualRecipe evaluated_recipe =
                    canonical_recipe(selected ? left.selected[state] : left.ordinary[state]);
                const ControlVisualRecipe expected_recipe =
                    canonical_recipe(selected ? right.selected[state] : right.ordinary[state]);
                if (evaluated_recipe == expected_recipe) continue;
                ++differences;
                print_difference(static_cast<ControlVisualRole>(role), state, selected, evaluated_recipe,
                                 expected_recipe);
            }
        }
        require(left.high_contrast == right.high_contrast && left.high_contrast_selected == right.high_contrast_selected,
                "the floor leaves the high-contrast records to Theme::create");
    }
    require(differences == 0U, "the floor sheet draws every family as the reference builders");
}

void round_trip_sheet(std::string_view file, std::string_view text) {
    const theme_language::ThemeSource source{.file = file, .text = text};
    theme_language::ThemeTokenBuffer buffer{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeDiagnosticList diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, buffer, lines, diagnostics));
    theme_language::ThemeSyntaxTree tree{};
    const theme_language::ParseOutcome parsed =
        theme_language::parse_theme_sheet(source, buffer, lines, tree, diagnostics);
    require(parsed == theme_language::ParseOutcome::tree_complete, "the sheet parses completely");
    std::string written{};
    const theme_language::WriteOutcome write = theme_language::write_theme_sheet(tree, buffer, text, written);
    require(write == theme_language::WriteOutcome::written, "the sheet's tree nests");
    require(written == text, "the sheet round-trips to identical bytes");
}

void round_trip() {
    round_trip_sheet("gui-forms/light.css", gui_forms_light_sheet());
    round_trip_sheet("gui-forms/floor.css", gui_forms_floor_sheet());
}

void write_report(const std::vector<ContrastRow>& rows, const char* path) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "the report file opens");
    out << "presentation\tfamily\tstate\tselected\tpair\tforeground\tbackground\tratio\tminimum\tverdict\n";
    for (const ContrastRow& row : rows) {
        char colours[40]{};
        std::snprintf(colours, sizeof(colours), "#%02X%02X%02X\t#%02X%02X%02X\t%.2f", row.foreground.red,
                      row.foreground.green, row.foreground.blue, row.background.red, row.background.green,
                      row.background.blue, row.ratio);
        const char* const presentation = row.high_contrast ? "high_contrast" : row.dark ? "dark" : "ordinary";
        out << presentation << '\t' << control_visual_role_name(row.family)
            << '\t' << detail::control_surface_state_name(row.state) << '\t' << (row.selected ? "yes" : "no") << '\t'
            << detail::contrast_pair_name(row.pair) << '\t' << colours << '\t' << row.minimum << '\t'
            << detail::contrast_verdict_name(row.verdict) << '\n';
    }
}

// Prints each failing row of `rows` and returns how many there are.
std::size_t print_failures(const std::vector<ContrastRow>& rows) {
    std::size_t failed = 0U;
    for (const ContrastRow& row : rows) {
        if (row.verdict != ContrastVerdict::fail) continue;
        ++failed;
        char line[200]{};
        std::snprintf(line, sizeof(line), "fails: %s %s %s %s #%02X%02X%02X on #%02X%02X%02X %.2f < %.1f\n",
                      std::string(control_visual_role_name(row.family)).c_str(),
                      std::string(detail::control_surface_state_name(row.state)).c_str(),
                      row.selected ? "selected" : "-", std::string(detail::contrast_pair_name(row.pair)).c_str(),
                      row.foreground.red, row.foreground.green, row.foreground.blue, row.background.red,
                      row.background.green, row.background.blue, row.ratio, row.minimum);
        std::cerr << line;
    }
    return failed;
}

// The rows of one presentation.
std::vector<ContrastRow> presentation_rows(const std::vector<ContrastRow>& rows, bool high_contrast, bool dark) {
    std::vector<ContrastRow> result{};
    for (const ContrastRow& row : rows) {
        if (row.high_contrast == high_contrast && row.dark == dark) result.push_back(row);
    }
    return result;
}

// Light (the gate of phase 3), dark and high contrast (phase 5): every
// gated pair meets its minimum.
void light_contrast() {
    std::vector<ContrastRow> rows{};
    detail::measure_theme_contrast(*gui_forms_theme(), rows);
    const std::vector<ContrastRow> light = presentation_rows(rows, false, false);
    const std::size_t failed = print_failures(light);
    const std::vector<ContrastRow> dark = presentation_rows(rows, false, true);
    require(!dark.empty(), "the derived dark presentation is measured");
    const ContrastCounts dark_counts = detail::count_contrast_verdicts(dark);
    std::cout << "gui-forms dark contrast: " << dark_counts.passed << " pass, " << dark_counts.failed << " fail, "
              << dark_counts.not_measured << " not measured\n";
    const std::size_t dark_failed = print_failures(dark);
    // GUI.Forms' own high contrast from the role model (phase 5d): text at
    // 7:1, as design language 5.8 requires.
    const std::vector<ContrastRow> forced = presentation_rows(rows, true, false);
    require(!forced.empty(), "the high-contrast presentation is measured");
    const ContrastCounts forced_counts = detail::count_contrast_verdicts(forced);
    std::cout << "gui-forms high contrast: " << forced_counts.passed << " pass, " << forced_counts.failed
              << " fail, " << forced_counts.not_measured << " not measured\n";
    const std::size_t forced_failed = print_failures(forced);
    const ContrastCounts counts = detail::count_contrast_verdicts(light);
    std::cout << "gui-forms light contrast: " << counts.passed << " pass, " << counts.failed << " fail, "
              << counts.not_measured << " not measured\n";
    const char* const path = std::getenv("GUI_FORMS_CONTRAST_REPORT");
    if (path != nullptr && path[0] != '\0') write_report(rows, path);
    require(failed == 0U, "every gated light pair meets its minimum");
    require(dark_failed == 0U, "every gated dark pair meets its minimum");
    require(forced_failed == 0U, "every gated high-contrast pair meets its minimum");
}

// The dark records (phase 5): gui-forms derives them, so a dark context
// resolves colours unlike the light ones with the same geometry; high
// contrast wins over dark; a theme without a dark variant resolves its
// ordinary records in a dark context.
void dark_records() {
    const std::shared_ptr<const Theme> theme = gui_forms_theme();
    ControlVisualContext light{};
    ControlVisualContext dark{};
    dark.dark = true;
    const ControlVisualRecipe& light_button = (*theme).resolve(ControlVisualRole::button, light);
    const ControlVisualRecipe& dark_button = (*theme).resolve(ControlVisualRole::button, dark);
    require(!(light_button.text == dark_button.text), "a dark button's text is derived");
    require(light_button.material.corner_radius == dark_button.material.corner_radius,
            "a dark button keeps its geometry");
    require(detail::relative_luminance(dark_button.text) > detail::relative_luminance(light_button.text),
            "dark text is lighter than light text");
    ControlVisualContext forced = dark;
    forced.high_contrast = true;
    ControlVisualContext forced_light{};
    forced_light.high_contrast = true;
    require((*theme).resolve(ControlVisualRole::button, forced) ==
                (*theme).resolve(ControlVisualRole::button, forced_light),
            "high contrast wins over dark");
    const std::shared_ptr<const Theme> professional = Theme::create(windows_professional_theme_definition());
    require((*professional).resolve(ControlVisualRole::button, dark) ==
                (*professional).resolve(ControlVisualRole::button, light),
            "a theme without a dark variant keeps its ordinary records");
}

// A control that paints nothing and presents itself as a ToolStrip, whose
// face the light sheet styles (tool-strip::part(face)); its parts are
// planned and painted by the engine.
constexpr ControlAnatomy probe_anatomy{
    .control_id = "tool_strip",
    .parts = {},
    .states = {},
    .custom_states = {},
};

class Probe final : public Control {
public:
    explicit Probe(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override { return probe_anatomy; }
};

// A painter that keeps the part rasters drawn and draws nothing.
class RasterWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        rasters.push_back(raster);
    }
    std::vector<std::shared_ptr<const PartRaster>> rasters;
};

// The coverage-weighted mean of the green channel (premultiplied BGRA8),
// over the rasters `painter` kept, with their total coverage; a stand-in
// for lightness that a darkened material lowers.
struct RasterMean final {
    double green{};
    double coverage{};
};

RasterMean raster_mean(const RasterWatch& painter) {
    RasterMean result{};
    double green = 0.0;
    for (const std::shared_ptr<const PartRaster>& raster : painter.rasters) {
        const std::span<const std::uint8_t> pixels = (*raster).pixels();
        const std::size_t width = (*raster).extent().width;
        const std::size_t height = (*raster).extent().height;
        for (std::size_t row = 0U; row < height; ++row) {
            const std::size_t start = row * (*raster).row_bytes();
            for (std::size_t column = 0U; column < width; ++column) {
                const std::size_t at = start + column * 4U;
                green = green + static_cast<double>(pixels[at + 1U]);
                result.coverage = result.coverage + static_cast<double>(pixels[at + 3U]);
            }
        }
    }
    if (result.coverage > 0.0) result.green = green / result.coverage;
    return result;
}

void set_scheme(Window& window, ColourScheme scheme) {
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = scheme;
    window.set_presentation_settings(settings);
}

// T4 phase 5c: in a dark window the engine darkens, by rule, what the
// sheet writes in light terms. Each part of the sheet drawn by a primitive
// over its recipe's material with fields the rules set is painted light and
// dark: the dark raster is darker, and its colour parameters are the rule's
// (detail::dark_parameter_colour) of the light ones.
void sheet_darkened() {
    const std::shared_ptr<Probe> root = make_control<Probe>(StableId("root"));
    const std::shared_ptr<Probe> probe = make_control<Probe>(StableId("probe"));
    (*probe).set_requested_bounds(Rect{10.0, 10.0, 120.0, 40.0});
    (*root).add_child(probe);
    Window window(root, Size{200.0, 100.0});
    window.set_theme(gui_forms_theme());
    window.perform_layout();
    const ThemeEngine& engine = window.theme().engine();
    std::size_t parts = 0U;
    std::size_t parameters = 0U;
    for (std::size_t family = 0U; family < control_visual_role_count; ++family) {
        for (std::size_t role = 0U; role < part_role_count; ++role) {
            PartPaint part{};
            part.family = static_cast<ControlVisualRole>(family);
            part.role = static_cast<PartRole>(role);
            part.bounds = Rect{0.0, 0.0, 80.0, 24.0};
            part.key = PartKey{part_role_name(part.role), {}, {}};
            set_scheme(window, ColourScheme::light);
            const ThemeEngine::PartPlan light = engine.plan(*probe, part);
            if (light.drawing != ThemeEngine::PartDrawing::primitive || light.resolution.primitive == nullptr) {
                continue;
            }
            RasterWatch light_painter{};
            engine.paint(light_painter, *probe, part);
            set_scheme(window, ColourScheme::dark);
            const ThemeEngine::PartPlan dark = engine.plan(*probe, part);
            require(dark.resolution.primitive == light.resolution.primitive, "a dark window plans the same primitive");
            const PrimitiveDescriptor& described = (*light.resolution.primitive).descriptor();
            for (std::size_t index = 0U; index < light.resolution.parameters.count; ++index) {
                if (index >= described.parameters.size()) break;
                if (described.parameters[index].kind != PrimitiveParameterKind::colour) continue;
                const Color expected = detail::dark_parameter_colour(light.resolution.parameters.values[index].colour,
                                                                     light.resolution.parameter_origins[index],
                                                                     part.family);
                require(dark.resolution.parameters.values[index].colour == expected,
                        "a colour parameter darkens by the rule");
                parameters = parameters + 1U;
            }
            if (!light.resolution.materials_authored) continue;
            RasterWatch dark_painter{};
            engine.paint(dark_painter, *probe, part);
            const RasterMean lit = raster_mean(light_painter);
            const RasterMean darkened = raster_mean(dark_painter);
            if (lit.coverage <= 0.0) continue;
            require(darkened.coverage > 0.0 && darkened.green < lit.green, "an authored material darkens by the rule");
            parts = parts + 1U;
        }
    }
    set_scheme(window, ColourScheme::light);
    std::cout << "gui-forms darkened sheet: " << parts << " authored materials, " << parameters
              << " colour parameters\n";
    require(parts > 0U, "the sheet has authored materials to darken");
}

} // namespace

int main() {
    stock_anatomies();
    compiled_at_first_use();
    round_trip();
    floor_matches_reference();
    light_contrast();
    dark_records();
    sheet_darkened();
    std::cout << "gui-forms theme: anatomies, first-use compile, round trip, light, dark and high contrast, dark records and the darkened sheet passed\n";
    return 0;
}
