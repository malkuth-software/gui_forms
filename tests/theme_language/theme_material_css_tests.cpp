// CSS's material properties (stage T3, phase P6b): background-color,
// background-image with the six gradient functions, the `background`
// shorthand, background-blend-mode, the border family, border-radius and
// box-shadow lower onto the engine's material fields, and a rule of fields
// alone selects the material primitive (owner C7). The authored gradients resolve as CSS places them: stop fixup, sides
// and corners, the radial extents, repeating periods, lengths along the
// gradient. Gate: for each form, a sheet and the same material written in
// C++ bake identical pixels.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/authored_gradient/authored_gradient.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/resources/types/image_resource_types.hpp"
#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {

using namespace gui_forms;
using theme_language::CompileOutcome;
using theme_language::DiagnosticCode;
using theme_language::SelectorVocabulary;
using theme_language::ThemeCompileInput;
using theme_language::ThemeCompiler;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeImageResource;
using theme_language::ThemeSource;
using theme_language::diagnostic_code_name;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr double pi = 3.14159265358979323846264338327950288;

const Color red = Color::rgba(255, 0, 0);
const Color blue = Color::rgba(0, 0, 255);
const Color green = Color::rgba(0, 128, 0);
const Color black = Color::rgba(0, 0, 0);
const Color transparent = Color::rgba(0, 0, 0, 0);

AuthoredGradientStop stop(Color colour) {
    AuthoredGradientStop made{};
    made.colour = colour;
    return made;
}

AuthoredGradientStop stop_at(Color colour, double fraction, double units = 0.0) {
    AuthoredGradientStop made{};
    made.colour = colour;
    made.positioned = true;
    made.position = BoxLength{fraction, units};
    return made;
}

AuthoredGradient gradient_of(MaterialFillKind kind, std::vector<AuthoredGradientStop> stops) {
    AuthoredGradient gradient{};
    gradient.kind = kind;
    gradient.stops = std::move(stops);
    return gradient;
}

MaterialFillLayer resolved(const AuthoredGradient& gradient, Size box) {
    require(valid_authored_gradient(gradient), "the gradient is valid");
    MaterialFillLayer layer{};
    resolve_authored_gradient(gradient, box, layer);
    return layer;
}

std::vector<GradientStop> stops_of(std::initializer_list<GradientStop> stops) {
    std::vector<GradientStop> made(stops);
    return made;
}

constexpr double separation = 1.0 / 1048576.0;

// -- the resolution ----------------------------------------------------------------

void test_placement() {
    AuthoredGradient linear = gradient_of(MaterialFillKind::linear_gradient, {stop(red), stop(green), stop(blue)});
    linear.angle_degrees = 135.0;
    require(authored_gradient_fixed(linear), "a gradient of angles and fractions is fixed");
    require(resolved(linear, Size{40.0, 24.0}) ==
                MaterialFillLayer::linear_css_angle(135.0, stops_of({{0.0, red}, {0.5, green}, {1.0, blue}})),
            "unplaced stops spread evenly, as the CSS angle form C++ writes");

    linear.stops = {stop_at(red, 0.2), stop(green), stop_at(blue, 0.8)};
    require(resolved(linear, Size{}).stops ==
                stops_of({{0.0, red}, {0.2, red}, {0.5, green}, {0.8, blue}, {1.0, blue}}),
            "stops short of the ends are padded with their colours");

    linear.stops = {stop_at(red, 0.5), stop_at(blue, 0.2)};
    require(resolved(linear, Size{}).stops ==
                stops_of({{0.0, red}, {0.5, red}, {0.5 + separation, blue}, {1.0, blue}}),
            "a stop before an earlier one moves to it: a hard stop, separated by 2^-20");

    linear.stops = {stop_at(red, -0.5), stop_at(blue, 1.5)};
    const MaterialFillLayer clipped = resolved(linear, Size{});
    require(clipped.stops.size() == 2U && clipped.stops[0].color == Color::rgba(191, 0, 64) &&
                clipped.stops[1].color == Color::rgba(64, 0, 191),
            "stops past the ends are clipped to the colours at the ends");

    linear.stops = {stop(red)};
    require(resolved(linear, Size{}) == MaterialFillLayer::solid(red), "a single stop is a solid");
}

void test_linear_geometry() {
    AuthoredGradient corner = gradient_of(MaterialFillKind::linear_gradient, {stop(red), stop(blue)});
    corner.corner = GradientCorner::top_right;
    require(!authored_gradient_fixed(corner), "a corner reads the box");
    const double diagonal = engine_arctangent2(24.0, 40.0) * (180.0 / pi);
    require(resolved(corner, Size{40.0, 24.0}).angle_degrees == diagonal, "to top right: the diagonal's angle");
    corner.corner = GradientCorner::bottom_left;
    require(resolved(corner, Size{40.0, 24.0}).angle_degrees == 180.0 + diagonal, "to bottom left: opposite");

    AuthoredGradient lengths = gradient_of(MaterialFillKind::linear_gradient,
                                           {stop_at(red, 0.0, 6.0), stop_at(blue, 0.0, 18.0)});
    lengths.angle_degrees = 0.0;
    require(resolved(lengths, Size{40.0, 24.0}).stops ==
                stops_of({{0.0, red}, {0.25, red}, {0.75, blue}, {1.0, blue}}),
            "lengths are fractions of the box's gradient line");

    AuthoredGradient repeating = lengths;
    repeating.repeating = true;
    repeating.stops = {stop_at(red, 0.0, 0.0), stop_at(blue, 0.0, 6.0)};
    require(resolved(repeating, Size{40.0, 24.0}) ==
                MaterialFillLayer::repeating_linear(Point{20.0, 24.0}, Point{20.0, 18.0},
                                                    stops_of({{0.0, red}, {1.0, blue}}),
                                                    MaterialCoordinateSpace::logical),
            "a repeating period narrower than the line is the line's span in logical units");
    repeating.stops = {stop(red), stop(blue)};
    require(authored_gradient_fixed(repeating) &&
                resolved(repeating, Size{}) ==
                    MaterialFillLayer::linear_css_angle(0.0, stops_of({{0.0, red}, {1.0, blue}}),
                                                        GradientSpreadMode::repeat),
            "a repeating gradient over the whole line keeps its CSS angle");
    repeating.stops = {stop_at(red, 0.5), stop_at(blue, 0.5)};
    require(resolved(repeating, Size{40.0, 24.0}) == MaterialFillLayer::solid(Color::rgba(128, 0, 128)),
            "a repeating gradient of no length is CSS's average colour");
}

void test_radial_geometry() {
    AuthoredGradient ellipse = gradient_of(MaterialFillKind::radial_gradient, {stop(red), stop(blue)});
    require(authored_gradient_fixed(ellipse), "an ellipse of keywords is fixed");
    const double corner = 0.5 * 1.41421356237309504880168872420969808;
    require(resolved(ellipse, Size{}) ==
                MaterialFillLayer::radial(Point{0.5, 0.5}, Size{corner, corner}, stops_of({{0.0, red}, {1.0, blue}})),
            "CSS's default ellipse reaches the corners, in normalized coordinates");
    ellipse.extent = RadialExtent::closest_side;
    ellipse.centre_x = BoxLength{0.25, 0.0};
    require(resolved(ellipse, Size{}).radii == (Size{0.25, 0.5}), "closest-side, each axis on its own");

    AuthoredGradient circle = ellipse;
    circle.circle = true;
    require(!authored_gradient_fixed(circle), "a circle reads the box");
    require(resolved(circle, Size{40.0, 24.0}) ==
                MaterialFillLayer::radial(Point{10.0, 12.0}, Size{10.0, 10.0}, stops_of({{0.0, red}, {1.0, blue}}),
                                          MaterialCoordinateSpace::logical),
            "a closest-side circle in logical units");

    AuthoredGradient beyond = gradient_of(MaterialFillKind::radial_gradient, {stop(red), stop_at(blue, 2.0)});
    const MaterialFillLayer far = resolved(beyond, Size{});
    require(far.radii == (Size{corner * 2.0, corner * 2.0}) && far.stops == stops_of({{0.0, red}, {1.0, blue}}),
            "a stop past the ray widens the radii");

    AuthoredGradient rings = gradient_of(MaterialFillKind::radial_gradient,
                                         {stop_at(red, 0.0, 25.0), stop_at(blue, 0.0, 75.0)});
    rings.repeating = true;
    rings.circle = true;
    rings.extent = RadialExtent::lengths;
    rings.radius_x = BoxLength{0.0, 100.0};
    const MaterialFillLayer ringed = resolved(rings, Size{40.0, 24.0});
    const Color middle = Color::rgba(128, 0, 128);
    require(ringed.spread == GradientSpreadMode::repeat && ringed.radii == (Size{50.0, 50.0}) &&
                ringed.stops == stops_of({{0.0, middle}, {0.5, blue}, {0.5 + separation, red}, {1.0, middle}}),
            "a repeating period that begins off the centre is rotated to begin there");

    // Reflected off the centre: the period and its mirror image repeat as
    // one pair of twice the radii, rotated as a repeating period is.
    AuthoredGradient mirrored = rings;
    mirrored.reflecting = true;
    const MaterialFillLayer reflected = resolved(mirrored, Size{40.0, 24.0});
    require(reflected.spread == GradientSpreadMode::repeat && reflected.radii == (Size{100.0, 100.0}) &&
                reflected.stops ==
                    stops_of({{0.0, middle}, {0.25, red}, {0.25 + separation, red}, {0.75, blue}, {1.0, middle}}),
            "a reflecting period that begins off the centre repeats with its mirror image");
    AuthoredGradient centred = rings;
    centred.reflecting = true;
    centred.stops = {stop_at(red, 0.0, 0.0), stop_at(blue, 0.0, 50.0)};
    const MaterialFillLayer reflecting = resolved(centred, Size{40.0, 24.0});
    require(reflecting.spread == GradientSpreadMode::reflect && reflecting.radii == (Size{50.0, 50.0}) &&
                reflecting.stops == stops_of({{0.0, red}, {1.0, blue}}),
            "a reflecting period that begins at the centre reflects");
    AuthoredGradient unrepeated = centred;
    unrepeated.repeating = false;
    require(!valid_authored_gradient(unrepeated), "a reflecting gradient repeats");
    AuthoredGradient crowded = centred;
    crowded.stops.assign(maximum_reflecting_radial_stops + 1U, stop(red));
    require(!valid_authored_gradient(crowded), "a reflecting radial gradient's stops fit twice over");

    AuthoredGradient wrong = circle;
    wrong.extent = RadialExtent::lengths;
    wrong.radius_x = BoxLength{0.5, 0.0};
    require(!valid_authored_gradient(wrong), "a circle's radius is a length, not a percentage");
}

void test_conic_geometry() {
    AuthoredGradient conic = gradient_of(MaterialFillKind::conic_gradient, {stop(red), stop(blue)});
    conic.angle_degrees = 90.0;
    require(resolved(conic, Size{}) ==
                MaterialFillLayer::conic(Point{0.5, 0.5}, 90.0, stops_of({{0.0, red}, {1.0, blue}})),
            "a conic gradient from 90 degrees");
    conic.repeating = true;
    conic.stops = {stop_at(red, 0.0), stop_at(blue, 0.25)};
    require(resolved(conic, Size{}) ==
                MaterialFillLayer::repeating_conic(Point{0.5, 0.5}, 90.0, 90.0, stops_of({{0.0, red}, {1.0, blue}})),
            "a repeating conic gradient's period is its stops' span");
}

// -- the compilation ---------------------------------------------------------------

constexpr std::array<PartDeclaration, 1> pad_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = pad_parts, .states = {}, .custom_states = {}, .item_systems = {}},
}};
// icon.check is listed but bound to no image; the others are images.
const std::array<std::string_view, 3> resources{{"icon.check", "mask.dots", "tile.quadrant"}};

// A binding for sheets that only compile: no window draws the image.
const std::array<ThemeImageResource, 1> listed_images{{
    {.resource = "tile.quadrant", .image = ImageId{1U}, .pixel_size = Size{4.0, 4.0}, .pixels_per_logical_pixel = 1.0},
}};

struct Compilation final {
    CompileOutcome outcome{CompileOutcome::refused};
    ThemeRuleSheet sheet{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

Compilation compile(std::string_view text, std::span<const ThemeImageResource> images = listed_images) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = text;
    ThemeCompileInput input{.source = source,
                            .registry = registry,
                            .vocabulary = SelectorVocabulary{},
                            .published_values = {},
                            .resources = resources,
                            .images = images,
                            .requires_engine = current_engine_version};
    input.vocabulary.controls = controls;
    ThemeCompiler compiler{};
    ThemeDiagnosticList diagnostics{};
    Compilation result{};
    result.outcome = compiler.compile(input, result.sheet, diagnostics);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    if (result.outcome == CompileOutcome::compiled) {
        const CompiledThemeRules engine = CompiledThemeRules::compile(result.sheet, registry);
        require(engine.diagnostics().empty(), std::string("the engine accepts the sheet: ") + std::string(text));
    }
    return result;
}

const ThemeRule& only_rule(const Compilation& compilation, std::string_view sheet) {
    require(compilation.outcome == CompileOutcome::compiled && compilation.sheet.rules.size() == 1U,
            std::string("one rule: ") + std::string(sheet));
    static const ThemeRule none{};
    if (compilation.sheet.rules.size() != 1U) return none;
    return compilation.sheet.rules[0];
}

const ThemeRule& only_rule(Compilation&& compilation, std::string_view sheet) = delete;

const MaterialDeclaration* field_of(const ThemeRule& rule, MaterialField field) {
    for (const MaterialDeclaration& declaration : rule.materials) {
        if (declaration.field == field) return &declaration;
    }
    return nullptr;
}
const MaterialDeclaration* field_of(ThemeRule&& rule, MaterialField field) = delete;

const RecipeDeclaration* recipe_field_of(const ThemeRule& rule, RecipeField field) {
    for (const RecipeDeclaration& declaration : rule.recipe_fields) {
        if (declaration.field == field) return &declaration;
    }
    return nullptr;
}

const RecipeDeclaration* recipe_field_of(ThemeRule&& rule, RecipeField field) = delete;

bool selects_material(const ThemeRule& rule) {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration == RuleDeclaration::primitive("material")) return true;
    }
    return false;
}

void test_lowering() {
    const std::string_view colour_sheet = "#pad::part(face) { background-color: red }";
    const Compilation colour = compile(colour_sheet);
    const ThemeRule& coloured = only_rule(colour, colour_sheet);
    const MaterialDeclaration* const background = field_of(coloured, MaterialField::background_colour);
    require(background != nullptr && (*background).colour == red, "background-color is the colour field");
    require(selects_material(coloured) && coloured.materials.size() == 1U,
            "a rule of fields alone selects the material primitive (C7)");

    const std::string_view named_sheet = "#pad::part(face) { primitive: sheen; background-color: red }";
    const Compilation named = compile(named_sheet);
    require(!selects_material(only_rule(named, named_sheet)), "a rule naming a primitive keeps it");

    const std::string_view layers_sheet =
        "#pad::part(face) { background-image: linear-gradient(red, blue), radial-gradient(circle, red, blue) }";
    const Compilation layers = compile(layers_sheet);
    const MaterialDeclaration* const images = field_of(only_rule(layers, layers_sheet), MaterialField::fills);
    require(images != nullptr && (*images).fills.size() == 2U && (*images).gradients.size() == 2U,
            "two layers, one of them read per box");
    if (images != nullptr && (*images).gradients.size() == 2U) {
        require((*images).gradients[0].kind == MaterialFillKind::radial_gradient && (*images).gradients[0].circle,
                "the layers are bottom first: CSS's last layer is the circle");
        require((*images).gradients[1].kind == MaterialFillKind::solid &&
                    (*images).fills[1] ==
                        MaterialFillLayer::linear_css_angle(180.0, stops_of({{0.0, red}, {1.0, blue}})),
                "a fixed layer is resolved when the sheet compiles");
    }

    const std::string_view shorthand_sheet = "#pad::part(face) { background: linear-gradient(red, blue) green }";
    const Compilation shorthand = compile(shorthand_sheet);
    const ThemeRule& short_rule = only_rule(shorthand, shorthand_sheet);
    const MaterialDeclaration* const short_colour = field_of(short_rule, MaterialField::background_colour);
    require(short_colour != nullptr && (*short_colour).colour == green && field_of(short_rule, MaterialField::fills),
            "background sets the colour and the images");
    const std::string_view none_sheet = "#pad::part(face) { background: none }";
    const Compilation none = compile(none_sheet);
    const MaterialDeclaration* const no_colour = field_of(only_rule(none, none_sheet), MaterialField::background_colour);
    require(no_colour != nullptr && (*no_colour).colour.alpha == 0U, "background: none is transparent");

    const std::string_view later_sheet = "#pad::part(face) { background: red; background-color: blue }";
    const Compilation later = compile(later_sheet);
    const MaterialDeclaration* const later_colour =
        field_of(only_rule(later, later_sheet), MaterialField::background_colour);
    require(later_colour != nullptr && (*later_colour).colour == blue, "a later longhand replaces the shorthand's");
    const std::string_view earlier_sheet = "#pad::part(face) { background-color: blue; background: red }";
    const Compilation earlier = compile(earlier_sheet);
    const MaterialDeclaration* const earlier_colour =
        field_of(only_rule(earlier, earlier_sheet), MaterialField::background_colour);
    require(earlier_colour != nullptr && (*earlier_colour).colour == red, "and a later shorthand the longhand's");

    const std::string_view blend_sheet = "#pad::part(face) { background-blend-mode: multiply, screen }";
    const Compilation blend = compile(blend_sheet);
    const MaterialDeclaration* const modes = field_of(only_rule(blend, blend_sheet), MaterialField::fill_blend_modes);
    require(modes != nullptr &&
                (*modes).blend_modes == (std::vector<MaterialBlendMode>{MaterialBlendMode::multiply,
                                                                        MaterialBlendMode::screen}),
            "background-blend-mode keeps CSS's order");
    const std::string_view separable_sheet =
        "#pad::part(face) { background-blend-mode: overlay, color-burn, Soft-Light, exclusion }";
    const Compilation separable = compile(separable_sheet);
    const MaterialDeclaration* const separable_modes =
        field_of(only_rule(separable, separable_sheet), MaterialField::fill_blend_modes);
    require(separable_modes != nullptr &&
                (*separable_modes).blend_modes ==
                    (std::vector<MaterialBlendMode>{MaterialBlendMode::overlay, MaterialBlendMode::color_burn,
                                                    MaterialBlendMode::soft_light, MaterialBlendMode::exclusion}),
            "the separable modes take their CSS names, in any case");

    const std::string_view initial_sheet = "#pad::part(face) { background: initial }";
    const Compilation initial = compile(initial_sheet);
    const ThemeRule& reset = only_rule(initial, initial_sheet);
    const MaterialDeclaration* const reset_colour = field_of(reset, MaterialField::background_colour);
    const MaterialDeclaration* const reset_images = field_of(reset, MaterialField::fills);
    require(reset_colour != nullptr && (*reset_colour).initial && reset_images != nullptr && (*reset_images).initial,
            "initial resets each field the shorthand sets");
}

void test_border_lowering() {
    const std::string_view border_sheet = "#pad::part(face) { border: 2px solid red }";
    const Compilation border = compile(border_sheet);
    const ThemeRule& bordered = only_rule(border, border_sheet);
    require(bordered.materials.size() == 12U, "border sets the twelve longhands");
    const MaterialDeclaration* const left_colour = field_of(bordered, MaterialField::border_left_colour);
    const MaterialDeclaration* const top_width = field_of(bordered, MaterialField::border_top_width);
    require(left_colour != nullptr && (*left_colour).colour == red && top_width != nullptr &&
                (*top_width).number == 2.0,
            "each edge takes the width and the colour");

    const std::string_view style_sheet = "#pad::part(face) { border: dashed }";
    const Compilation style = compile(style_sheet);
    const MaterialDeclaration* const omitted = field_of(only_rule(style, style_sheet), MaterialField::border_top_width);
    const MaterialDeclaration* const dash =
        field_of(only_rule(style, style_sheet), MaterialField::border_bottom_style);
    require(omitted != nullptr && (*omitted).initial && dash != nullptr &&
                (*dash).border_style == stxlib::Optional<MaterialBorderStyle>::of(MaterialBorderStyle::dashed),
            "a part the shorthand omits keeps the floor's");

    const std::string_view colour_sheet = "#pad::part(face) { border-color: red green blue }";
    const Compilation colours = compile(colour_sheet);
    const ThemeRule& coloured = only_rule(colours, colour_sheet);
    const MaterialDeclaration* const right = field_of(coloured, MaterialField::border_right_colour);
    const MaterialDeclaration* const left = field_of(coloured, MaterialField::border_left_colour);
    const MaterialDeclaration* const bottom = field_of(coloured, MaterialField::border_bottom_colour);
    require(right != nullptr && left != nullptr && bottom != nullptr && (*right).colour == green &&
                (*left).colour == green && (*bottom).colour == blue,
            "three values: top, the sides, bottom");

    const std::string_view none_sheet = "#pad::part(face) { border-left-style: none; border-right-width: thick }";
    const Compilation none = compile(none_sheet);
    const MaterialDeclaration* const no_line = field_of(only_rule(none, none_sheet), MaterialField::border_left_style);
    const MaterialDeclaration* const thick = field_of(only_rule(none, none_sheet), MaterialField::border_right_width);
    require(no_line != nullptr && (*no_line).border_style.absent() && thick != nullptr && (*thick).number == 5.0,
            "none is no line; thick is 5px");

    const std::string_view radius_sheet = "#pad::part(face) { border-radius: 4px 50%; border-top-left-radius: 3px }";
    const Compilation radius = compile(radius_sheet);
    const ThemeRule& rounded = only_rule(radius, radius_sheet);
    const MaterialDeclaration* const top_left = field_of(rounded, MaterialField::corner_top_left);
    const MaterialDeclaration* const top_right = field_of(rounded, MaterialField::corner_top_right);
    const MaterialDeclaration* const bottom_right = field_of(rounded, MaterialField::corner_bottom_right);
    require(top_left != nullptr && (*top_left).number == 3.0 && !(*top_left).relative && top_right != nullptr &&
                (*top_right).relative && (*top_right).number == 0.5 && bottom_right != nullptr &&
                (*bottom_right).number == 4.0,
            "border-radius sets the corners, a percentage relative, and a later corner replaces its own");

    const std::string_view shadow_sheet = "#pad::part(face) { box-shadow: 0 2px 4px red, inset 1px 1px blue }";
    const Compilation shadow = compile(shadow_sheet);
    const MaterialDeclaration* const shadows = field_of(only_rule(shadow, shadow_sheet), MaterialField::shadows);
    require(shadows != nullptr && (*shadows).shadows.size() == 2U, "two shadows");
    if (shadows != nullptr && (*shadows).shadows.size() == 2U) {
        require((*shadows).shadows[0].inset && (*shadows).shadows[0].color == blue &&
                    (*shadows).shadows[1].blur_radius == 4.0 && (*shadows).shadows[1].offset == (Point{0.0, 2.0}),
                "CSS's first shadow is on top: the list is bottom first");
    }

    const std::string_view reset_sheet = "#pad::part(face) { border: initial }";
    const Compilation reset = compile(reset_sheet);
    const ThemeRule& reset_rule = only_rule(reset, reset_sheet);
    bool all_initial = reset_rule.materials.size() == 12U;
    for (const MaterialDeclaration& declaration : reset_rule.materials) all_initial = all_initial && declaration.initial;
    require(all_initial, "border: initial resets the twelve longhands");
}

void test_line_lowering() {
    const std::string_view sheet = "#pad::part(face) { border-style: double groove }";
    const Compilation compiled = compile(sheet);
    const ThemeRule& rule = only_rule(compiled, sheet);
    const MaterialDeclaration* const top = field_of(rule, MaterialField::border_top_style);
    const MaterialDeclaration* const right = field_of(rule, MaterialField::border_right_style);
    require(top != nullptr && (*top).border_style == stxlib::Optional<MaterialBorderStyle>::of(MaterialBorderStyle::solid) &&
                (*top).edge_lines == MaterialEdgeLines::double_line && right != nullptr &&
                (*right).edge_lines == MaterialEdgeLines::groove,
            "double and groove are solid edges drawn as two lines");
    const std::string_view image_sheet =
        "#pad::part(face) { border-image: url(\"resource:tile.quadrant\") 25% fill / auto }";
    const Compilation framed = compile(image_sheet);
    const MaterialDeclaration* const image = field_of(only_rule(framed, image_sheet), MaterialField::border_image);
    require(image != nullptr && (*image).border_image &&
                *(*image).border_image ==
                    MaterialFillLayer::nine_patch(ImageId{1U}, Size{4.0, 4.0}, Insets{1.0, 1.0, 1.0, 1.0}, 1.0),
            "a border image's percentage slices are of the image, auto widths its own size");
}

// The recipe's own fields (P6f): `color`, the outline family and
// `text-shadow` set the control's recipe, not its material.
void test_recipe_lowering() {
    const std::string_view colour_sheet = "#pad { color: red; background-color: blue }";
    const Compilation coloured = compile(colour_sheet);
    const ThemeRule& colour_rule = only_rule(coloured, colour_sheet);
    const RecipeDeclaration* const text = recipe_field_of(colour_rule, RecipeField::text);
    require(text != nullptr && (*text).colour == red && colour_rule.recipe_fields.size() == 1U &&
                field_of(colour_rule, MaterialField::background_colour) != nullptr && selects_material(colour_rule),
            "color is the recipe's text colour, beside the material fields");
    const std::string_view text_sheet = "#pad { color: red }";
    const Compilation text_only = compile(text_sheet);
    require(!selects_material(only_rule(text_only, text_sheet)), "a rule of recipe fields alone selects no primitive");

    const std::string_view outline_sheet = "#pad { outline: 2px solid green; outline-offset: -1px }";
    const Compilation outlined = compile(outline_sheet);
    const ThemeRule& outline_rule = only_rule(outlined, outline_sheet);
    const RecipeDeclaration* const style = recipe_field_of(outline_rule, RecipeField::outline_style);
    const RecipeDeclaration* const colour = recipe_field_of(outline_rule, RecipeField::outline_colour);
    const RecipeDeclaration* const width = recipe_field_of(outline_rule, RecipeField::outline_width);
    const RecipeDeclaration* const offset = recipe_field_of(outline_rule, RecipeField::outline_offset);
    require(style != nullptr && !(*style).initial && (*style).outline_style == RecipeOutlineStyle::solid &&
                colour != nullptr && (*colour).colour == green && width != nullptr && (*width).number == 2.0 &&
                offset != nullptr && (*offset).number == -1.0,
            "outline's colour, style and width, and the offset");
    const std::string_view ring_sheet = "#pad { outline: auto }";
    const Compilation ringed = compile(ring_sheet);
    const ThemeRule& ring_rule = only_rule(ringed, ring_sheet);
    const RecipeDeclaration* const ring = recipe_field_of(ring_rule, RecipeField::outline_style);
    const RecipeDeclaration* const ring_colour = recipe_field_of(ring_rule, RecipeField::outline_colour);
    const RecipeDeclaration* const ring_width = recipe_field_of(ring_rule, RecipeField::outline_width);
    require(ring != nullptr && (*ring).outline_style == RecipeOutlineStyle::ring && ring_colour != nullptr &&
                (*ring_colour).initial && ring_width != nullptr && (*ring_width).initial &&
                recipe_field_of(ring_rule, RecipeField::outline_offset) == nullptr,
            "auto is the theme's ring; the shorthand's omitted members are initial, and the offset not its");
    const std::string_view none_sheet = "#pad { outline: thick none }";
    const Compilation none = compile(none_sheet);
    const ThemeRule& none_rule = only_rule(none, none_sheet);
    const RecipeDeclaration* const no_style = recipe_field_of(none_rule, RecipeField::outline_style);
    const RecipeDeclaration* const thick = recipe_field_of(none_rule, RecipeField::outline_width);
    require(no_style != nullptr && (*no_style).outline_style == RecipeOutlineStyle::none && thick != nullptr &&
                (*thick).number == 5.0,
            "none and CSS's thick");
    const std::string_view dotted_sheet = "#pad { outline: 1px dotted green }";
    const Compilation dotted = compile(dotted_sheet);
    const RecipeDeclaration* const dots =
        recipe_field_of(only_rule(dotted, dotted_sheet), RecipeField::outline_style);
    require(dots != nullptr && (*dots).outline_style == RecipeOutlineStyle::dotted, "a dotted outline");

    const std::string_view shadow_sheet = "#pad { text-shadow: blue 1px 2px 3px }";
    const Compilation shadowed = compile(shadow_sheet);
    const RecipeDeclaration* const shadow = recipe_field_of(only_rule(shadowed, shadow_sheet), RecipeField::text_shadow);
    require(shadow != nullptr && (*shadow).shadowed && (*shadow).shadow_offset == Point{1.0, 2.0} &&
                (*shadow).shadow_blur == 3.0 && (*shadow).colour == blue,
            "a text shadow's offset, blur and colour");
    const std::string_view no_shadow_sheet = "#pad { text-shadow: none }";
    const Compilation unshadowed = compile(no_shadow_sheet);
    const RecipeDeclaration* const no_shadow =
        recipe_field_of(only_rule(unshadowed, no_shadow_sheet), RecipeField::text_shadow);
    require(no_shadow != nullptr && !(*no_shadow).shadowed && !(*no_shadow).initial, "text-shadow: none");

    const std::string_view initial_sheet = "#pad { outline: initial; color: initial }";
    const Compilation reset = compile(initial_sheet);
    const ThemeRule& reset_rule = only_rule(reset, initial_sheet);
    bool all_initial = reset_rule.recipe_fields.size() == 4U;
    for (const RecipeDeclaration& declaration : reset_rule.recipe_fields) {
        if (!declaration.initial) all_initial = false;
    }
    require(all_initial, "initial resets each field the property sets");

    const Compilation on_part = compile("#pad::part(face) { outline-width: 2px }");
    require(on_part.outcome == CompileOutcome::compiled && on_part.sheet.rules.empty() &&
                on_part.diagnostics.size() == 1U &&
                on_part.diagnostics[0].code == DiagnosticCode::property_not_admitted,
            "a recipe field on a part is dropped with a warning: the recipe's fields are the control's");
    const std::string_view part_colour_sheet = "#pad::part(face) { color: red; glyph-color: blue }";
    const Compilation part_colour = compile(part_colour_sheet);
    const ThemeRule& part_colour_rule = only_rule(part_colour, part_colour_sheet);
    require(part_colour_rule.selector.part == "face" &&
                recipe_field_of(part_colour_rule, RecipeField::text) != nullptr &&
                recipe_field_of(part_colour_rule, RecipeField::glyph) != nullptr,
            "but a named part takes its own text colours");
}

void test_filter_lowering() {
    const std::string_view none_sheet = "#pad::part(face) { filter: none }";
    const Compilation none = compile(none_sheet);
    const ThemeRule& none_rule = only_rule(none, none_sheet);
    const MaterialDeclaration* const none_colour = field_of(none_rule, MaterialField::colour_filter);
    const MaterialDeclaration* const none_drop = field_of(none_rule, MaterialField::drop_shadow);
    const MaterialDeclaration* const none_blur = field_of(none_rule, MaterialField::filter_blur);
    require(none_rule.materials.size() == 3U && none_colour != nullptr && !(*none_colour).colour_filter &&
                none_drop != nullptr && (*none_drop).drop_shadow.absent() && none_blur != nullptr &&
                (*none_blur).number == 0.0 && selects_material(none_rule),
            "filter: none clears the three filter fields");

    const std::string_view blur_sheet = "#pad::part(face) { filter: blur(1.5px) blur(2px) }";
    const Compilation blurred = compile(blur_sheet);
    const MaterialDeclaration* const blur = field_of(only_rule(blurred, blur_sheet), MaterialField::filter_blur);
    require(blur != nullptr && (*blur).number == 5.0, "two blurs combine in quadrature, filter_blur = 2 sigma");

    const std::string_view shadow_sheet = "#pad::part(face) { filter: opacity(50%) drop-shadow(0 1px red) invert(1) }";
    const Compilation shadowed = compile(shadow_sheet);
    const ThemeRule& shadow_rule = only_rule(shadowed, shadow_sheet);
    const MaterialDeclaration* const colour = field_of(shadow_rule, MaterialField::colour_filter);
    const MaterialDeclaration* const drop = field_of(shadow_rule, MaterialField::drop_shadow);
    const MaterialColourMatrix expected =
        MaterialColourMatrix::opacity(0.5).then(MaterialColourMatrix::invert(1.0));
    require(colour != nullptr && (*colour).colour_filter && *(*colour).colour_filter == expected,
            "the colour functions compose in source order");
    require(drop != nullptr && (*drop).drop_shadow.present() &&
            (*(*drop).drop_shadow.value()).offset == Point{0.0, 1.0} &&
                (*(*drop).drop_shadow.value()).blur_radius == 0.0 &&
                (*(*drop).drop_shadow.value()).color == Color::rgba(0, 255, 255),
            "a drop shadow's colour takes the colour functions after it, not those before");

    const std::string_view finish_sheet = "#pad::part(face) { surface-finish: 25% }";
    const Compilation finished = compile(finish_sheet);
    const MaterialDeclaration* const finish = field_of(only_rule(finished, finish_sheet), MaterialField::finish);
    require(finish != nullptr && (*finish).number == 0.25, "surface-finish reads a percentage");

    // gradient-dither (MaterialDither): each keyword, in any case.
    const std::array<std::string_view, 3> dither_sheets{"#pad::part(face) { gradient-dither: none }",
                                                         "#pad::part(face) { gradient-dither: ordered }",
                                                         "#pad::part(face) { gradient-dither: Blue-Noise }"};
    const std::array<MaterialDither, 3> dithers{MaterialDither::none, MaterialDither::ordered,
                                                MaterialDither::blue_noise};
    for (std::size_t index = 0U; index < dither_sheets.size(); ++index) {
        const Compilation dithered = compile(dither_sheets[index]);
        const MaterialDeclaration* const dither =
            field_of(only_rule(dithered, dither_sheets[index]), MaterialField::gradient_dither);
        require(dither != nullptr && (*dither).dither == dithers[index], "gradient-dither reads its keyword");
    }

    const std::string_view glow_sheet = "#pad::part(face) { glow: red 2px, 1px 3px blue; glow-placement: over }";
    const Compilation glowing = compile(glow_sheet);
    const ThemeRule& glow_rule = only_rule(glowing, glow_sheet);
    const MaterialDeclaration* const glows = field_of(glow_rule, MaterialField::glows);
    const MaterialDeclaration* const placements = field_of(glow_rule, MaterialField::glow_placements);
    require(glows != nullptr && (*glows).glows.size() == 2U && (*glows).glows[0].color == red &&
                (*glows).glows[0].blur_radius == 2.0 && (*glows).glows[0].spread == 0.0 &&
                (*glows).glows[1].color == blue && (*glows).glows[1].blur_radius == 1.0 &&
                (*glows).glows[1].spread == 3.0,
            "glow reads a colour and a blur and spread, in the written order");
    require(placements != nullptr && (*placements).glow_placements.size() == 1U &&
                (*placements).glow_placements[0] == MaterialGlowPlacement::over,
            "glow-placement reads its list");
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    fields[static_cast<std::size_t>(MaterialField::glow_placements)] = placements;
    fields[static_cast<std::size_t>(MaterialField::glows)] = glows;
    SurfaceMaterial assembled{};
    assemble_material(SurfaceMaterial{}, fields, Size{40.0, 24.0}, assembled);
    require(assembled.glows.size() == 2U && assembled.glows[0].placement == MaterialGlowPlacement::over &&
                assembled.glows[1].placement == MaterialGlowPlacement::over,
            "a short placement list repeats over the glows");
    SurfaceMaterial floor_glows{};
    floor_glows.glows = {MaterialGlow{}};
    std::array<const MaterialDeclaration*, material_field_count> placed{};
    placed[static_cast<std::size_t>(MaterialField::glow_placements)] = placements;
    assemble_material(floor_glows, placed, Size{40.0, 24.0}, assembled);
    require(assembled.glows.size() == 1U && assembled.glows[0].placement == MaterialGlowPlacement::over,
            "a placement applies to the floor's glows");

    const std::string_view fixed_sheet = "#pad::part(face) { mask-image: linear-gradient(black, transparent) }";
    const Compilation fixed = compile(fixed_sheet);
    const MaterialDeclaration* const fixed_mask = field_of(only_rule(fixed, fixed_sheet), MaterialField::mask);
    require(fixed_mask != nullptr && (*fixed_mask).gradients.empty() && (*fixed_mask).masks.size() == 1U &&
                (*fixed_mask).masks.front() ==
                    MaterialFillLayer::linear_css_angle(180.0, stops_of({{0.0, black}, {1.0, transparent}})),
            "a fixed mask gradient is resolved when the sheet compiles");
    const std::string_view sized_sheet = "#pad::part(face) { mask-image: radial-gradient(circle 10px, black, transparent) }";
    const Compilation sized = compile(sized_sheet);
    const MaterialDeclaration* const sized_mask = field_of(only_rule(sized, sized_sheet), MaterialField::mask);
    require(sized_mask != nullptr && (*sized_mask).gradients.size() == 1U && (*sized_mask).masks.size() == 1U,
            "a mask gradient of lengths waits for the box");
    std::array<const MaterialDeclaration*, material_field_count> masked{};
    masked[static_cast<std::size_t>(MaterialField::mask)] = sized_mask;
    require(material_fields_read_box(masked), "a mask gradient of lengths reads the box");
    assemble_material(SurfaceMaterial{}, masked, Size{40.0, 24.0}, assembled);
    require(assembled.masks.size() == 1U &&
                assembled.masks.front().source == MaterialFillLayer::radial(Point{20.0, 12.0}, Size{10.0, 10.0},
                                                                            stops_of({{0.0, black}, {1.0, transparent}}),
                                                                            MaterialCoordinateSpace::logical),
            "the mask is resolved for the part's box");

    // Several layers (E9.6, owner 2026-11-16): CSS lists the top layer
    // first, the material holds them bottom first; a deferred layer's
    // neighbours keep a solid placeholder gradient.
    const std::string_view layered_sheet =
        "#pad::part(face) { mask-image: linear-gradient(black, transparent), radial-gradient(circle 10px, black, "
        "transparent); mask-mode: luminance, inverse-alpha; mask-composite: subtract, add }";
    const Compilation layered = compile(layered_sheet);
    const ThemeRule& layered_rule = only_rule(layered, layered_sheet);
    const MaterialDeclaration* const layers = field_of(layered_rule, MaterialField::mask);
    require(layers != nullptr && (*layers).masks.size() == 2U && (*layers).gradients.size() == 2U &&
                (*layers).gradients[0].kind != MaterialFillKind::solid &&
                (*layers).gradients[1].kind == MaterialFillKind::solid,
            "two mask layers, bottom first, the radial one waiting for the box");
    const MaterialDeclaration* const modes = field_of(layered_rule, MaterialField::mask_modes);
    require(modes != nullptr &&
                (*modes).mask_modes ==
                    std::vector<MaterialMaskMode>{MaterialMaskMode::luminance, MaterialMaskMode::inverse_alpha},
            "mask-mode lowers in CSS's order");
    const MaterialDeclaration* const composites = field_of(layered_rule, MaterialField::mask_composites);
    require(composites != nullptr && (*composites).mask_composites ==
                                         std::vector<MaterialMaskComposite>{MaterialMaskComposite::subtract,
                                                                            MaterialMaskComposite::add},
            "mask-composite lowers in CSS's order");
    std::array<const MaterialDeclaration*, material_field_count> layered_fields{};
    layered_fields[static_cast<std::size_t>(MaterialField::mask)] = layers;
    layered_fields[static_cast<std::size_t>(MaterialField::mask_modes)] = modes;
    layered_fields[static_cast<std::size_t>(MaterialField::mask_composites)] = composites;
    assemble_material(SurfaceMaterial{}, layered_fields, Size{40.0, 24.0}, assembled);
    require(assembled.masks.size() == 2U && assembled.masks[1].mode == MaterialMaskMode::luminance &&
                assembled.masks[1].composite == MaterialMaskComposite::subtract &&
                assembled.masks[0].mode == MaterialMaskMode::inverse_alpha &&
                assembled.masks[0].source.kind == MaterialFillKind::radial_gradient &&
                assembled.masks[1].source.kind == MaterialFillKind::linear_gradient,
            "the top layer takes the first mode and composite");
    const std::string_view modes_only_sheet = "#pad::part(face) { mask-mode: match-source }";
    const Compilation modes_only = compile(modes_only_sheet);
    const MaterialDeclaration* const matched = field_of(only_rule(modes_only, modes_only_sheet), MaterialField::mask_modes);
    require(matched != nullptr && (*matched).mask_modes == std::vector<MaterialMaskMode>{MaterialMaskMode::alpha},
            "match-source reads an image layer's alpha");
}

void test_image_lowering() {
    const std::string_view position_sheet = "#pad::part(face) { background-position: right 25%, 10px 20px }";
    const Compilation positioned = compile(position_sheet);
    const MaterialDeclaration* const positions =
        field_of(only_rule(positioned, position_sheet), MaterialField::fill_positions);
    require(positions != nullptr && (*positions).image_positions.size() == 2U &&
                (*positions).image_positions[0] == MaterialImagePosition{BoxLength{1.0, 0.0}, BoxLength{0.25, 0.0}} &&
                (*positions).image_positions[1] == MaterialImagePosition{BoxLength{0.0, 10.0}, BoxLength{0.0, 20.0}},
            "background-position reads a position per layer");

    const std::string_view size_sheet = "#pad::part(face) { background-size: cover, 10px, auto 20%, 100% 100%, auto }";
    const Compilation sized = compile(size_sheet);
    const MaterialDeclaration* const sizes = field_of(only_rule(sized, size_sheet), MaterialField::fill_sizes);
    const std::array<MaterialImageSize, 5> expected_sizes{{
        {.sizing = MaterialImageSizing::cover, .length = BoxLength{}},
        {.sizing = MaterialImageSizing::width, .length = BoxLength{0.0, 10.0}},
        {.sizing = MaterialImageSizing::height, .length = BoxLength{0.2, 0.0}},
        {.sizing = MaterialImageSizing::stretch, .length = BoxLength{}},
        {.sizing = MaterialImageSizing::natural, .length = BoxLength{}},
    }};
    bool sizes_read = sizes != nullptr && (*sizes).image_sizes.size() == expected_sizes.size();
    for (std::size_t index = 0U; sizes_read && index < expected_sizes.size(); ++index) {
        sizes_read = (*sizes).image_sizes[index] == expected_sizes[index];
    }
    require(sizes_read, "background-size reads each layer's size");

    const std::string_view repeat_sheet = "#pad::part(face) { background-repeat: no-repeat, repeat repeat }";
    const Compilation repeated = compile(repeat_sheet);
    const MaterialDeclaration* const repeats = field_of(only_rule(repeated, repeat_sheet), MaterialField::fill_repeats);
    require(repeats != nullptr && (*repeats).image_repeats.size() == 2U &&
                (*repeats).image_repeats[0] == MaterialImageRepeat::no_repeat &&
                (*repeats).image_repeats[1] == MaterialImageRepeat::repeat,
            "background-repeat reads each layer's repetition");
    const std::string_view mirror_sheet = "#pad::part(face) { background-repeat: mirror, extend extend }";
    const Compilation mirrored = compile(mirror_sheet);
    const MaterialDeclaration* const mirrors = field_of(only_rule(mirrored, mirror_sheet), MaterialField::fill_repeats);
    require(mirrors != nullptr && (*mirrors).image_repeats.size() == 2U &&
                (*mirrors).image_repeats[0] == MaterialImageRepeat::mirror &&
                (*mirrors).image_repeats[1] == MaterialImageRepeat::extend,
            "background-repeat reads mirror and extend");

    const std::string_view image_sheet = "#pad::part(face) { background-image: url(\"resource:tile.quadrant\") }";
    const Compilation imaged = compile(image_sheet);
    const MaterialDeclaration* const images = field_of(only_rule(imaged, image_sheet), MaterialField::fills);
    require(images != nullptr && (*images).fills.size() == 1U && (*images).gradients.empty() &&
                (*images).fills[0] == MaterialFillLayer::tiled_image(ImageId{1U}, Size{4.0, 4.0}, 1.0),
            "an image resource is its bound image, tiled at its own size");

    // Cover on a 4-pixel image in a 40 x 24 box: ten times its size.
    const std::string_view cover_sheet = "#pad::part(face) { background-size: cover }";
    const Compilation covered = compile(cover_sheet);
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    fields[static_cast<std::size_t>(MaterialField::fills)] = images;
    fields[static_cast<std::size_t>(MaterialField::fill_sizes)] =
        field_of(only_rule(covered, cover_sheet), MaterialField::fill_sizes);
    require(material_fields_read_box(fields), "cover reads the box");
    SurfaceMaterial assembled{};
    assemble_material(SurfaceMaterial{}, fields, Size{40.0, 24.0}, assembled);
    require(assembled.fills.size() == 2U && assembled.fills.back().image_scale == 1.0 / 10.0 &&
                assembled.fills.back().image_mode == MaterialImageMode::tile,
            "cover scales the image to cover the box");
}

struct Refusal final {
    std::string_view sheet{};
    DiagnosticCode code{};
};

// `reflection` (stage T5, C7): the edge, up to two lengths and up to two
// opacities, with their defaults.
void test_reflection_lowering() {
    struct Lowered final {
        std::string_view sheet;
        MaterialReflection reflection;
    };
    const std::array<Lowered, 5> lowered{{
        {"#pad::part(face) { reflection: below 1px 6px 60% 0 }", MaterialReflection{MaterialEdge::bottom, 1.0, 6.0, 0.6, 0.0}},
        {"#pad::part(face) { reflection: above }", MaterialReflection{MaterialEdge::top, 0.0, 8.0, 0.5, 0.0}},
        {"#pad::part(face) { reflection: left 2px 0.25 }", MaterialReflection{MaterialEdge::left, 2.0, 6.0, 0.25, 0.0}},
        {"#pad::part(face) { reflection: right 0 3px 1 0.5 }", MaterialReflection{MaterialEdge::right, 0.0, 3.0, 1.0, 0.5}},
        {"#pad::part(face) { reflection: below 2px 40% backplane }",
         MaterialReflection{MaterialEdge::bottom, 2.0, 6.0, 0.4, 0.0, MaterialReflectionSource::backplane}},
    }};
    for (const Lowered& entry : lowered) {
        const Compilation compiled = compile(entry.sheet);
        const MaterialDeclaration* const declaration = field_of(only_rule(compiled, entry.sheet), MaterialField::reflection);
        require(declaration != nullptr && (*declaration).reflection.present() &&
                (*(*declaration).reflection.value()) == entry.reflection,
                std::string("the reflection lowers: ") + std::string(entry.sheet));
    }
    const std::string_view none_sheet = "#pad::part(face) { reflection: none }";
    const Compilation none = compile(none_sheet);
    const MaterialDeclaration* const cleared = field_of(only_rule(none, none_sheet), MaterialField::reflection);
    require(cleared != nullptr && (*cleared).reflection.absent(), "reflection: none clears it");
}

// transform and clip-path on a material (owner 2026-11-16): rotate() and
// scale() compose as a CSS transform list into the linear map about the
// box's centre; polygon() and path() lower to the clip path's verbs and
// points, with their fill rule; `none` clears each; malformed values are
// refused at their place.
void test_shape_lowering() {
    struct Turned final {
        std::string_view sheet;
        MaterialTransform transform;
    };
    // Every quarter turn is exact (the engine's deterministic degree form).
    const std::array<Turned, 9> turned{{
        {"#pad::part(face) { transform: rotate(0deg) }", MaterialTransform{1.0, 0.0, 0.0, 1.0}},
        {"#pad::part(face) { transform: rotate(90deg) }", MaterialTransform{0.0, 1.0, -1.0, 0.0}},
        {"#pad::part(face) { transform: rotate(180deg) }", MaterialTransform{-1.0, 0.0, 0.0, -1.0}},
        {"#pad::part(face) { transform: rotate(270deg) }", MaterialTransform{0.0, -1.0, 1.0, 0.0}},
        {"#pad::part(face) { transform: rotate(-450deg) }", MaterialTransform{0.0, -1.0, 1.0, 0.0}},
        {"#pad::part(face) { transform: scale(2, 50%) }", MaterialTransform{2.0, 0.0, 0.0, 0.5}},
        {"#pad::part(face) { transform: scale(1.5) }", MaterialTransform{1.5, 0.0, 0.0, 1.5}},
        // The last function applies first: scale, then the quarter turn.
        {"#pad::part(face) { transform: rotate(90deg) scale(2) }", MaterialTransform{0.0, 2.0, -2.0, 0.0}},
        {"#pad::part(face) { transform: none }", MaterialTransform{}},
    }};
    for (const Turned& entry : turned) {
        const Compilation compiled = compile(entry.sheet);
        const MaterialDeclaration* const declaration =
            field_of(only_rule(compiled, entry.sheet), MaterialField::transform);
        require(declaration != nullptr && (*declaration).transform == entry.transform,
                std::string("the transform lowers: ") + std::string(entry.sheet));
    }

    const std::string_view polygon_sheet = "#pad::part(face) { clip-path: polygon(50% 0, 100% 100%, 0 10px) }";
    const Compilation polygon = compile(polygon_sheet);
    const MaterialDeclaration* const outlined = field_of(only_rule(polygon, polygon_sheet), MaterialField::clip_path);
    const std::vector<MaterialClipVerb> closed_triangle{MaterialClipVerb::move, MaterialClipVerb::line,
                                                        MaterialClipVerb::line, MaterialClipVerb::close};
    const std::vector<MaterialClipPoint> polygon_points{MaterialClipPoint{0.0, 0.5, 0.0, 0.0},
                                                        MaterialClipPoint{0.0, 1.0, 0.0, 1.0},
                                                        MaterialClipPoint{0.0, 0.0, 10.0, 0.0}};
    require(outlined != nullptr && (*outlined).clip_path && (*(*outlined).clip_path).verbs == closed_triangle &&
                (*(*outlined).clip_path).points == polygon_points &&
                (*(*outlined).clip_path).rule == MaterialClipRule::nonzero,
            "polygon() lowers its vertices, lengths and percentages of the box, and closes");

    const std::string_view ruled_sheet = "#pad::part(face) { clip-path: polygon(evenodd, 0 0, 10px 0, 0 10px) }";
    const Compilation ruled = compile(ruled_sheet);
    const MaterialDeclaration* const even = field_of(only_rule(ruled, ruled_sheet), MaterialField::clip_path);
    require(even != nullptr && (*even).clip_path && (*(*even).clip_path).rule == MaterialClipRule::even_odd,
            "polygon() takes a leading fill rule");

    // Relative and absolute commands, H and V, a move's further pair as a
    // line, curves, and decimals without a leading digit or with an
    // exponent.
    const std::string_view path_sheet =
        "#pad::part(face) { clip-path: path(evenodd, \"M1e1,0 h 10 V.5e1 l-5 5 Q 0 10 0 5 c 0 -1 1 -2 2 -2 z\") }";
    const Compilation traced = compile(path_sheet);
    const MaterialDeclaration* const path = field_of(only_rule(traced, path_sheet), MaterialField::clip_path);
    const std::vector<MaterialClipVerb> path_verbs{MaterialClipVerb::move,      MaterialClipVerb::line,
                                                   MaterialClipVerb::line,      MaterialClipVerb::line,
                                                   MaterialClipVerb::quadratic, MaterialClipVerb::cubic,
                                                   MaterialClipVerb::close};
    const std::vector<MaterialClipPoint> path_points{
        MaterialClipPoint{10.0, 0.0, 0.0, 0.0}, MaterialClipPoint{20.0, 0.0, 0.0, 0.0},
        MaterialClipPoint{20.0, 0.0, 5.0, 0.0}, MaterialClipPoint{15.0, 0.0, 10.0, 0.0},
        MaterialClipPoint{0.0, 0.0, 10.0, 0.0}, MaterialClipPoint{0.0, 0.0, 5.0, 0.0},
        MaterialClipPoint{0.0, 0.0, 4.0, 0.0},  MaterialClipPoint{1.0, 0.0, 3.0, 0.0},
        MaterialClipPoint{2.0, 0.0, 3.0, 0.0}};
    require(path != nullptr && (*path).clip_path && (*(*path).clip_path).verbs == path_verbs &&
                (*(*path).clip_path).points == path_points &&
                (*(*path).clip_path).rule == MaterialClipRule::even_odd,
            "path() reads SVG path data in px from the box's corner");

    const std::string_view none_sheet = "#pad::part(face) { clip-path: none }";
    const Compilation none = compile(none_sheet);
    const MaterialDeclaration* const cleared = field_of(only_rule(none, none_sheet), MaterialField::clip_path);
    require(cleared != nullptr && !(*cleared).clip_path, "clip-path: none clears it");

    // S and T reflect the previous curve's last control point about the
    // current point.
    const std::string_view smooth_sheet =
        "#pad::part(face) { clip-path: path(\"M0 0 C 0 10 10 10 10 0 S 20 -10 20 0 Q 25 10 30 0 T 40 0 Z\") }";
    const Compilation smooth = compile(smooth_sheet);
    const MaterialDeclaration* const smoothed = field_of(only_rule(smooth, smooth_sheet), MaterialField::clip_path);
    const std::vector<MaterialClipVerb> smooth_verbs{MaterialClipVerb::move,      MaterialClipVerb::cubic,
                                                     MaterialClipVerb::cubic,     MaterialClipVerb::quadratic,
                                                     MaterialClipVerb::quadratic, MaterialClipVerb::close};
    const std::vector<MaterialClipPoint> smooth_points{
        MaterialClipPoint{0.0, 0.0, 0.0, 0.0},   MaterialClipPoint{0.0, 0.0, 10.0, 0.0},
        MaterialClipPoint{10.0, 0.0, 10.0, 0.0}, MaterialClipPoint{10.0, 0.0, 0.0, 0.0},
        MaterialClipPoint{10.0, 0.0, -10.0, 0.0}, MaterialClipPoint{20.0, 0.0, -10.0, 0.0},
        MaterialClipPoint{20.0, 0.0, 0.0, 0.0},  MaterialClipPoint{25.0, 0.0, 10.0, 0.0},
        MaterialClipPoint{30.0, 0.0, 0.0, 0.0},  MaterialClipPoint{35.0, 0.0, -10.0, 0.0},
        MaterialClipPoint{40.0, 0.0, 0.0, 0.0}};
    require(smoothed != nullptr && (*smoothed).clip_path && (*(*smoothed).clip_path).verbs == smooth_verbs &&
                (*(*smoothed).clip_path).points == smooth_points,
            "S and T lower to a cubic and a quadratic with the reflected control point");

    // An arc lowers to cubics by its fixed subdivision: a half turn of
    // radius 10 from (0, 0) to (20, 0), sweeping clockwise on screen, is two
    // quarter pieces through (10, -10), ending exactly at (20, 0). The flags
    // may be written without separators.
    const std::array<std::string_view, 2> arc_sheets{
        "#pad::part(face) { clip-path: path(\"M 0 0 A 10 10 0 0 1 20 0 Z\") }",
        "#pad::part(face) { clip-path: path(\"M0 0a10 10 0 0120 0z\") }"};
    for (const std::string_view arc_sheet : arc_sheets) {
        const Compilation arced = compile(arc_sheet);
        const MaterialDeclaration* const arc = field_of(only_rule(arced, arc_sheet), MaterialField::clip_path);
        const bool lowered = arc != nullptr && (*arc).clip_path;
        require(lowered, std::string("the arc lowers: ") + std::string(arc_sheet));
        if (!lowered) continue;
        const MaterialClipPath& clip = *(*arc).clip_path;
        const std::vector<MaterialClipVerb> arc_verbs{MaterialClipVerb::move, MaterialClipVerb::cubic,
                                                      MaterialClipVerb::cubic, MaterialClipVerb::close};
        require(clip.verbs == arc_verbs && clip.points.size() == 7U, "a half turn is two cubic pieces");
        if (clip.points.size() != 7U) continue;
        const MaterialClipPoint middle = clip.points[3];
        const MaterialClipPoint last = clip.points[6];
        require(std::abs(middle.x_units - 10.0) < 1e-9 && std::abs(middle.y_units + 10.0) < 1e-9,
                "the pieces meet at the top of the half turn");
        require(last.x_units == 20.0 && last.y_units == 0.0, "the last piece ends exactly at the arc's end");
        // The quarter piece's handle: 4/3 tan(22.5 degrees) = 4/3 (sqrt(2) - 1)
        // of the radius, the constant 0.55228474983079339840.
        const double handle = 0.55228474983079339840 * 10.0;
        require(std::abs(clip.points[1].y_units + handle) < 1e-9 && std::abs(clip.points[1].x_units) < 1e-9,
                "the first handle leaves the start upward by the stated length");
    }
    // A zero radius draws a line; equal endpoints draw nothing.
    const std::string_view flat_sheet = "#pad::part(face) { clip-path: path(\"M 0 0 A 0 5 0 0 1 10 0 A 5 5 0 0 1 10 0 L 0 10 Z\") }";
    const Compilation flat = compile(flat_sheet);
    const MaterialDeclaration* const flattened = field_of(only_rule(flat, flat_sheet), MaterialField::clip_path);
    const std::vector<MaterialClipVerb> flat_verbs{MaterialClipVerb::move, MaterialClipVerb::line,
                                                   MaterialClipVerb::line, MaterialClipVerb::close};
    require(flattened != nullptr && (*flattened).clip_path && (*(*flattened).clip_path).verbs == flat_verbs,
            "an arc of zero radius is a line, and one to its own start is nothing");

    const std::array<Refusal, 14> refusals{{
        {"#a { transform: rotate(1px) }", DiagnosticCode::value_wrong_kind},
        {"#a { transform: rotate(10deg, 20deg) }", DiagnosticCode::value_syntax},
        {"#a { transform: scale(1, 2, 3) }", DiagnosticCode::value_syntax},
        {"#a { transform: scale(0) }", DiagnosticCode::value_out_of_range},
        {"#a { transform: scale(100) }", DiagnosticCode::value_out_of_range},
        {"#a { transform: red }", DiagnosticCode::value_wrong_kind},
        {"#a { clip-path: border-box }", DiagnosticCode::value_form_not_admitted},
        {"#a { clip-path: polygon(0 0, 1px 1px) }", DiagnosticCode::value_syntax},
        {"#a { clip-path: polygon(0 0, 1em 0, 0 1px) }", DiagnosticCode::value_form_not_admitted},
        {"#a { clip-path: polygon(sideways, 0 0, 1px 0, 0 1px) }", DiagnosticCode::keyword_unknown},
        {"#a { clip-path: path(\"M 0 0 A 1 1 0 2 1 2 2\") }", DiagnosticCode::value_syntax},
        {"#a { clip-path: path(\"L 0 0\") }", DiagnosticCode::value_syntax},
        {"#a { clip-path: path(\"M 0 0 L\") }", DiagnosticCode::value_syntax},
        {"#a { clip-path: path(10px) }", DiagnosticCode::value_wrong_kind},
    }};
    for (const Refusal& refusal : refusals) {
        const Compilation compiled = compile(refusal.sheet);
        const bool refused = compiled.outcome == CompileOutcome::refused && !compiled.diagnostics.empty() &&
                             compiled.diagnostics.back().code == refusal.code;
        if (!refused && !compiled.diagnostics.empty()) {
            std::cerr << "  got " << diagnostic_code_name(compiled.diagnostics.back().code) << '\n';
        }
        require(refused, std::string("refused: ") + std::string(refusal.sheet));
    }
}

// The seat (skeudoindustrial surfaces): `seat` lowers to its height, as a
// length or a share of the shorter side, and its colour; the material is
// assembled with a raised part's contact shadow and crevice, or a sunk
// part's inset lip shade and seam, under the light from the upper left;
// the depth is clamped to a bas-relief's, a share resolves at the part's
// size, and a seat with no room among four shadows gives way.
void test_seat() {
    const std::string_view raised_sheet = "#pad::part(face) { seat: 2px }";
    const Compilation raised = compile(raised_sheet);
    const MaterialDeclaration* const height = field_of(only_rule(raised, raised_sheet), MaterialField::seat);
    require(height != nullptr && (*height).number == 2.0 && !(*height).relative &&
                (*height).colour == default_seat_colour,
            "seat: 2px lowers to a height of 2 in the default colour");
    const std::string_view sunk_sheet = "#pad::part(face) { seat: -6% #102030 }";
    const Compilation sunk = compile(sunk_sheet);
    const MaterialDeclaration* const share = field_of(only_rule(sunk, sunk_sheet), MaterialField::seat);
    require(share != nullptr && (*share).relative && std::abs((*share).number + 0.06) < 1e-12 &&
                (*share).colour == Color::rgba(0x10, 0x20, 0x30),
            "seat: -6% #102030 lowers to a sunk share and its colour");
    const std::string_view none_sheet = "#pad::part(face) { seat: none }";
    const Compilation none = compile(none_sheet);
    const MaterialDeclaration* const cleared = field_of(only_rule(none, none_sheet), MaterialField::seat);
    require(cleared != nullptr && (*cleared).number == 0.0, "seat: none seats nothing");
    require(compile("#pad::part(face) { seat: 2px 3px }").outcome != CompileOutcome::compiled,
            "a second length is not a colour");

    const Color shade = Color::rgba(0, 0, 0, 100);
    std::vector<MaterialShadow> standing{};
    seat_shadows(2.0, shade, standing);
    require(standing.size() == 2U, "a raised part casts a contact shadow and darkens its crevice");
    if (standing.size() == 2U) {
        require(!standing[0].inset && standing[0].offset == Point{1.0, 1.0} && standing[0].blur_radius == 2.0 &&
                    standing[0].spread == -0.5 && standing[0].color == shade,
                "the contact shadow falls half the height down and to the right, narrower than the part");
        require(!standing[1].inset && standing[1].offset == Point{0.0, 0.0} && standing[1].blur_radius == 1.0 &&
                    standing[1].color == Color::rgba(0, 0, 0, 80),
                "the crevice darkens all round, softer");
    }
    std::vector<MaterialShadow> well{};
    seat_shadows(-3.0, shade, well);
    require(well.size() == 2U && well[0].inset && well[1].inset && well[0].offset == Point{1.5, 1.5},
            "a sunk part's lip shades its upper left inside");
    std::vector<MaterialShadow> tall{};
    seat_shadows(10.0, shade, tall);
    require(!tall.empty() && tall[0].offset == Point{2.0, 2.0}, "the depth is clamped to maximum_seat_height");
    std::vector<MaterialShadow> flat{};
    seat_shadows(0.0, shade, flat);
    require(flat.empty(), "a height of 0 seats nothing");
    std::vector<MaterialShadow> crowded(3U);
    seat_shadows(2.0, shade, crowded);
    require(crowded.size() == SurfaceMaterial::maximum_shadows && !crowded.back().inset &&
                crowded.back().offset == Point{1.0, 1.0},
            "with room for one, the contact shadow stays and the crevice gives way");

    MaterialDeclaration relative{};
    relative.field = MaterialField::seat;
    relative.relative = true;
    relative.number = 0.25;
    relative.colour = shade;
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    fields[static_cast<std::size_t>(MaterialField::seat)] = &relative;
    require(material_fields_read_box(fields), "a share of the shorter side reads the box");
    SurfaceMaterial assembled{};
    assemble_material(SurfaceMaterial{}, fields, Size{40.0, 8.0}, assembled);
    require(assembled.shadows.size() == 2U && assembled.shadows[0].offset == Point{1.0, 1.0},
            "a quarter of an 8-unit side seats the part 2 units proud");
}

// `@filter <id>` and `filter: url(#id)` (capability review, owner
// 2026-11-16): the declared matrix and table, composed where named.
void test_theme_filters() {
    const std::string_view sheet = "#pad::part(face) { filter: url(#warm) contrast(1.5) }\n"
                                   "@filter warm { color-matrix: saturate(0.5); color-matrix: hue-rotate(90deg);\n"
                                   "  component-transfer: red linear(0.5 0.25) alpha discrete(0 1) }";
    const Compilation warm = compile(sheet);
    const MaterialDeclaration* const filter = field_of(only_rule(warm, sheet), MaterialField::colour_filter);
    require(filter != nullptr && (*filter).colour_filter.has_value() && (*filter).colour_filter.table() != nullptr,
            "a named filter lowers to a matrix and a table");
    const MaterialColourMatrix matrix =
        MaterialColourMatrix::saturate(0.5).then(MaterialColourMatrix::hue_rotate(90.0));
    require(*(*filter).colour_filter == matrix, "its color-matrix declarations compose in order");
    MaterialTransferFunction red{};
    red.kind = MaterialTransferKind::linear;
    red.slope = 0.5;
    red.intercept = 0.25;
    MaterialTransferFunction alpha{};
    alpha.kind = MaterialTransferKind::discrete;
    alpha.values = {0.0, 1.0};
    const MaterialColourTable table =
        MaterialColourTable::transfer(red, MaterialTransferFunction{}, MaterialTransferFunction{}, alpha)
            .then_matrix(MaterialColourMatrix::contrast(1.5));
    require(*(*filter).colour_filter.table() == table, "a per-channel function after it folds into the table");

    const std::string_view quoted = "@filter alpha-of { color-matrix: luminance-to-alpha }\n"
                                    "#pad::part(face) { filter: url(\"#alpha-of\") }";
    const Compilation alpha_of = compile(quoted);
    const MaterialDeclaration* const luminance = field_of(only_rule(alpha_of, quoted), MaterialField::colour_filter);
    MaterialColourMatrix to_alpha{};
    to_alpha.values = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                       0.0, 0.0, 0.0, 0.0, 0.0, 0.2125, 0.7154, 0.0721, 0.0, 0.0};
    require(luminance != nullptr && *(*luminance).colour_filter == to_alpha &&
                (*luminance).colour_filter.table() == nullptr,
            "a quoted fragment names the filter too, and a matrix alone has no table");

    const std::string_view raw = "@filter swap { color-matrix: 0 1 0 0 0  1 0 0 0 0  0 0 1 0 0  0 0 0 1 0 }\n"
                                 "#pad::part(face) { filter: url(#swap) }";
    const Compilation swapped = compile(raw);
    const MaterialDeclaration* const swap = field_of(only_rule(swapped, raw), MaterialField::colour_filter);
    require(swap != nullptr && (*(*swap).colour_filter).values[1] == 1.0 && (*(*swap).colour_filter).values[5] == 1.0,
            "twenty numbers are the matrix's rows");
}

void test_refusals() {
    const std::array<Refusal, 98> refusals{{
        {"#a { reflection: below backplane 1px }", DiagnosticCode::value_syntax},
        {"#a { reflection: below 1px mirror }", DiagnosticCode::keyword_unknown},
        {"#a { reflection: below 4px 6px }", DiagnosticCode::value_out_of_range},
        {"#a { reflection: below 9px }", DiagnosticCode::value_out_of_range},
        {"#a { reflection: sideways }", DiagnosticCode::keyword_unknown},
        {"#a { reflection: below 1px 2px 3px }", DiagnosticCode::value_syntax},
        {"#a { reflection: below 0.5 1px }", DiagnosticCode::value_syntax},
        {"#a { background-image: "
         "linear-gradient(red, blue), linear-gradient(red, blue), linear-gradient(red, blue), "
         "linear-gradient(red, blue), linear-gradient(red, blue), linear-gradient(red, blue), "
         "linear-gradient(red, blue), linear-gradient(red, blue), linear-gradient(red, blue) }",
         DiagnosticCode::too_many_layers},
        {"#a { background-image: linear-gradient(red, 40%, blue) }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-image: url(\"resource:icon.check\") }", DiagnosticCode::value_wrong_kind},
        {"#a { background: red fixed }", DiagnosticCode::value_form_not_admitted},
        {"#a { background: red, linear-gradient(red, blue) }", DiagnosticCode::value_syntax},
        {"#a { background-image: none, linear-gradient(red, blue) }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-blend-mode: hue }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-blend-mode: sideways }", DiagnosticCode::keyword_unknown},
        {"#a { background-color: 3px }", DiagnosticCode::value_wrong_kind},
        {"#a { background-color: inherit }", DiagnosticCode::value_wrong_kind},
        {"#a { background-image: radial-gradient(circle 10%, red, blue) }", DiagnosticCode::value_wrong_kind},
        {"#a { background-image: radial-gradient(-4px 4px, red, blue) }", DiagnosticCode::value_out_of_range},
        {"#a { background-image: reflecting-radial-gradient(red, blue, red, blue, red, blue, red, blue, red, blue, "
         "red, blue, red, blue, red, blue) }",
         DiagnosticCode::value_out_of_range},
        {"#a { background-image: radial-gradient(at center focus, red, blue) }", DiagnosticCode::value_syntax},
        {"#a { background-image: linear-gradient(to center, red, blue) }", DiagnosticCode::value_syntax},
        {"#a { background-image: radial-gradient(at left 10px top, red, blue) }",
         DiagnosticCode::value_form_not_admitted},
        {"#a { border-width: -1em }", DiagnosticCode::value_out_of_range},
        {"#a { border-image: url(\"resource:tile.quadrant\") 1 / 2px }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-style: inset }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-style: wavy }", DiagnosticCode::keyword_unknown},
        {"#a { border-radius: 4px / 2px }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-top-left-radius: 4px 2px }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-width: -1px }", DiagnosticCode::value_out_of_range},
        {"#a { border-width: 1px 1px 1px 1px 1px }", DiagnosticCode::value_syntax},
        {"#a { border: 1px 2px }", DiagnosticCode::value_syntax},
        {"#a { box-shadow: 1px }", DiagnosticCode::value_syntax},
        {"#a { box-shadow: 1px 1px -2px red }", DiagnosticCode::value_out_of_range},
        {"#a { box-shadow: 1px 1px red, 1px 1px red, 1px 1px red, 1px 1px red, 1px 1px red }",
         DiagnosticCode::too_many_layers},
        {"#a { filter: blur(1px) drop-shadow(1px 1px red) }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: drop-shadow(1px 1px red) drop-shadow(1px 1px red) }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: drop-shadow(1px 1px red) opacity(50%) }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: drop-shadow(1px) }", DiagnosticCode::value_syntax},
        {"#a { filter: drop-shadow(8px 8px 8px red) }", DiagnosticCode::value_out_of_range},
        {"#a { filter: url(\"resource:icon.check\") }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: brightness(-1) }", DiagnosticCode::value_out_of_range},
        {"#a { filter: blur(10%) }", DiagnosticCode::value_wrong_kind},
        {"#a { filter: blur(-1px) }", DiagnosticCode::value_out_of_range},
        {"#a { filter: hue-rotate(2px) }", DiagnosticCode::value_wrong_kind},
        {"#a { filter: red }", DiagnosticCode::value_wrong_kind},
        {"#a { filter: tritone(black, grey, white) saturate(2) }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: tritone(black, grey, white) tritone(black, grey, white) }",
         DiagnosticCode::value_form_not_admitted},
        {"#a { filter: drop-shadow(1px 1px red) tritone(black, grey, white) }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: duotone(black) }", DiagnosticCode::value_syntax},
        {"#a { filter: fill(1px) }", DiagnosticCode::value_wrong_kind},
        {"#a { filter: tritone(black grey white) }", DiagnosticCode::value_syntax},
        {"#a { filter: url(#missing) }", DiagnosticCode::resource_unknown},
        {"@filter f { color-matrix: 1 2 3 } #a { filter: url(#f) }", DiagnosticCode::value_syntax},
        {"@filter f { color-matrix: saturate(1px) } #a { filter: url(#f) }", DiagnosticCode::value_wrong_kind},
        {"@filter f { component-transfer: red identity; component-transfer: blue identity } #a { opacity: 1 }",
         DiagnosticCode::value_form_not_admitted},
        {"@filter f { component-transfer: red identity; color-matrix: saturate(2) } #a { opacity: 1 }",
         DiagnosticCode::value_form_not_admitted},
        {"@filter f { component-transfer: red gamma(1 0 0) } #a { opacity: 1 }", DiagnosticCode::value_out_of_range},
        {"@filter f { component-transfer: red identity red identity } #a { opacity: 1 }", DiagnosticCode::value_syntax},
        {"@filter g { component-transfer: red table(0 1) } #a { filter: tritone(black, grey, white) url(#g) }",
         DiagnosticCode::value_form_not_admitted},
        {"#a { surface-finish: 2 }", DiagnosticCode::value_out_of_range},
        {"#a { surface-finish: 1px }", DiagnosticCode::value_wrong_kind},
        {"#a { gradient-dither: dots }", DiagnosticCode::keyword_unknown},
        {"#a { gradient-dither: 2 }", DiagnosticCode::value_wrong_kind},
        {"#a { gradient-dither: ordered blue-noise }", DiagnosticCode::value_syntax},
        {"#a { glow: red }", DiagnosticCode::value_syntax},
        {"#a { glow: red 1px, red 1px, red 1px }", DiagnosticCode::too_many_layers},
        {"#a { glow: red 8px }", DiagnosticCode::value_out_of_range},
        {"#a { glow-placement: sideways }", DiagnosticCode::keyword_unknown},
        {"#a { mask-image: linear-gradient(black, white), linear-gradient(black, white) }",
         DiagnosticCode::too_many_layers},
        {"#a { mask-image: url(\"resource:icon.check\") }", DiagnosticCode::value_wrong_kind},
        {"#a { mask-image: url(\"resource:tile.quadrant\") }", DiagnosticCode::value_form_not_admitted},
        {"#a { filter: drop-shadow(1px 1px red) }", DiagnosticCode::value_form_not_admitted},
        {"#a { mask-image: linear-gradient(currentcolor, transparent) }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-size: 10px 20px }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-size: 0px }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-size: -1px }", DiagnosticCode::value_out_of_range},
        {"#a { background-repeat: repeat-x }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-repeat: mirror repeat }", DiagnosticCode::value_form_not_admitted},
        {"#a { background-repeat: sideways }", DiagnosticCode::keyword_unknown},
        {"#a { background-position: 150% }", DiagnosticCode::value_out_of_range},
        {"#a { background-position: left 10px top }", DiagnosticCode::value_form_not_admitted},
        {"#a { background: linear-gradient(red, blue) center }", DiagnosticCode::value_form_not_admitted},
        {"#a { background: url(\"resource:tile.quadrant\") / 4px }", DiagnosticCode::value_syntax},
        {"#a { border-image: url(\"resource:tile.quadrant\") 1 fill }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-image: linear-gradient(red, blue) 1 fill / 2px }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-image: url(\"resource:tile.quadrant\") 1 fill / 2px round }",
         DiagnosticCode::value_form_not_admitted},
        {"#a { border-image: url(\"resource:tile.quadrant\") 1 2 fill / 2px }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-image: url(\"resource:tile.quadrant\") 1 fill / 2px / 1px }",
         DiagnosticCode::value_form_not_admitted},
        {"#a { border-image: url(\"resource:tile.quadrant\") 1 fill / 1 }", DiagnosticCode::value_form_not_admitted},
        {"#a { border-image: url(\"resource:icon.check\") 1 fill / 2px }", DiagnosticCode::value_wrong_kind},
        {"#a { color: red blue }", DiagnosticCode::value_syntax},
        {"#a { outline: 2px dashed red }", DiagnosticCode::value_form_not_admitted},
        {"#a { outline-style: hidden }", DiagnosticCode::keyword_unknown},
        {"#a { outline-width: 17px }", DiagnosticCode::value_out_of_range},
        {"#a { outline-offset: 65px }", DiagnosticCode::value_out_of_range},
        {"#a { outline: 1px 2px }", DiagnosticCode::value_syntax},
        {"#a { text-shadow: 1px 1px red, 2px 2px blue }", DiagnosticCode::too_many_layers},
        {"#a { text-shadow: 1px 1px 9px red }", DiagnosticCode::value_out_of_range},
        {"#a { text-shadow: 1px 1px 1px 1px red }", DiagnosticCode::value_syntax},
        {"#a { text-shadow: 17px 0 red }", DiagnosticCode::value_out_of_range},
    }};
    for (const Refusal& refusal : refusals) {
        const Compilation compiled = compile(refusal.sheet);
        const bool refused = compiled.outcome == CompileOutcome::refused && !compiled.diagnostics.empty() &&
                             compiled.diagnostics.back().code == refusal.code;
        if (!refused && !compiled.diagnostics.empty()) {
            std::cerr << "  got " << diagnostic_code_name(compiled.diagnostics.back().code) << '\n';
        }
        require(refused, std::string("refused: ") + std::string(refusal.sheet));
    }
    const Compilation unknown = compile("#a { background-image: felt(red) }");
    require(unknown.outcome == CompileOutcome::compiled && unknown.sheet.rules.empty() &&
                unknown.diagnostics.size() == 1U && unknown.diagnostics[0].code == DiagnosticCode::unknown_function,
            "an unknown function drops the declaration with a warning");
    const Compilation unknown_filter = compile("#a { filter: felt(1) }");
    require(unknown_filter.outcome == CompileOutcome::compiled && unknown_filter.sheet.rules.empty() &&
                unknown_filter.diagnostics.size() == 1U &&
                unknown_filter.diagnostics[0].code == DiagnosticCode::unknown_function,
            "an unknown filter function drops the declaration with a warning");
}

// A rule with no part, role or item writing `filter` or `mask-*` is the
// control's content effect (owner 2026-11-16): its declarations go to the
// rule's content_effect, not its materials, and select no primitive. A
// part keeps the material's own filter and mask, an image mask among them;
// on a control, the mask's image placement is not admitted.
void test_content_effect_lowering() {
    const std::string_view sheet =
        "#a { filter: grayscale(1) blur(1px); mask-image: linear-gradient(black, transparent); mask-mode: luminance }";
    const Compilation compiled = compile(sheet);
    const ThemeRule& rule = only_rule(compiled, sheet);
    bool filter = false;
    bool blur = false;
    bool mask = false;
    bool modes = false;
    bool shadow = false;
    for (const MaterialDeclaration& declaration : rule.content_effect) {
        if (declaration.field == MaterialField::colour_filter) filter = declaration.colour_filter.has_value();
        if (declaration.field == MaterialField::filter_blur) blur = declaration.number == 2.0;
        if (declaration.field == MaterialField::mask) mask = declaration.masks.size() == 1U;
        if (declaration.field == MaterialField::mask_modes) modes = declaration.mask_modes.size() == 1U;
        if (declaration.field == MaterialField::drop_shadow) shadow = true;
    }
    require(rule.materials.empty() && rule.declarations.empty() && rule.content_effect.size() == 4U && filter &&
                blur && mask && modes && !shadow,
            "a control's filter and mask are its content effect, with no drop shadow and no primitive");
    const std::string_view on_part = "#a::part(face) { mask-image: url(\"resource:tile.quadrant\") }";
    const Compilation part = compile(on_part);
    const ThemeRule& part_rule = only_rule(part, on_part);
    require(part_rule.content_effect.empty() && field_of(part_rule, MaterialField::mask) != nullptr,
            "a part's mask is the material's own, an image allowed");
    const Compilation placed = compile("#a { mask-size: 8px }");
    require(placed.outcome == CompileOutcome::compiled && !placed.diagnostics.empty() &&
                placed.diagnostics.back().code == DiagnosticCode::property_not_admitted,
            "a control's mask has no image placement");
    const std::string_view cleared_sheet = "#a { filter: none }";
    const Compilation cleared = compile(cleared_sheet);
    const ThemeRule& cleared_rule = only_rule(cleared, cleared_sheet);
    require(cleared_rule.content_effect.size() == 2U && cleared_rule.materials.empty(),
            "filter: none clears the effect's colour filter and blur");
}

// A-gf-core-15-F1: a "one value" problem whose value has no second
// top-level component (one function with arguments, or a part shape
// missing its length or colour) is refused and placed inside the value,
// not at a component read past the value's end. Each sheet is compiled
// first, so nothing earlier left a longer value in the storage past it.
void test_one_value_without_a_second() {
    const std::array<std::string_view, 6> sheets{{
        "#a { outline-width: blur(2px) }",
        "#a { border-top-color: linear-gradient(red, blue) }",
        "#pad::part(face) { taper: red }",
        "#pad::part(face) { flare: red }",
        "#pad::part(face) { edge-shadow: 10px }",
        "#pad::part(face) { edge-shadow: red }",
    }};
    for (const std::string_view sheet : sheets) {
        const Compilation compiled = compile(sheet);
        require(!compiled.diagnostics.empty(), std::string("a problem is reported: ") + std::string(sheet));
        const ThemeDiagnostic& last = compiled.diagnostics.back();
        require(last.position.offset.value < sheet.size(),
                std::string("the problem is placed inside the sheet: ") + std::string(sheet));
        require(compiled.outcome == CompileOutcome::refused || last.code == DiagnosticCode::unknown_function,
                std::string("the value is refused: ") + std::string(sheet));
    }
}

// -- the gate: a sheet and C++ bake the same pixels ------------------------------------

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
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override { last = raster; }
    std::shared_ptr<const PartRaster> last{};
};

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, 40.0, 24.0};
        part.key.set(PartKey{"face", {}, {}});
        paint_part(painter, *this, part);
    }
};

// The face's pixels, drawn with `rules` over the button recipes' material
// replaced by `recipe`: for a sheet the floor its fields assemble over, for
// C++ the material the fields stand for.
// `resources` holds the images the materials draw.
std::uint64_t face_pixels(const ThemeRuleSheet& rules, const SurfaceMaterial& recipe,
                          const std::shared_ptr<ApplicationResources>& resources) {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*pad).set_requested_bounds(Rect{0.0, 0.0, 100.0, 84.0});
    (*root).add_child(pad);
    Window window(root, Size{140.0, 124.0}, resources);
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "theme-material-css-gate";
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& written : button.ordinary) written.material = recipe;
    for (ControlVisualRecipe& written : button.selected) written.material = recipe;
    definition.rules = rules;
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the gate theme compiles cleanly");
    window.perform_layout();
    RasterWatch painter{};
    window.paint(painter);
    require(painter.last != nullptr, "the face is drawn by the material primitive");
    if (painter.last == nullptr) return 0U;
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : (*painter.last).pixels()) {
        hash = hash ^ byte;
        hash = hash * 1099511628211ULL;
    }
    return hash;
}

ThemeRuleSheet written_rules() {
    ThemeRuleSheet sheet{};
    ThemeRule rule{};
    rule.label = "written";
    rule.level = CascadeLevel::stable_id_rule;
    rule.selector.stable_id = "pad";
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    sheet.rules.push_back(rule);
    return sheet;
}

struct GateRow final {
    std::string_view declarations{};
    SurfaceMaterial material{};
};

MaterialBorder border_of(Color colour, double width, MaterialBorderStyle style) {
    MaterialBorder border{};
    border.color = colour;
    border.width = width;
    border.style = style;
    return border;
}

MaterialShadow shadow_of(Point offset, double blur, double spread, Color colour, bool inset) {
    MaterialShadow shadow{};
    shadow.offset = offset;
    shadow.blur_radius = blur;
    shadow.spread = spread;
    shadow.color = colour;
    shadow.inset = inset;
    return shadow;
}

// A row whose C++ material is the plain floor with `fills`.
GateRow filled(std::string_view declarations, std::vector<MaterialFillLayer> fills) {
    GateRow row{};
    row.declarations = declarations;
    row.material.fills = std::move(fills);
    return row;
}

// The gate's images, in the window's resources.
struct GateImages final {
    ImageId quadrant{};
    ImageId dots{};
};

std::vector<GateRow> gate_rows(const GateImages& images) {
    const std::vector<GradientStop> red_blue = stops_of({{0.0, red}, {1.0, blue}});
    const double diagonal = engine_arctangent2(24.0, 40.0) * (180.0 / pi);
    const double corner = 0.5 * 1.41421356237309504880168872420969808;
    std::vector<GateRow> rows{};
    rows.push_back(filled("background: green", {MaterialFillLayer::solid(green)}));
    rows.push_back(filled("background: linear-gradient(135deg, red, blue)",
                          {MaterialFillLayer::linear_css_angle(135.0, red_blue)}));
    rows.push_back(filled("background: linear-gradient(to top right, red, blue)",
                          {MaterialFillLayer::linear_css_angle(diagonal, red_blue)}));
    rows.push_back(filled("background: linear-gradient(red 25%, green, blue 75%)",
                          {MaterialFillLayer::linear_css_angle(
                              180.0, stops_of({{0.0, red}, {0.25, red}, {0.5, green}, {0.75, blue}, {1.0, blue}}))}));
    rows.push_back(filled("background: repeating-linear-gradient(0deg, red 0px, blue 6px)",
                          {MaterialFillLayer::repeating_linear(Point{20.0, 24.0}, Point{20.0, 18.0}, red_blue,
                                                               MaterialCoordinateSpace::logical)}));
    rows.push_back(filled("background: radial-gradient(red, blue)",
                          {MaterialFillLayer::radial(Point{0.5, 0.5}, Size{corner, corner}, red_blue)}));
    rows.push_back(filled("background: radial-gradient(circle closest-side at 25% 50%, red, blue)",
                          {MaterialFillLayer::radial(Point{10.0, 12.0}, Size{10.0, 10.0}, red_blue,
                                                     MaterialCoordinateSpace::logical)}));
    rows.push_back(filled("background: repeating-radial-gradient(closest-side, red, blue 50%)",
                          {MaterialFillLayer::repeating_radial(Point{0.5, 0.5}, Size{0.25, 0.25}, red_blue)}));
    MaterialFillLayer focused = MaterialFillLayer::radial(Point{0.5, 0.5}, Size{corner, corner}, red_blue);
    focused.focused = true;
    focused.focus = Point{0.25, 0.5};
    rows.push_back(filled("background: radial-gradient(at 50% 50% focus 25% 50%, red, blue)", {focused}));
    ProceduralFill crossed{};
    crossed.pattern = ProceduralPattern::hatch;
    crossed.hatch_style = HatchStyle::cross;
    crossed.tile = 8.0;
    crossed.strength = 1.0;
    crossed.ground = red;
    crossed.figure = blue;
    crossed.accent = red;
    rows.push_back(filled("background: pattern(hatch-cross, red, blue)",
                          {MaterialFillLayer::procedural_pattern(crossed)}));
    rows.push_back(filled("background: conic-gradient(from 90deg, red, blue)",
                          {MaterialFillLayer::conic(Point{0.5, 0.5}, 90.0, red_blue)}));
    rows.push_back(filled("background: repeating-conic-gradient(red 0deg, blue 90deg)",
                          {MaterialFillLayer::repeating_conic(Point{0.5, 0.5}, 0.0, 90.0, red_blue)}));
    MaterialFillLayer reflected_conic = MaterialFillLayer::repeating_conic(Point{0.5, 0.5}, 0.0, 90.0, red_blue);
    reflected_conic.spread = GradientSpreadMode::reflect;
    rows.push_back(filled("background: reflecting-conic-gradient(red 0deg, blue 90deg)", {reflected_conic}));
    MaterialFillLayer reflected_linear = MaterialFillLayer::repeating_linear(Point{20.0, 24.0}, Point{20.0, 18.0},
                                                                             red_blue, MaterialCoordinateSpace::logical);
    reflected_linear.spread = GradientSpreadMode::reflect;
    rows.push_back(filled("background: reflecting-linear-gradient(0deg, red 0px, blue 6px)", {reflected_linear}));
    MaterialFillLayer reflected_rings = MaterialFillLayer::repeating_radial(Point{0.5, 0.5}, Size{0.25, 0.25}, red_blue);
    reflected_rings.spread = GradientSpreadMode::reflect;
    rows.push_back(filled("background: reflecting-radial-gradient(closest-side, red, blue 50%)", {reflected_rings}));
    MaterialFillLayer focused_rings = MaterialFillLayer::repeating_radial(Point{0.5, 0.5}, Size{0.25, 0.25}, red_blue);
    focused_rings.focused = true;
    focused_rings.focus = Point{0.4, 0.5};
    rows.push_back(filled("background: repeating-radial-gradient(closest-side at 50% 50% focus 40% 50%, red, blue 50%)",
                          {focused_rings}));
    rows.push_back(filled("background: linear-gradient(red, blue) green",
                          {MaterialFillLayer::solid(green), MaterialFillLayer::linear_css_angle(180.0, red_blue)}));
    MaterialFillLayer multiplied = MaterialFillLayer::linear_css_angle(90.0, red_blue);
    multiplied.blend = MaterialBlendMode::multiply;
    rows.push_back(filled("background: linear-gradient(90deg, red, blue) green; background-blend-mode: multiply",
                          {MaterialFillLayer::solid(green), multiplied}));

    GateRow border{};
    border.declarations = "border: 2px solid red";
    border.material.border.set(border_of(red, 2.0, MaterialBorderStyle::solid));
    rows.push_back(border);
    GateRow dashed{};
    dashed.declarations = "border: 2px dashed red; border-radius: 6px";
    dashed.material.border.set(border_of(red, 2.0, MaterialBorderStyle::dashed));
    dashed.material.corner_radius = 6.0;
    rows.push_back(dashed);
    GateRow edges{};
    edges.declarations = "border-top: 3px solid blue; border-bottom: 1px solid red";
    edges.material.border_edges.top.set(border_of(blue, 3.0, MaterialBorderStyle::solid));
    edges.material.border_edges.bottom.set(border_of(red, 1.0, MaterialBorderStyle::solid));
    rows.push_back(edges);
    GateRow sides{};
    sides.declarations = "border-style: solid; border-width: 1px 2px; border-color: red blue";
    sides.material.border_edges.top.set(border_of(red, 1.0, MaterialBorderStyle::solid));
    sides.material.border_edges.right.set(border_of(blue, 2.0, MaterialBorderStyle::solid));
    sides.material.border_edges.bottom.set(border_of(red, 1.0, MaterialBorderStyle::solid));
    sides.material.border_edges.left.set(border_of(blue, 2.0, MaterialBorderStyle::solid));
    rows.push_back(sides);
    GateRow radii{};
    radii.declarations = "border-radius: 4px 8px";
    radii.material.corner_radius = 4.0;
    radii.material.corner_radii.set(MaterialCornerRadii{4.0, 8.0, 4.0, 8.0});
    rows.push_back(radii);
    GateRow relative{};
    relative.declarations = "border-radius: 50%";
    relative.material.corner_radius = 12.0;
    rows.push_back(relative);
    GateRow shadows{};
    shadows.declarations = "box-shadow: 0 2px 4px rgb(0 0 0 / 40%), inset 0 1px 0 white";
    shadows.material.shadows = {shadow_of(Point{0.0, 1.0}, 0.0, 0.0, Color::rgba(255, 255, 255), true),
                                shadow_of(Point{0.0, 2.0}, 4.0, 0.0, Color::rgba(0, 0, 0, 102), false)};
    rows.push_back(shadows);

    GateRow colours{};
    colours.declarations = "background: red; filter: grayscale(100%) brightness(0.5)";
    colours.material.fills = {MaterialFillLayer::solid(red)};
    colours.material.colour_filter = MaterialColourMatrix::grayscale(1.0).then(MaterialColourMatrix::brightness(0.5));
    rows.push_back(colours);
    // The theme language's colour stages (owner 2026-11-16).
    GateRow filled_stage{};
    filled_stage.declarations = "background: red; filter: fill(#102030)";
    filled_stage.material.fills = {MaterialFillLayer::solid(red)};
    filled_stage.material.colour_filter = MaterialColourMatrix::fill(Color::rgba(16, 32, 48));
    rows.push_back(filled_stage);
    GateRow duotoned{};
    duotoned.declarations = "background: red; filter: duotone(#000000, #ffffff)";
    duotoned.material.fills = {MaterialFillLayer::solid(red)};
    duotoned.material.colour_filter =
        MaterialColourMatrix::duotone(Color::rgba(0, 0, 0), Color::rgba(255, 255, 255));
    rows.push_back(duotoned);
    GateRow tritoned{};
    tritoned.declarations = "background: red; filter: tritone(blue, lime, red) brightness(0.5)";
    tritoned.material.fills = {MaterialFillLayer::solid(red)};
    const MaterialColourTable tones =
        MaterialColourTable::tritone(Color::rgba(0, 0, 255), Color::rgba(0, 255, 0), Color::rgba(255, 0, 0));
    tritoned.material.colour_filter =
        MaterialColourFilter(MaterialColourMatrix::luminance(), tones.then_matrix(MaterialColourMatrix::brightness(0.5)));
    rows.push_back(tritoned);
    GateRow blurred{};
    blurred.declarations = "background: blue; filter: blur(1px) blur(1px)";
    blurred.material.fills = {MaterialFillLayer::solid(blue)};
    blurred.material.filter_blur = 2.0 * std::sqrt(2.0);
    rows.push_back(blurred);
    GateRow dropped{};
    dropped.declarations = "background: blue; filter: drop-shadow(1px 2px 2px red) invert(1)";
    dropped.material.fills = {MaterialFillLayer::solid(blue)};
    dropped.material.colour_filter = MaterialColourMatrix::invert(1.0);
    MaterialDropShadow drop{};
    drop.offset = Point{1.0, 2.0};
    drop.blur_radius = 2.0;
    drop.color = Color::rgba(0, 255, 255);
    dropped.material.drop_shadow.set(drop);
    rows.push_back(dropped);
    GateRow finished{};
    finished.declarations = "background: blue; surface-finish: 0.5";
    finished.material.fills = {MaterialFillLayer::solid(blue)};
    finished.material.finish = 0.5;
    rows.push_back(finished);
    GateRow dithered{};
    dithered.declarations = "background: linear-gradient(blue, white); gradient-dither: blue-noise";
    dithered.material.fills = {
        MaterialFillLayer::linear_css_angle(180.0, stops_of({{0.0, blue}, {1.0, Color::rgba(255, 255, 255)}}))};
    dithered.material.dither = MaterialDither::blue_noise;
    rows.push_back(dithered);
    GateRow glowing{};
    glowing.declarations = "background: blue; glow: red 2px 1px, white 1px; glow-placement: over, under";
    glowing.material.fills = {MaterialFillLayer::solid(blue)};
    MaterialGlow over_glow{};
    over_glow.color = red;
    over_glow.blur_radius = 2.0;
    over_glow.spread = 1.0;
    over_glow.placement = MaterialGlowPlacement::over;
    MaterialGlow under_glow{};
    under_glow.color = Color::rgba(255, 255, 255);
    under_glow.blur_radius = 1.0;
    under_glow.spread = 0.0;
    under_glow.placement = MaterialGlowPlacement::under;
    glowing.material.glows = {over_glow, under_glow};
    rows.push_back(glowing);
    const std::vector<GradientStop> fading = stops_of({{0.0, black}, {1.0, transparent}});
    GateRow linear_mask{};
    linear_mask.declarations = "background: blue; mask-image: linear-gradient(black, transparent)";
    linear_mask.material.fills = {MaterialFillLayer::solid(blue)};
    linear_mask.material.masks = {MaterialMaskLayer{.source = MaterialFillLayer::linear_css_angle(180.0, fading)}};
    rows.push_back(linear_mask);
    GateRow radial_mask{};
    radial_mask.declarations = "background: blue; mask-image: radial-gradient(circle 10px, black, transparent)";
    radial_mask.material.fills = {MaterialFillLayer::solid(blue)};
    const MaterialFillLayer radial_fade =
        MaterialFillLayer::radial(Point{20.0, 12.0}, Size{10.0, 10.0}, fading, MaterialCoordinateSpace::logical);
    radial_mask.material.masks = {MaterialMaskLayer{.source = radial_fade}};
    rows.push_back(radial_mask);

    const Size four{4.0, 4.0};
    const MaterialFillLayer tiles = MaterialFillLayer::tiled_image(images.quadrant, four, 1.0);
    const MaterialFillLayer white = MaterialFillLayer::solid(Color::rgba(255, 255, 255));
    rows.push_back(filled("background: url(\"resource:tile.quadrant\")", {tiles}));
    MaterialFillLayer placed = tiles;
    placed.image_scale = 0.5;
    placed.image_mode = MaterialImageMode::single;
    placed.image_anchor = Point{1.0, 0.25};
    rows.push_back(filled("background: url(\"resource:tile.quadrant\") right 25% / 8px no-repeat blue",
                          {MaterialFillLayer::solid(blue), placed}));
    MaterialFillLayer contained = tiles;
    contained.image_scale = 1.0 / 6.0;
    contained.image_anchor = Point{0.5, 0.5};
    rows.push_back(filled("background-image: url(\"resource:tile.quadrant\"); background-size: contain; "
                          "background-position: center",
                          {white, contained}));
    MaterialFillLayer half = tiles;
    half.image_scale = 4.0 / (0.5 * 40.0 + 0.0);
    half.image_mode = MaterialImageMode::single;
    rows.push_back(filled("background-image: url(\"resource:tile.quadrant\"); background-size: 50% auto; "
                          "background-repeat: no-repeat",
                          {white, half}));
    MaterialFillLayer mirrored_tiles = tiles;
    mirrored_tiles.image_mode = MaterialImageMode::mirror;
    rows.push_back(filled("background: url(\"resource:tile.quadrant\") mirror", {mirrored_tiles}));
    MaterialFillLayer extended_tile = tiles;
    extended_tile.image_mode = MaterialImageMode::extend;
    extended_tile.image_anchor = Point{0.5, 0.5};
    rows.push_back(filled("background-image: url(\"resource:tile.quadrant\"); background-repeat: extend; "
                          "background-position: center",
                          {white, extended_tile}));
    rows.push_back(filled("background-image: url(\"resource:tile.quadrant\"); background-size: 100% 100%",
                          {white, MaterialFillLayer::stretched_image(images.quadrant, four)}));
    GateRow image_mask{};
    image_mask.declarations = "background: blue; mask-image: url(\"resource:mask.dots\")";
    image_mask.material.fills = {MaterialFillLayer::solid(blue)};
    image_mask.material.masks = {MaterialMaskLayer{.source = MaterialFillLayer::tiled_image(images.dots, Size{2.0, 2.0}, 0.5)}};
    rows.push_back(image_mask);
    // The mask longhands (owner 2026-11-16): an image layer placed as a
    // background image is, under a luminance gradient that subtracts it.
    GateRow placed_mask{};
    placed_mask.declarations = "background: blue; mask-image: linear-gradient(black, transparent), "
                               "url(\"resource:mask.dots\"); mask-mode: luminance, alpha; mask-composite: subtract; "
                               "mask-size: auto, 8px; mask-repeat: repeat, mirror; mask-position: 0 0, center";
    placed_mask.material.fills = {MaterialFillLayer::solid(blue)};
    MaterialFillLayer placed_dots = MaterialFillLayer::tiled_image(images.dots, Size{2.0, 2.0}, 0.5);
    placed_dots.image_scale = 2.0 / 8.0;
    placed_dots.image_mode = MaterialImageMode::mirror;
    placed_dots.image_anchor = Point{0.5, 0.5};
    placed_mask.material.masks = {
        MaterialMaskLayer{.source = placed_dots},
        MaterialMaskLayer{.source = MaterialFillLayer::linear_css_angle(180.0, fading),
                          .mode = MaterialMaskMode::luminance,
                          .composite = MaterialMaskComposite::subtract}};
    rows.push_back(placed_mask);

    // Each edge's two lines, the outer first, in CSS's edge order.
    const std::array<MaterialEdge, 4> each_edge{MaterialEdge::top, MaterialEdge::right, MaterialEdge::bottom,
                                                MaterialEdge::left};
    GateRow doubled{};
    doubled.declarations = "border: 3px double red";
    for (const MaterialEdge edge : each_edge) {
        doubled.material.keylines.push_back(MaterialKeyline{edge, red, 1.0, 0.0});
        doubled.material.keylines.push_back(MaterialKeyline{edge, red, 1.0, 2.0});
    }
    rows.push_back(doubled);
    const Color clay = Color::rgba(200, 100, 50);
    const Color shade = Color::rgba(100, 50, 25);
    GateRow grooved{};
    grooved.declarations = "border: 4px groove rgb(200 100 50)";
    for (const MaterialEdge edge : each_edge) {
        const bool lit = edge == MaterialEdge::bottom || edge == MaterialEdge::right;
        grooved.material.keylines.push_back(MaterialKeyline{edge, lit ? clay : shade, 2.0, 0.0});
        grooved.material.keylines.push_back(MaterialKeyline{edge, lit ? shade : clay, 2.0, 2.0});
    }
    rows.push_back(grooved);
    GateRow ridged{};
    ridged.declarations = "border-top: 2px ridge blue";
    ridged.material.keylines = {MaterialKeyline{MaterialEdge::top, blue, 1.0, 0.0},
                                MaterialKeyline{MaterialEdge::top, Color::rgba(0, 0, 127), 1.0, 1.0}};
    rows.push_back(ridged);
    GateRow framed{};
    framed.declarations = "border: 2px solid red; border-image: url(\"resource:tile.quadrant\") 1 fill / 2px";
    framed.material.fills = {white,
                             MaterialFillLayer::nine_patch(images.quadrant, four, Insets{1.0, 1.0, 1.0, 1.0}, 0.5)};
    rows.push_back(framed);
    return rows;
}

// Premultiplied BGRA pixels: a 4 x 4 image of four coloured quadrants, and
// a 2 x 2 one of black at alphas 255, 0, 128 and 0.
std::vector<std::byte> quadrant_pixels() {
    std::vector<std::byte> pixels(4U * 4U * 4U);
    const std::array<std::array<std::uint8_t, 4>, 4> quadrants{{
        {0U, 0U, 255U, 255U}, {0U, 255U, 0U, 255U}, {255U, 0U, 0U, 255U}, {255U, 255U, 255U, 255U}}};
    for (std::size_t row = 0U; row < 4U; ++row) {
        for (std::size_t column = 0U; column < 4U; ++column) {
            const std::size_t quadrant = (row / 2U) * 2U + column / 2U;
            const std::size_t offset = (row * 4U + column) * 4U;
            for (std::size_t index = 0U; index < 4U; ++index) {
                pixels[offset + index] = static_cast<std::byte>(quadrants[quadrant][index]);
            }
        }
    }
    return pixels;
}

std::vector<std::byte> dot_pixels() {
    const std::array<std::uint8_t, 16> bytes{0U, 0U, 0U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 128U, 0U, 0U, 0U, 0U};
    std::vector<std::byte> pixels(bytes.size());
    for (std::size_t index = 0U; index < bytes.size(); ++index) pixels[index] = static_cast<std::byte>(bytes[index]);
    return pixels;
}

void test_gate() {
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    const std::vector<std::byte> quadrant = quadrant_pixels();
    const std::vector<std::byte> dots = dot_pixels();
    const ImageLoadResult quadrant_loaded = (*resources).load_bgra32_premultiplied(4U, 4U, 16U, quadrant);
    const ImageLoadResult dots_loaded = (*resources).load_bgra32_premultiplied(2U, 2U, 8U, dots);
    require(static_cast<bool>(quadrant_loaded) && static_cast<bool>(dots_loaded), "the gate's images load");
    const GateImages images{.quadrant = quadrant_loaded.image, .dots = dots_loaded.image};
    // The dots are drawn at half the density: two logical units a pixel.
    const std::array<ThemeImageResource, 2> bound{{
        {.resource = "tile.quadrant", .image = images.quadrant, .pixel_size = Size{4.0, 4.0},
         .pixels_per_logical_pixel = 1.0},
        {.resource = "mask.dots", .image = images.dots, .pixel_size = Size{2.0, 2.0}, .pixels_per_logical_pixel = 0.5},
    }};
    const SurfaceMaterial plain{};
    const std::uint64_t floor_pixels = face_pixels(written_rules(), plain, resources);
    const std::vector<GateRow> rows = gate_rows(images);
    for (const GateRow& row : rows) {
        const std::string sheet = "#pad::part(face) { " + std::string(row.declarations) + " }";
        const Compilation compiled = compile(sheet, bound);
        require(compiled.outcome == CompileOutcome::compiled, "the row compiles: " + sheet);
        require(valid_surface_material(row.material), "the C++ material is valid: " + sheet);
        const std::uint64_t from_sheet = face_pixels(compiled.sheet, plain, resources);
        const std::uint64_t from_cpp = face_pixels(written_rules(), row.material, resources);
        require(from_sheet == from_cpp, "the sheet bakes the pixels of the C++ material: " + sheet);
        require(from_cpp != floor_pixels, "and the row changes the face: " + sheet);
    }
}

} // namespace

int main() {
    test_one_value_without_a_second();
    test_placement();
    test_linear_geometry();
    test_radial_geometry();
    test_conic_geometry();
    test_lowering();
    test_border_lowering();
    test_line_lowering();
    test_recipe_lowering();
    test_filter_lowering();
    test_image_lowering();
    test_reflection_lowering();
    test_shape_lowering();
    test_seat();
    test_theme_filters();
    test_content_effect_lowering();
    test_refusals();
    test_gate();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
