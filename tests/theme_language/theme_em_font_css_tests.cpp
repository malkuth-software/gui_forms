// em, text roles and the current colour (stage T3, phase P8c, engine
// change EC5): `font: <role>` records a part's text role; a role's size is
// the theme's typography token times the window's text scale, and is the
// part's em; each em length a material, recipe, translate or parameter
// declaration writes is carried as pixels plus em and taken at the part's
// em. `color: inherit` takes the parent control's text colour, and
// currentcolor the part's own, at paint. Gate: em at two text scales, the
// font's role and its inheritance, the em lengths' records, and the
// current colour's.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using theme_language::CompileOutcome;
using theme_language::DiagnosticCode;
using theme_language::SelectorVocabulary;
using theme_language::ThemeCompileInput;
using theme_language::ThemeCompiler;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;
using theme_language::diagnostic_code_name;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Compilation final {
    CompileOutcome outcome{CompileOutcome::refused};
    ThemeRuleSheet sheet{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

// Compiles `text`; a compiled sheet must be one the engine accepts whole.
Compilation compile(std::string_view text) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = text;
    ThemeCompileInput input{.source = source,
                            .registry = registry,
                            .vocabulary = SelectorVocabulary{},
                            .published_values = {},
                            .resources = {},
                            .images = {},
                            .requires_engine = current_engine_version};
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

bool compiled_clean(const Compilation& compilation) {
    const bool clean = compilation.outcome == CompileOutcome::compiled && compilation.diagnostics.empty();
    return clean;
}

// The only rule of a clean compilation.
ThemeRule only_rule(std::string_view text) {
    const Compilation compiled = compile(text);
    const bool one = compiled_clean(compiled) && compiled.sheet.rules.size() == 1U;
    require(one, std::string("one clean rule: ") + std::string(text));
    if (!one) return ThemeRule{};
    return compiled.sheet.rules[0];
}

const MaterialDeclaration* material_of(const ThemeRule& rule, MaterialField field) {
    for (const MaterialDeclaration& declaration : rule.materials) {
        if (declaration.field == field) return &declaration;
    }
    return nullptr;
}

const RecipeDeclaration* recipe_of(const ThemeRule& rule, RecipeField field) {
    for (const RecipeDeclaration& declaration : rule.recipe_fields) {
        if (declaration.field == field) return &declaration;
    }
    return nullptr;
}

std::optional<std::string> font_of(const ThemeRule& rule) {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == font_property) return declaration.name;
    }
    return std::nullopt;
}

// -- font ----------------------------------------------------------------------

void test_font_lowering() {
    require(text_role_count == 6U && text_role_name(TextRole::ui) == "ui" &&
                text_role_name(TextRole::monospace) == "monospace" &&
                text_role_from_name("heading") == TextRole::heading && !text_role_from_name("Heading"),
            "the six text roles are named");
    require(font_of(only_rule("#a { font: heading }")) == "heading", "font names a text role");
    require(font_of(only_rule("#a { font: TITLE }")) == "title", "in any case");
    require(font_of(only_rule("#a { font: inherit }")) == "inherit", "inherit is the parent control's");
    require(font_of(only_rule("#a { font: initial }")) == "ui", "initial is the ui role");
    require(font_of(only_rule("#a { font: caption; font: field }")) == "field", "the later declaration wins");

    const Compilation unknown = compile("#a { font: huge }");
    require(unknown.outcome == CompileOutcome::refused && !unknown.diagnostics.empty() &&
                unknown.diagnostics.back().code == DiagnosticCode::keyword_unknown &&
                unknown.diagnostics.back().detail == "huge",
            "an unknown role is E406");
    const Compilation shorthand = compile("#a { font: bold 12px serif }");
    require(shorthand.outcome == CompileOutcome::refused && !shorthand.diagnostics.empty() &&
                shorthand.diagnostics.back().code == DiagnosticCode::value_form_not_admitted,
            "CSS's font shorthand is E405");
    const Compilation unset = compile("#a { font: unset }");
    require(unset.outcome == CompileOutcome::refused && !unset.diagnostics.empty() &&
                unset.diagnostics.back().code == DiagnosticCode::value_form_not_admitted,
            "unset is E405");

    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeRuleSheet sheet{};
    ThemeRule rule{};
    rule.selector.stable_id = "a";
    rule.declarations.push_back(RuleDeclaration::choice_value(font_property, "enormous"));
    sheet.rules.push_back(rule);
    const CompiledThemeRules refused = CompiledThemeRules::compile(sheet, registry);
    require(refused.diagnostics().size() == 2U && refused.diagnostics()[0].kind == RuleDiagnosticKind::font_invalid &&
                rule_diagnostic_name(RuleDiagnosticKind::font_invalid) == "font_invalid",
            "the engine drops a font that names no role");
}

// -- em lengths ----------------------------------------------------------------

void test_em_records() {
    const ThemeRule widths = only_rule("#a { border-width: 0.5em 2px; border-radius: 1em }");
    const MaterialDeclaration* top = material_of(widths, MaterialField::border_top_width);
    const MaterialDeclaration* right = material_of(widths, MaterialField::border_right_width);
    const MaterialDeclaration* corner = material_of(widths, MaterialField::corner_top_left);
    require(top != nullptr && (*top).number == 0.0 && (*top).em_lengths == std::vector<double>{0.5},
            "an em width is 0 px and 0.5 em");
    require(right != nullptr && (*right).number == 2.0 && (*right).em_lengths.empty(),
            "a px width has no em lengths");
    require(corner != nullptr && !(*corner).relative && (*corner).em_lengths == std::vector<double>{1.0},
            "a corner's radius in em");

    const ThemeRule shadow = only_rule("#a { box-shadow: 1px 2em 3px 0.25em rgb(0 0 0 / 50%) }");
    const MaterialDeclaration* shadows = material_of(shadow, MaterialField::shadows);
    require(shadows != nullptr && (*shadows).shadows.size() == 1U &&
                (*shadows).shadows[0].offset == Point{1.0, 0.0} && (*shadows).shadows[0].blur_radius == 3.0 &&
                (*shadows).em_lengths == std::vector<double>({0.0, 2.0, 0.0, 0.25}),
            "a shadow's lengths are each px plus em");

    const ThemeRule ring = only_rule("#a { outline: 0.125em solid; outline-offset: 1em; text-shadow: 0 1em red }");
    const RecipeDeclaration* width = recipe_of(ring, RecipeField::outline_width);
    const RecipeDeclaration* offset = recipe_of(ring, RecipeField::outline_offset);
    const RecipeDeclaration* text_shadow = recipe_of(ring, RecipeField::text_shadow);
    require(width != nullptr && (*width).em_lengths == std::vector<double>{0.125} && offset != nullptr &&
                (*offset).em_lengths == std::vector<double>{1.0} && text_shadow != nullptr &&
                (*text_shadow).em_lengths == std::vector<double>({0.0, 1.0, 0.0}),
            "the recipe's lengths take em too");

    const ThemeRule gradient = only_rule("#a { background-image: linear-gradient(red 1em, blue) }");
    const MaterialDeclaration* fills = material_of(gradient, MaterialField::fills);
    require(fills != nullptr && (*fills).gradients.size() == 1U && (*fills).gradients[0].stops.size() == 2U &&
                (*fills).em_lengths.size() == 6U && (*fills).em_lengths[4] == 1.0,
            "a gradient with an em stop keeps its lengths, resolved when assembled");
    const ThemeRule pixels = only_rule("#a { background-image: linear-gradient(red 12px, blue) }");
    const MaterialDeclaration* fixed = material_of(pixels, MaterialField::fills);
    require(fixed != nullptr && (*fixed).em_lengths.empty(),
            "a gradient in px has no em lengths");

    const Compilation negative = compile("#a { border-width: -1em }");
    require(negative.outcome == CompileOutcome::refused && !negative.diagnostics.empty() &&
                negative.diagnostics.back().code == DiagnosticCode::value_out_of_range,
            "a negative em width is out of range");
}

void test_em_assembly() {
    MaterialDeclaration width{};
    width.field = MaterialField::border_top_width;
    width.number = 1.0;
    width.em_lengths = {0.5};
    MaterialDeclaration style{};
    style.field = MaterialField::border_top_style;
    style.border_style = MaterialBorderStyle::solid;
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    fields[static_cast<std::size_t>(MaterialField::border_top_width)] = &width;
    fields[static_cast<std::size_t>(MaterialField::border_top_style)] = &style;
    require(material_fields_read_em(fields), "a field with em lengths reads the em");
    SurfaceMaterial floor{};
    SurfaceMaterial at_twelve{};
    assemble_material(floor, fields, Size{40.0, 20.0}, MaterialAssemblyContext{12.0, Color{}}, at_twelve);
    SurfaceMaterial at_twenty_four{};
    assemble_material(floor, fields, Size{40.0, 20.0}, MaterialAssemblyContext{24.0, Color{}}, at_twenty_four);
    const bool sized = at_twelve.border_edges.top.has_value() && at_twenty_four.border_edges.top.has_value() &&
                       (*at_twelve.border_edges.top).width == 7.0 && (*at_twenty_four.border_edges.top).width == 13.0;
    require(sized, "1px + 0.5em at 12 and 24");
    require(width.number == 1.0, "the rules' declaration is not changed");

    std::vector<double> lengths{};
    MaterialDeclaration shadow{};
    shadow.field = MaterialField::shadows;
    shadow.shadows.push_back(MaterialShadow{});
    shadow.em_lengths = {1.0, 2.0, 0.0, 0.0};
    material_declaration_lengths(shadow, lengths);
    require(lengths.size() == 4U && em_lengths_fit(shadow), "a shadow has four lengths");
    add_material_em(shadow, 10.0);
    require(shadow.shadows[0].offset == Point{10.0, 22.0}, "add_material_em adds em times each coefficient");
    shadow.em_lengths = {1.0};
    require(!em_lengths_fit(shadow), "coefficients that do not fit are refused");
}

// Whether compiling `text` reports `code` with `detail`.
bool refused_with(std::string_view text, DiagnosticCode code, std::string_view detail) {
    const Compilation compiled = compile(text);
    for (const ThemeDiagnostic& diagnostic : compiled.diagnostics) {
        if (diagnostic.code == code && diagnostic.detail == detail) return true;
    }
    for (const ThemeDiagnostic& diagnostic : compiled.diagnostics) {
        std::cerr << "  got " << diagnostic_code_name(diagnostic.code) << " (" << diagnostic.detail << ")\n";
    }
    return false;
}

void test_em_motion() {
    const ThemeRule moved = only_rule("#a { translate: 1em 2px }");
    require(moved.transitions.size() == 2U && moved.transitions[0].states.size() == 1U &&
                moved.transitions[0].states[0].target == 0.0 && moved.transitions[0].states[0].target_em == 1.0 &&
                moved.transitions[1].states[0].target == 2.0 && moved.transitions[1].states[0].target_em == 0.0,
            "a translate in em is 0 px and 1 em on its axis");
    const ThemeRule amplitude = only_rule("#a { primitive: bulge; amplitude: 0.5em }");
    require(amplitude.declarations.size() == 2U && amplitude.declarations[1].number == 0.0 &&
                amplitude.declarations[1].number_em == 0.5,
            "a number parameter in em");
    require(refused_with("@keyframes k { from { translate: 1em } } #a { animation: k 1s }",
                         DiagnosticCode::value_form_not_admitted, "an em length in @keyframes"),
            "a keyframe in em is E405");
    require(refused_with("@keyframes k { 50% { amplitude: 0.5px } } "
                         "#a { primitive: bulge; amplitude: 1em; animation: k 1s linear infinite }",
                         DiagnosticCode::value_form_not_admitted, "an em length in a looped animation"),
            "a loop's own value in em is E405");

    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeRuleSheet sheet{};
    ThemeRule rule{};
    rule.selector.stable_id = "a";
    PropertyTransition machine{};
    machine.property = std::string(opacity_property);
    PresentationState state{};
    state.target = 0.5;
    state.target_em = 0.1;
    machine.states.push_back(state);
    machine.curve = MotionCurve::timed(FrameInterval{}, MotionEasing::linear);
    rule.transitions.push_back(machine);
    sheet.rules.push_back(rule);
    const CompiledThemeRules refused = CompiledThemeRules::compile(sheet, registry);
    require(!refused.diagnostics().empty() &&
                refused.diagnostics()[0].kind == RuleDiagnosticKind::transition_invalid,
            "the engine takes em only on translate and parameter machines");
}

const MaterialDeclaration* only_material(const ThemeRule& rule) {
    if (rule.materials.size() != 1U) return nullptr;
    return &rule.materials[0];
}

void test_current_colour_records() {
    const ThemeRule bare = only_rule("#a { box-shadow: 1px 1px }");
    const MaterialDeclaration* shadow = only_material(bare);
    require(shadow != nullptr && (*shadow).shadows.size() == 1U &&
                (*shadow).current_colours == std::vector<std::uint16_t>{1U},
            "a shadow without a colour takes the current colour");
    const ThemeRule two = only_rule("#a { box-shadow: 1px 1px #102030, 2px 2px currentcolor }");
    const MaterialDeclaration* written = only_material(two);
    // The current shadow's colour follows the declaration's colour, in the
    // order the shadows are kept.
    const bool second_last = written != nullptr && (*written).shadows.size() == 2U &&
                             (*written).shadows[1].offset == Point{2.0, 2.0};
    const std::uint16_t position = second_last ? 2U : 1U;
    require(written != nullptr && (*written).shadows.size() == 2U &&
                (*written).current_colours == std::vector<std::uint16_t>{position},
            "a written currentcolor, and not a written colour");
    const ThemeRule glow = only_rule("#a { glow: 2px; filter: drop-shadow(1px 1px) }");
    const MaterialDeclaration* glows = material_of(glow, MaterialField::glows);
    const MaterialDeclaration* drop = material_of(glow, MaterialField::drop_shadow);
    require(glows != nullptr && (*glows).current_colours == std::vector<std::uint16_t>{1U},
            "a glow without a colour");
    require(drop != nullptr && (*drop).drop_shadow && (*drop).current_colours == std::vector<std::uint16_t>{1U},
            "a drop shadow without a colour");
    const ThemeRule background = only_rule("#a { background-color: currentcolor }");
    bool current_fill = false;
    for (const MaterialDeclaration& declaration : background.materials) {
        if (declaration.current_colours == std::vector<std::uint16_t>{0U}) current_fill = true;
    }
    require(current_fill, "a background in the current colour");
    const ThemeRule gradient = only_rule("#a { background-image: linear-gradient(currentcolor, #102030) }");
    const MaterialDeclaration* fills = material_of(gradient, MaterialField::fills);
    require(fills != nullptr && (*fills).gradients.size() == 1U &&
                (*fills).current_colours == std::vector<std::uint16_t>{1U},
            "a gradient's stop in the current colour stays authored");
    const ThemeRule coloured = only_rule("#a { box-shadow: 1px 1px #102030 }");
    const MaterialDeclaration* plain = only_material(coloured);
    require(plain != nullptr && (*plain).current_colours.empty(), "a written colour is not current");

    const ThemeRule inherit = only_rule("#a { color: inherit }");
    const ThemeRule current = only_rule("#a { color: currentColor }");
    const RecipeDeclaration* inherited = recipe_of(inherit, RecipeField::text);
    const RecipeDeclaration* currently = recipe_of(current, RecipeField::text);
    require(inherited != nullptr && (*inherited).inherit && currently != nullptr && (*currently).inherit,
            "color: inherit, and color: currentcolor, which CSS reads as it");
    const ThemeRule ring = only_rule("#a { outline: 1px solid currentcolor; text-shadow: 1px 1px }");
    const RecipeDeclaration* outline = recipe_of(ring, RecipeField::outline_colour);
    const RecipeDeclaration* text_shadow = recipe_of(ring, RecipeField::text_shadow);
    require(outline != nullptr && (*outline).current_colour && text_shadow != nullptr &&
                (*text_shadow).current_colour,
            "the ring and the text shadow in the current colour");
}

void test_current_colour_assembly() {
    MaterialDeclaration shadow{};
    shadow.field = MaterialField::shadows;
    shadow.shadows.push_back(MaterialShadow{});
    shadow.shadows[0].offset = Point{1.0, 1.0};
    shadow.current_colours = {1U};
    require(current_colours_fit(shadow), "the shadow's colour is position 1");
    std::array<const MaterialDeclaration*, material_field_count> fields{};
    fields[static_cast<std::size_t>(MaterialField::shadows)] = &shadow;
    require(material_fields_read_current_colour(fields), "a current colour reads the text colour");
    const Color teal = Color::rgba(0, 128, 128, 255);
    SurfaceMaterial floor{};
    SurfaceMaterial assembled{};
    assemble_material(floor, fields, Size{40.0, 20.0}, MaterialAssemblyContext{0.0, teal}, assembled);
    require(assembled.shadows.size() == 1U && assembled.shadows[0].color == teal,
            "the shadow takes the part's text colour when assembled");
    shadow.current_colours = {2U};
    require(!current_colours_fit(shadow), "a position past the colours is refused");
}

// -- windows -------------------------------------------------------------------

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
};

std::shared_ptr<const Theme> theme_from(std::string_view text) {
    const Compilation compiled = compile(text);
    require(compiled_clean(compiled), std::string("the window's sheet compiles: ") + std::string(text));
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "theme-em-font-css-test";
    (*definition).rules = compiled.sheet;
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    require((*theme).engine().rules().diagnostics().empty(), "the theme compiles cleanly");
    return theme;
}

double focus_width(const Window& window, const Pad& pad) {
    const ResolvedControlRecipe resolved = window.theme().resolve(pad, ControlVisualRole::button, ControlVisualContext{});
    return resolved.recipe().focus_width;
}

Color text_of(const Window& window, const Control& control) {
    const ResolvedControlRecipe resolved =
        window.theme().resolve(control, ControlVisualRole::button, ControlVisualContext{});
    return resolved.recipe().text;
}

Color ring_of(const Window& window, const Control& control) {
    const ResolvedControlRecipe resolved =
        window.theme().resolve(control, ControlVisualRole::button, ControlVisualContext{});
    return resolved.recipe().focus_ring;
}

void test_inherited_colour() {
    const std::shared_ptr<const Theme> theme =
        theme_from(":root { signal-on-paper: #a01010 on #ffffff } #root { color: role(signal-on-paper) } #pad { color: inherit } "
                   "#inner { color: currentcolor; outline: 1px solid currentcolor } "
                   "#plain { outline: 1px solid currentcolor }");
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Panel> pad = make_control<Panel>(StableId("pad"));
    const std::shared_ptr<Pad> inner = make_control<Pad>(StableId("inner"));
    const std::shared_ptr<Pad> plain = make_control<Pad>(StableId("plain"));
    (*root).add_child(pad);
    (*pad).add_child(inner);
    (*root).add_child(plain);
    Window window(root, Size{400.0, 300.0});
    window.set_theme(theme);
    const Color authored = text_of(window, *root);
    require(!(authored == text_of(window, *plain)), "the root's colour is not the floor's");
    require(text_of(window, *pad) == authored, "inherit takes the parent control's colour");
    require(text_of(window, *inner) == authored, "through two controls");
    require(ring_of(window, *inner) == authored, "currentcolor is the control's text colour");
    require(ring_of(window, *plain) == text_of(window, *plain), "or the floor's");
}

void test_em_at_two_text_scales() {
    const std::shared_ptr<const Theme> theme =
        theme_from("#root { font: title } #pad { outline: 0.5em solid; font: heading } "
                   "#inner { outline: 0.5em solid; font: inherit } #plain { outline: 0.5em solid }");
    const ThemeTypographyTokens& tokens = (*theme).structure().typography;
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    const std::shared_ptr<Pad> inner = make_control<Pad>(StableId("inner"));
    const std::shared_ptr<Pad> plain = make_control<Pad>(StableId("plain"));
    (*root).add_child(pad);
    (*root).add_child(inner);
    (*root).add_child(plain);
    Window window(root, Size{400.0, 300.0});
    window.set_theme(theme);
    require(resolved_text_role(*pad, FontDeclaration{TextRole::heading, false}) == TextRole::heading &&
                resolved_text_role(*inner, FontDeclaration{TextRole::ui, true}) == TextRole::title,
            "inherit takes the parent control's role");
    require(text_role_em(*pad, TextRole::heading) == tokens.heading.size, "a role's em is its token's size");
    require(focus_width(window, *pad) == 0.5 * tokens.heading.size, "0.5em of the heading role");
    require(focus_width(window, *inner) == 0.5 * tokens.title.size, "0.5em of the inherited title role");
    require(focus_width(window, *plain) == 0.5 * tokens.control.size, "a control with no font has the ui role");

    PresentationSettings settings = window.presentation_settings();
    settings.text_scale = 2.0;
    window.set_presentation_settings(settings);
    require(text_role_em(*pad, TextRole::heading) == 2.0 * tokens.heading.size, "the text scale scales the em");
    require(focus_width(window, *pad) == tokens.heading.size, "at text scale 2 the width doubles");
    require(focus_width(window, *plain) == tokens.control.size, "for every role");
}

} // namespace

int main() {
    test_font_lowering();
    test_em_records();
    test_em_assembly();
    test_em_motion();
    test_current_colour_records();
    test_current_colour_assembly();
    test_em_at_two_text_scales();
    test_inherited_colour();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme em and font css tests passed\n";
    return 0;
}
