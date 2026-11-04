// Authored materials (T3 engine change EC1, owner R2-02): a rule's material
// fields are checked when the rules compile, cascade one by one over every
// matching rule, and are assembled over the part's recipe material, the
// cascade's floor; the border's edges assemble together, as CSS's
// longhands. The engine bakes the assembled material as it would the same
// material written into the theme's recipe in C++, assembles it once per
// distinct assembly for a keyed part (engine_material_assemblies), and
// draws an assembly no material admits with the built-in recipe. The
// material primitive's default recipe is the part's own family.

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_authored_material_tests: " << message << '\n';
    std::exit(1);
}

MaterialDeclaration fills_of(Color colour) {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::background_colour;
    declaration.colour = colour;
    return declaration;
}

MaterialDeclaration number_of(MaterialField field, double value) {
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.number = value;
    return declaration;
}

MaterialDeclaration style_of(MaterialField field, std::optional<MaterialBorderStyle> style) {
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.border_style = style;
    return declaration;
}

MaterialDeclaration colour_of(MaterialField field, Color colour) {
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.colour = colour;
    return declaration;
}

std::size_t at(MaterialField field) {
    const std::size_t position = static_cast<std::size_t>(field);
    return position;
}

constexpr std::array<MaterialField, 4> corner_fields{MaterialField::corner_top_left, MaterialField::corner_top_right,
                                                     MaterialField::corner_bottom_right,
                                                     MaterialField::corner_bottom_left};

// The four corners at `radius` (a fraction of the shorter side when
// `relative`), as `border-radius` sets them.
std::array<MaterialDeclaration, 4> corners_of(double radius, bool relative) {
    std::array<MaterialDeclaration, 4> corners{};
    for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
        corners[corner].field = corner_fields[corner];
        corners[corner].number = radius;
        corners[corner].relative = relative;
    }
    return corners;
}

void push_corners(ThemeRule& rule, double radius) {
    const std::array<MaterialDeclaration, 4> corners = corners_of(radius, false);
    for (const MaterialDeclaration& corner : corners) rule.materials.push_back(corner);
}

void point_at(const std::array<MaterialDeclaration, 4>& corners,
              std::array<const MaterialDeclaration*, material_field_count>& fields) {
    for (const MaterialDeclaration& corner : corners) fields[at(corner.field)] = &corner;
}
void point_at(std::array<MaterialDeclaration, 4>&& corners,
              std::array<const MaterialDeclaration*, material_field_count>& fields) = delete;

MaterialBorder border_of(Color colour, double width) {
    MaterialBorder border{};
    border.color = colour;
    border.width = width;
    return border;
}

ThemeRule part_rule(std::string_view label, std::string_view stable_id, std::string_view part) {
    ThemeRule rule{};
    rule.label = std::string(label);
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::string(stable_id);
    rule.selector.part = std::string(part);
    return rule;
}

bool has_diagnostic(const CompiledThemeRules& rules, RuleDiagnosticKind kind, std::string_view label,
                    std::string_view property) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind && diagnostic.label == label && diagnostic.property == property) return true;
    }
    return false;
}

// -- compiling ------------------------------------------------------------------------

void test_field_names() {
    const std::array<std::string_view, material_field_count> expected{
        "background_colour", "fills",         "fill_blend_modes",
        "fill_positions",    "fill_sizes",    "fill_repeats",     "shadows",           "glows",
        "glow_placements",
        "border_top_style",    "border_top_width",    "border_top_colour",
        "border_right_style",  "border_right_width",  "border_right_colour",
        "border_bottom_style", "border_bottom_width", "border_bottom_colour",
        "border_left_style",   "border_left_width",   "border_left_colour",  "border_image",
        "keylines",      "corner_top_left", "corner_top_right", "corner_bottom_right", "corner_bottom_left",
        "colour_filter",
        "drop_shadow",   "filter_blur",       "finish",
        "mask",          "reflection"};
    for (std::size_t index = 0U; index < material_field_count; ++index) {
        require(material_field_name(static_cast<MaterialField>(index)) == expected[index],
                "each material field has its name, in enum order");
    }
    require(material_field_name(MaterialField::count).empty(), "the count names no field");
}

void test_compile() {
    ThemeRuleSheet sheet{};
    ThemeRule fields_only = part_rule("fields", "pad", "face");
    fields_only.materials.push_back(fills_of(Color::rgba(200, 0, 0)));
    fields_only.materials.push_back(number_of(MaterialField::corner_top_left, 4.0));
    sheet.rules.push_back(fields_only);
    ThemeRule repeated = part_rule("repeated", "pad", "face");
    repeated.materials.push_back(fills_of(Color::rgba(200, 0, 0)));
    repeated.materials.push_back(fills_of(Color::rgba(0, 0, 200)));
    sheet.rules.push_back(repeated);
    ThemeRule invalid = part_rule("invalid", "pad", "face");
    MaterialDeclaration nine_fills{};
    nine_fills.field = MaterialField::fills;
    nine_fills.fills.assign(SurfaceMaterial::maximum_fill_layers + 1U, MaterialFillLayer::solid(Color::rgba(0, 0, 0)));
    invalid.materials.push_back(nine_fills);
    invalid.materials.push_back(number_of(MaterialField::corner_top_left, -1.0));
    invalid.materials.push_back(number_of(MaterialField::finish, 2.0));
    invalid.materials.push_back(number_of(MaterialField::count, 0.0));
    invalid.materials.push_back(number_of(MaterialField::border_left_width, -1.0));
    invalid.materials.push_back(number_of(MaterialField::border_top_width, 65.0));
    sheet.rules.push_back(invalid);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());

    require(!has_diagnostic(rules, RuleDiagnosticKind::rule_empty, "fields", {}),
            "a rule declaring only material fields declares something");
    require(has_diagnostic(rules, RuleDiagnosticKind::duplicate_declaration, "repeated", "background_colour"),
            "a second declaration of a field in one rule is a duplicate");
    require(has_diagnostic(rules, RuleDiagnosticKind::material_field_invalid, "invalid", "fills") &&
                has_diagnostic(rules, RuleDiagnosticKind::material_field_invalid, "invalid", "corner_top_left") &&
                has_diagnostic(rules, RuleDiagnosticKind::material_field_invalid, "invalid", "finish") &&
                has_diagnostic(rules, RuleDiagnosticKind::material_field_invalid, "invalid", {}) &&
                has_diagnostic(rules, RuleDiagnosticKind::material_field_invalid, "invalid", "border_left_width") &&
                has_diagnostic(rules, RuleDiagnosticKind::material_field_invalid, "invalid", "border_top_width"),
            "a field no material admits is dropped: nine fills, a negative radius, a finish past 1, no field, "
            "a negative or over-wide edge");
    require(has_diagnostic(rules, RuleDiagnosticKind::rule_empty, "invalid", {}),
            "a rule whose every field is dropped is empty");
    require(rules.diagnostics().size() == 8U, "nothing else is reported");
    require(rule_diagnostic_name(RuleDiagnosticKind::material_field_invalid) == "material_field_invalid",
            "the new diagnostic has its name");
}

// -- the cascade ----------------------------------------------------------------------

void test_cascade() {
    ThemeRuleSheet sheet{};
    ThemeRule faces = part_rule("faces", {}, "face");
    faces.materials.push_back(fills_of(Color::rgba(200, 0, 0)));
    faces.materials.push_back(number_of(MaterialField::border_top_width, 2.0));
    sheet.rules.push_back(faces);
    // More specific, so later in the cascade whatever its position.
    ThemeRule pad = part_rule("pad", "pad", "face");
    pad.materials.push_back(fills_of(Color::rgba(0, 0, 200)));
    MaterialDeclaration initial_glows{};
    initial_glows.field = MaterialField::glows;
    initial_glows.initial = true;
    pad.materials.push_back(initial_glows);
    sheet.rules.insert(sheet.rules.begin(), pad);
    MaterialDeclaration glows{};
    glows.field = MaterialField::glows;
    glows.glows.push_back(MaterialGlow{});
    sheet.rules[1].materials.push_back(glows);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(rules.diagnostics().empty(), "the cascade sheet compiles cleanly");

    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "pad";
    facts.stable_id = "pad";
    facts.part = "face";
    facts.role = PartRole::face;
    const CompiledThemeRules::Resolution both = rules.resolve(facts);
    const MaterialDeclaration* const fills = both.materials[at(MaterialField::background_colour)];
    const MaterialDeclaration* const top = both.materials[at(MaterialField::border_top_width)];
    require(both.materials_authored && fills != nullptr && top != nullptr, "both rules set fields of the pad's face");
    require((*fills) == fills_of(Color::rgba(0, 0, 200)), "the more specific rule's fills win");
    require((*top).number == 2.0,
            "a field only the less specific rule sets is still its: each field cascades on its own");
    require(both.materials[at(MaterialField::corner_top_left)] == nullptr,
            "a field no rule sets has no winner");
    const MaterialDeclaration* const glow = both.materials[static_cast<std::size_t>(MaterialField::glows)];
    require(glow != nullptr && (*glow).initial, "an initial declaration wins over the rules beneath it");
    require(both.outcome == CompiledThemeRules::Outcome::no_rule,
            "material fields select no primitive: they change a part only where one draws a material");

    facts.stable_id = "other";
    const CompiledThemeRules::Resolution other = rules.resolve(facts);
    const MaterialDeclaration* const other_fills = other.materials[at(MaterialField::background_colour)];
    require(other_fills != nullptr && (*other_fills) == fills_of(Color::rgba(200, 0, 0)),
            "another face takes the general rule's fills");
    facts.part = "label";
    const CompiledThemeRules::Resolution label = rules.resolve(facts);
    require(!label.materials_authored && label.materials[0] == nullptr, "a part no rule matches has no fields");
}

void test_serials() {
    const ThemeRuleSheet sheet{};
    const CompiledThemeRules first = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    const CompiledThemeRules second = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(first.serial() != second.serial(), "each compilation has its own serial");
    const CompiledThemeRules copied(first);
    require(copied.serial() != first.serial(), "a copy, whose declarations lie elsewhere, takes a new serial");
    const std::uint64_t held = second.serial();
    CompiledThemeRules source = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    const std::uint64_t moving = source.serial();
    const CompiledThemeRules moved(std::move(source));
    require(moved.serial() == moving && moving != held, "a move keeps the serial with the declarations");
}

// -- assembly -------------------------------------------------------------------------

std::array<const MaterialDeclaration*, material_field_count> no_fields() {
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    return fields;
}

void test_assembly() {
    SurfaceMaterial floor{};
    floor.border = border_of(Color::rgba(10, 20, 30), 1.0);
    (*floor.border).style = MaterialBorderStyle::dashed;
    floor.corner_radius = 5.0;
    SurfaceMaterial assembled{};

    std::array<const MaterialDeclaration*, material_field_count> fields = no_fields();
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled == floor, "with no field set the assembly is the floor");

    const MaterialDeclaration red = fills_of(Color::rgba(200, 0, 0));
    const std::array<MaterialDeclaration, 4> radius = corners_of(2.0, false);
    fields[at(MaterialField::background_colour)] = &red;
    point_at(radius, fields);
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    SurfaceMaterial expected = floor;
    expected.fills.assign(1U, MaterialFillLayer::solid(Color::rgba(200, 0, 0)));
    expected.corner_radius = 2.0;
    require(assembled == expected, "each set field replaces the floor's, and the others stay");

    // An edge equal to the floor's border leaves a uniform border, its dash
    // and its rounding kept.
    const MaterialDeclaration same_width = number_of(MaterialField::border_top_width, 1.0);
    fields = no_fields();
    fields[at(MaterialField::border_top_width)] = &same_width;
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled == floor, "four equal edges are one uniform border");

    // The longhands combine: a width and a colour from two declarations.
    const MaterialDeclaration thick = number_of(MaterialField::border_top_width, 3.0);
    const MaterialDeclaration black = colour_of(MaterialField::border_top_colour, Color::rgba(0, 0, 0));
    fields[at(MaterialField::border_top_width)] = &thick;
    fields[at(MaterialField::border_top_colour)] = &black;
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    MaterialBorder expected_top = *floor.border;
    expected_top.width = 3.0;
    expected_top.color = Color::rgba(0, 0, 0);
    require(!assembled.border && assembled.border_edges.top == expected_top &&
                assembled.border_edges.right == floor.border && assembled.border_edges.bottom == floor.border &&
                assembled.border_edges.left == floor.border,
            "an unequal edge splits the border, keeping the floor's style; the others keep the floor's");
    require(!valid_surface_material(assembled), "single edges with rounded corners are no material");
    const std::array<MaterialDeclaration, 4> square = corners_of(0.0, false);
    point_at(square, fields);
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(valid_surface_material(assembled), "with square corners they are");

    // A floor with no border: a style draws the default border; a width
    // alone draws nothing, as CSS's style none.
    const SurfaceMaterial bare{};
    std::array<MaterialDeclaration, 4> solids{};
    const std::array<MaterialField, 4> style_fields{MaterialField::border_top_style, MaterialField::border_right_style,
                                                    MaterialField::border_bottom_style,
                                                    MaterialField::border_left_style};
    fields = no_fields();
    for (std::size_t edge = 0U; edge < solids.size(); ++edge) {
        solids[edge] = style_of(style_fields[edge], MaterialBorderStyle::solid);
        fields[at(style_fields[edge])] = &solids[edge];
    }
    assemble_material(bare, fields, Size{20.0, 40.0}, assembled);
    require(assembled.border == MaterialBorder{} && assembled.border_edges.empty(),
            "four solid edges over no border are the default uniform border");
    const MaterialDeclaration none_width = number_of(MaterialField::border_left_width, 0.0);
    fields[at(MaterialField::border_left_width)] = &none_width;
    assemble_material(bare, fields, Size{20.0, 40.0}, assembled);
    require(!assembled.border && !assembled.border_edges.left && assembled.border_edges.top == MaterialBorder{},
            "a zero width draws no edge");
    fields = no_fields();
    fields[at(MaterialField::border_top_width)] = &thick;
    assemble_material(bare, fields, Size{20.0, 40.0}, assembled);
    require(assembled == bare, "a width with no style draws nothing");

    // A floor drawn with single edges lends them edge by edge.
    SurfaceMaterial edged{};
    edged.border_edges.bottom = border_of(Color::rgba(1, 2, 3), 1.0);
    const MaterialDeclaration no_top = style_of(MaterialField::border_top_style, std::nullopt);
    fields = no_fields();
    fields[at(MaterialField::border_top_style)] = &no_top;
    assemble_material(edged, fields, Size{20.0, 40.0}, assembled);
    require(assembled == edged, "the floor's single edges stay where no edge is set");
    const MaterialDeclaration bottom_none = style_of(MaterialField::border_bottom_style, std::nullopt);
    fields[at(MaterialField::border_bottom_style)] = &bottom_none;
    assemble_material(edged, fields, Size{20.0, 40.0}, assembled);
    require(!assembled.border && assembled.border_edges.empty(), "four absent edges are no border");

    // Relative corners are fractions of the shorter side.
    std::array<MaterialDeclaration, 4> relative = corners_of(0.5, true);
    fields = no_fields();
    point_at(relative, fields);
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled.corner_radius == 10.0 && !assembled.corner_radii, "50% of a 20-unit side is 10");
    relative[0].number = 0.25;
    relative[1] = number_of(MaterialField::corner_top_right, 3.0);
    relative[3] = number_of(MaterialField::corner_bottom_left, 1.0);
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled.corner_radii == (MaterialCornerRadii{5.0, 3.0, 10.0, 1.0}) && assembled.corner_radius == 1.0,
            "each relative corner is resolved, the others kept in units; the built-in path takes the smallest");
    fields = no_fields();
    const MaterialDeclaration one_corner = number_of(MaterialField::corner_bottom_right, 8.0);
    fields[at(MaterialField::corner_bottom_right)] = &one_corner;
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled.corner_radii == (MaterialCornerRadii{5.0, 5.0, 8.0, 5.0}) && assembled.corner_radius == 5.0,
            "a corner set alone keeps the floor's others: each corner cascades on its own");
    fields = no_fields();
    fields[at(MaterialField::border_top_style)] = &no_top;

    // `initial` keeps the floor's value, for an edge as for any field.
    MaterialDeclaration initial_bottom = bottom_none;
    initial_bottom.initial = true;
    MaterialDeclaration initial_fills{};
    initial_fills.initial = true;
    fields[at(MaterialField::border_bottom_style)] = &initial_bottom;
    initial_fills.field = MaterialField::fills;
    fields[at(MaterialField::fills)] = &initial_fills;
    assemble_material(edged, fields, Size{20.0, 40.0}, assembled);
    require(assembled == edged, "an initial field is the floor's");
}

// The fill fields compose (owner C6): the colour under the image layers,
// each the declared one, else the floor's.
MaterialFillLayer veil() {
    const std::vector<GradientStop> stops{GradientStop{0.0, Color::rgba(255, 255, 255, 160)},
                                          GradientStop{1.0, Color::rgba(0, 0, 0, 64)}};
    const MaterialFillLayer layer = MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, stops);
    return layer;
}

MaterialDeclaration images_of(std::vector<MaterialFillLayer> layers) {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::fills;
    declaration.fills = std::move(layers);
    return declaration;
}

void test_fill_composition() {
    SurfaceMaterial floor{};
    floor.fills = {MaterialFillLayer::solid(Color::rgba(9, 9, 9)), veil()};
    SurfaceMaterial assembled{};
    std::array<const MaterialDeclaration*, material_field_count> fields = no_fields();
    const MaterialDeclaration red = fills_of(Color::rgba(200, 0, 0));
    fields[at(MaterialField::background_colour)] = &red;
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled.fills == (std::vector<MaterialFillLayer>{MaterialFillLayer::solid(Color::rgba(200, 0, 0)), veil()}),
            "a colour replaces the floor's solid bottom layer, under the floor's image layers");
    const MaterialDeclaration none = images_of({});
    fields[at(MaterialField::fills)] = &none;
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled.fills == (std::vector<MaterialFillLayer>{MaterialFillLayer::solid(Color::rgba(200, 0, 0))}),
            "no image layers leave the colour alone");
    const MaterialDeclaration clear = fills_of(Color::rgba(0, 0, 0, 0));
    fields[at(MaterialField::background_colour)] = &clear;
    assemble_material(floor, fields, Size{20.0, 40.0}, assembled);
    require(assembled.fills == (std::vector<MaterialFillLayer>{MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0))}),
            "with no layer at all the fill is one transparent solid");
    SurfaceMaterial graded{};
    graded.fills = {veil()};
    fields = no_fields();
    fields[at(MaterialField::background_colour)] = &red;
    assemble_material(graded, fields, Size{20.0, 40.0}, assembled);
    require(assembled.fills == (std::vector<MaterialFillLayer>{MaterialFillLayer::solid(Color::rgba(200, 0, 0)), veil()}),
            "a floor whose bottom layer is not solid keeps every layer as an image layer");
    const MaterialDeclaration eight = images_of(std::vector<MaterialFillLayer>(8U, veil()));
    fields[at(MaterialField::fills)] = &eight;
    assemble_material(graded, fields, Size{20.0, 40.0}, assembled);
    require(assembled.fills.size() == 9U && !valid_surface_material(assembled),
            "a colour under eight image layers is more than a material holds");

    // Blend modes are in CSS's order, top layer first, repeated.
    SurfaceMaterial layered{};
    layered.fills = {MaterialFillLayer::solid(Color::rgba(9, 9, 9)), veil(), veil(), veil()};
    MaterialDeclaration blends{};
    blends.field = MaterialField::fill_blend_modes;
    blends.blend_modes = {MaterialBlendMode::multiply, MaterialBlendMode::screen};
    fields = no_fields();
    fields[at(MaterialField::fill_blend_modes)] = &blends;
    assemble_material(layered, fields, Size{20.0, 40.0}, assembled);
    require(assembled.fills[0].blend == MaterialBlendMode::normal && assembled.fills[3].blend == MaterialBlendMode::multiply &&
                assembled.fills[2].blend == MaterialBlendMode::screen &&
                assembled.fills[1].blend == MaterialBlendMode::multiply,
            "the colour layer keeps normal; the image layers take the modes from the top, repeated");
}

// -- the engine ------------------------------------------------------------------------

class RasterWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        last = raster;
        rasters = rasters + 1U;
    }
    std::shared_ptr<const PartRaster> last{};
    std::size_t rasters{};
};

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = family;
        part.bounds = Rect{20.0, 20.0, 40.0, 24.0};
        if (keyed) part.key = PartKey{"face", {}, {}};
        if (!enabled()) part.states.insert(AnatomyState::disabled);
        return part;
    }
    ControlVisualRole family{ControlVisualRole::button};
    bool keyed{true};
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
    RasterWatch painter{};
};

// What a fixture's theme is: the professional definition with `rules`, and
// with the button recipes' materials replaced by `button_fills` and
// `button_radius` when set (the C++ equivalent of the fields).
struct FixtureTheme final {
    std::vector<ThemeRule> rules{};
    std::optional<std::vector<MaterialFillLayer>> button_fills{};
    std::optional<double> button_radius{};
    ControlVisualRole family{ControlVisualRole::button};
    bool keyed{true};
};

void replace_recipe(ControlVisualRecipe& recipe, const FixtureTheme& theme) {
    if (theme.button_fills) recipe.material.fills = *theme.button_fills;
    if (theme.button_radius) {
        recipe.material.corner_radius = *theme.button_radius;
        recipe.material.corner_radii.reset();
    }
}

std::unique_ptr<Fixture> make_fixture(const FixtureTheme& theme) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.pad = make_control<Pad>(StableId("pad"));
    (*made.pad).family = theme.family;
    (*made.pad).keyed = theme.keyed;
    (*made.pad).set_requested_bounds(Rect{0.0, 0.0, 100.0, 84.0});
    (*made.root).add_child(made.pad);
    made.window = std::make_unique<Window>(made.root, Size{140.0, 124.0});
    Window& window = *made.window;
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "engine-authored-material-test";
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& recipe : button.ordinary) replace_recipe(recipe, theme);
    for (ControlVisualRecipe& recipe : button.selected) replace_recipe(recipe, theme);
    definition.rules.rules = theme.rules;
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the fixture theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

void repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

std::uint64_t bakes(const Window& window) {
    const RasterCacheStatistics statistics = window.engine_parts().cache_statistics();
    const std::uint64_t count = statistics.storage_reuses + statistics.storage_allocations + statistics.uncached;
    return count;
}

std::uint64_t assemblies(const Window& window) {
    const std::uint64_t count = window.metrics_snapshot().engine_material_assemblies;
    return count;
}

std::uint64_t pixels_hash(const Fixture& fixture) {
    require(fixture.painter.last != nullptr, "the face is drawn by the material primitive");
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : (*fixture.painter.last).pixels()) {
        hash = hash ^ byte;
        hash = hash * 1099511628211ULL;
    }
    return hash;
}

ThemeRule material_rule(std::optional<std::string_view> recipe) {
    ThemeRule rule = part_rule("pad", "pad", "face");
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    if (recipe) rule.declarations.push_back(RuleDeclaration::choice_value("recipe", *recipe));
    return rule;
}

FixtureTheme authored_theme() {
    FixtureTheme theme{};
    theme.rules.push_back(material_rule("button"));
    // The fields come from a second, general rule: they cascade from any
    // matching rule, whatever primitive it selects.
    ThemeRule fields = part_rule("faces", {}, "face");
    fields.materials.push_back(fills_of(Color::rgba(40, 120, 200)));
    MaterialDeclaration no_images{};
    no_images.field = MaterialField::fills;
    fields.materials.push_back(no_images);
    push_corners(fields, 6.0);
    theme.rules.push_back(fields);
    return theme;
}

FixtureTheme written_theme() {
    FixtureTheme theme{};
    theme.rules.push_back(material_rule("button"));
    theme.button_fills = std::vector<MaterialFillLayer>{MaterialFillLayer::solid(Color::rgba(40, 120, 200))};
    theme.button_radius = 6.0;
    return theme;
}

void test_engine_equivalence() {
    std::unique_ptr<Fixture> authored = make_fixture(authored_theme());
    std::unique_ptr<Fixture> written = make_fixture(written_theme());
    std::unique_ptr<Fixture> plain = make_fixture(FixtureTheme{std::vector<ThemeRule>{material_rule("button")}});
    require(pixels_hash(*authored) == pixels_hash(*written),
            "the fields bake as the same material written into the recipe in C++");
    require(pixels_hash(*authored) != pixels_hash(*plain), "and the fields change the part");
    require(assemblies(*(*authored).window) == 1U && assemblies(*(*written).window) == 0U,
            "the authored part assembled its material once; the written one assembles none");

    Window& window = *(*authored).window;
    const std::uint64_t first_bakes = bakes(window);
    (*(*authored).pad).invalidate(Dirty::paint);
    repaint(*authored);
    require(assemblies(window) == 1U && bakes(window) == first_bakes,
            "a repaint reuses the assembly and the raster");
    (*(*authored).pad).set_enabled(false);
    (*(*written).pad).set_enabled(false);
    repaint(*authored);
    repaint(*written);
    require(assemblies(window) == 2U && bakes(window) == first_bakes + 1U,
            "a state whose recipe material differs is one more assembly and one more bake");
    require(pixels_hash(*authored) == pixels_hash(*written), "and still bakes as the C++ material");
    (*(*authored).pad).invalidate(Dirty::paint);
    repaint(*authored);
    require(assemblies(window) == 2U && bakes(window) == first_bakes + 1U, "which is kept in turn");
}

// The colour in one rule and the image layers in another, the colour's rule
// less specific (`colour_general`) or more: both compose as CSS has them,
// and bake as the same layers written into the recipe in C++.
FixtureTheme split_fill_theme(bool colour_general) {
    FixtureTheme theme{};
    theme.rules.push_back(material_rule("button"));
    ThemeRule general = part_rule("general", {}, "face");
    ThemeRule specific = part_rule("specific", "pad", "face");
    const MaterialDeclaration colour = fills_of(Color::rgba(40, 120, 200));
    const MaterialDeclaration images = images_of({veil()});
    (colour_general ? general : specific).materials.push_back(colour);
    (colour_general ? specific : general).materials.push_back(images);
    theme.rules.push_back(general);
    theme.rules.push_back(specific);
    return theme;
}

void test_split_fills() {
    FixtureTheme written{};
    written.rules.push_back(material_rule("button"));
    written.button_fills = std::vector<MaterialFillLayer>{MaterialFillLayer::solid(Color::rgba(40, 120, 200)), veil()};
    std::unique_ptr<Fixture> expected = make_fixture(written);
    std::unique_ptr<Fixture> colour_general = make_fixture(split_fill_theme(true));
    std::unique_ptr<Fixture> colour_specific = make_fixture(split_fill_theme(false));
    require(pixels_hash(*colour_general) == pixels_hash(*expected),
            "a general colour goes under a specific rule's image layers");
    require(pixels_hash(*colour_specific) == pixels_hash(*expected),
            "a specific colour goes under a general rule's image layers");
}

void test_unkeyed_part() {
    // A part without a key has no part name to select: the rules select
    // its role.
    FixtureTheme theme = authored_theme();
    theme.keyed = false;
    for (ThemeRule& rule : theme.rules) {
        rule.selector.part.clear();
        rule.selector.role = PartRole::face;
    }
    std::unique_ptr<Fixture> fixture = make_fixture(theme);
    Window& window = *(*fixture).window;
    require(assemblies(window) == 1U, "a part without a key assembles its material for its paint");
    (*(*fixture).pad).invalidate(Dirty::paint);
    repaint(*fixture);
    require(assemblies(window) == 2U, "and again at every paint, having no record to keep it in");
    std::unique_ptr<Fixture> written = make_fixture(written_theme());
    require(pixels_hash(*fixture) == pixels_hash(*written), "it bakes the same material");
}

void test_family_default() {
    FixtureTheme family{};
    family.family = ControlVisualRole::menu_item;
    family.rules.push_back(material_rule(std::nullopt));
    FixtureTheme named = family;
    named.rules[0] = material_rule("menu_item");
    FixtureTheme button = family;
    button.rules[0] = material_rule("button");
    std::unique_ptr<Fixture> by_family = make_fixture(family);
    std::unique_ptr<Fixture> by_name = make_fixture(named);
    std::unique_ptr<Fixture> by_button = make_fixture(button);
    require(pixels_hash(*by_family) == pixels_hash(*by_name),
            "a material rule naming no recipe draws the part's own family's");
    require(pixels_hash(*by_family) != pixels_hash(*by_button), "not the button's, as before engine 0.3");
}

void test_refused_assembly() {
    // Eight keylines and a finish are each a material's, but a finish needs
    // room for one more keyline: the assembly is no material, and the part
    // is drawn by the built-in recipe, the refusal counted once.
    FixtureTheme theme{};
    theme.rules.push_back(material_rule("button"));
    ThemeRule lines = part_rule("lines", "pad", "face");
    MaterialDeclaration keylines{};
    keylines.field = MaterialField::keylines;
    keylines.keylines.assign(SurfaceMaterial::maximum_keylines, MaterialKeyline{});
    lines.materials.push_back(keylines);
    ThemeRule finish = part_rule("finish", {}, "face");
    finish.materials.push_back(number_of(MaterialField::finish, 0.5));
    theme.rules.push_back(lines);
    theme.rules.push_back(finish);
    std::unique_ptr<Fixture> fixture = make_fixture(theme);
    Window& window = *(*fixture).window;
    require((*fixture).painter.rasters == 0U, "a refused assembly composites no raster");
    require(window.metrics_snapshot().engine_primitive_refusals == 1U, "the refusal is counted");
    (*(*fixture).pad).invalidate(Dirty::paint);
    repaint(*fixture);
    require(window.metrics_snapshot().engine_primitive_refusals == 1U && assemblies(window) == 1U,
            "once, when it begins, with the assembly kept");
}

} // namespace

int main() {
    test_field_names();
    test_compile();
    test_cascade();
    test_serials();
    test_assembly();
    test_fill_composition();
    test_engine_equivalence();
    test_split_fills();
    test_unkeyed_part();
    test_family_default();
    test_refused_assembly();
    std::cout << "gui_forms_engine_authored_material_tests: passed\n";
    return 0;
}
