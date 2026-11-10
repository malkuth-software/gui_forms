// Colour roles (stage T3, phase P8b, engine change EC4): a theme's `:root`
// palette declares the design language's colour roles, `role()` reads
// them, and each colour a sheet declares remembers its role, or, for a
// literal, only its position, so that forced high contrast can replace it
// by meaning. Gate: role and literal fixtures under forced high contrast,
// the `literal_colour_unremappable` warning, undefined roles, and one
// palette switching light and dark.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
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

constexpr std::string_view palette =
    ":root { ink-on-paper: #102030 on #f0f0e0; disabled-ink-on-paper: #808080 on #f0f0e0; focus: #2672b9; "
    "chrome: #d4d0c8; --gap: 2px } ";

ColourOrigin role_origin(ColourRole role, ColourRoleSide side) {
    ColourOrigin origin{};
    origin.source = ColourSource::role;
    origin.role = role;
    origin.side = side;
    return origin;
}

ColourOrigin literal_origin() {
    ColourOrigin origin{};
    origin.source = ColourSource::literal;
    return origin;
}

// The rule's declaration of a material field, or null.
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

std::size_t count_code(const Compilation& compilation, DiagnosticCode code) {
    std::size_t count = 0U;
    for (const ThemeDiagnostic& diagnostic : compilation.diagnostics) {
        if (diagnostic.code == code) count = count + 1U;
    }
    return count;
}

// -- the vocabulary ------------------------------------------------------------

void test_role_names() {
    require(colour_role_count == 26U, "17 pairs and 9 single roles");
    std::size_t paired = 0U;
    for (std::size_t index = 0U; index < colour_role_count; ++index) {
        const ColourRole role = static_cast<ColourRole>(index);
        const std::string_view name = colour_role_name(role);
        require(colour_role_from_name(name) == role, std::string("the name round-trips: ") + std::string(name));
        if (colour_role_paired(role)) paired = paired + 1U;
    }
    require(paired == 17U, "the pairs are the design language's");
    require(colour_role_name(ColourRole::ink_on_paper) == "ink-on-paper" &&
                colour_role_name(ColourRole::command_link_ink_on_paper) == "command-link-ink-on-paper" &&
                colour_role_name(ColourRole::selection_line) == "selection-line",
            "roles are named in kebab case");
    require(!colour_role_from_name("Ink-On-Paper") && !colour_role_from_name("ink_on_paper") &&
                !colour_role_from_name(""),
            "a name matches exactly");
    require(!colour_role_paired(ColourRole::focus) && !colour_role_paired(ColourRole::chrome),
            "focus and the relief ramp are single colours");
}

// -- reading roles -------------------------------------------------------------

void test_reading_roles() {
    const Compilation compiled = compile(std::string(palette) +
                                         "#a { color: role(ink-on-paper); background: role(ink-on-paper); "
                                         "border-color: role(ink-on-paper foreground); outline-color: role(focus); "
                                         "box-shadow: 0 1px 2px role(chrome) }");
    require(compiled_clean(compiled) && compiled.sheet.rules.size() == 1U, "roles compile without a warning");
    if (compiled.sheet.rules.size() != 1U) return;
    const ThemeRule& rule = compiled.sheet.rules[0];
    const RecipeDeclaration* text = recipe_of(rule, RecipeField::text);
    require(text != nullptr && (*text).colour == Color::rgba(0x10, 0x20, 0x30) &&
                (*text).origin == role_origin(ColourRole::ink_on_paper, ColourRoleSide::foreground),
            "color takes a pair's foreground and remembers it");
    const MaterialDeclaration* plate = material_of(rule, MaterialField::background_colour);
    require(plate != nullptr && (*plate).colour == Color::rgba(0xf0, 0xf0, 0xe0) &&
                (*plate).origin == role_origin(ColourRole::ink_on_paper, ColourRoleSide::background),
            "background takes its background");
    const MaterialDeclaration* edge = material_of(rule, MaterialField::border_left_colour);
    require(edge != nullptr && (*edge).colour == Color::rgba(0x10, 0x20, 0x30) &&
                (*edge).origin == role_origin(ColourRole::ink_on_paper, ColourRoleSide::foreground),
            "a side picks one anywhere, on every edge");
    const RecipeDeclaration* ring = recipe_of(rule, RecipeField::outline_colour);
    require(ring != nullptr && (*ring).colour == Color::rgba(0x26, 0x72, 0xb9) &&
                (*ring).origin == role_origin(ColourRole::focus, ColourRoleSide::foreground),
            "a single role is read by its name");
    const MaterialDeclaration* shadows = material_of(rule, MaterialField::shadows);
    require(shadows != nullptr && (*shadows).origin == ColourOrigin{},
            "a field with no colour position records no origin");
    const MaterialDeclaration* style = material_of(rule, MaterialField::border_left_style);
    require(style == nullptr, "border-color sets only the colours");

    const Compilation mixed = compile(std::string(palette) +
                                      "#a { color: color-mix(in srgb, role(ink-on-paper) 50%, role(focus)) }");
    require(compiled_clean(mixed) && mixed.sheet.rules.size() == 1U, "a mix of roles compiles cleanly");
    if (mixed.sheet.rules.size() == 1U) {
        const RecipeDeclaration* mix = recipe_of(mixed.sheet.rules[0], RecipeField::text);
        require(mix != nullptr && (*mix).origin == role_origin(ColourRole::ink_on_paper, ColourRoleSide::foreground),
                "a mix of roles keeps its first role");
    }

    const Compilation parameter =
        compile(std::string(palette) + "#a { primitive: bulge; light-colour: role(focus) }");
    require(compiled_clean(parameter) && parameter.sheet.rules.size() == 1U, "a colour parameter reads a role");
    if (parameter.sheet.rules.size() == 1U) {
        const std::vector<RuleDeclaration>& declarations = parameter.sheet.rules[0].declarations;
        require(declarations.size() == 2U && declarations[1].colour == Color::rgba(0x26, 0x72, 0xb9) &&
                    declarations[1].origin == role_origin(ColourRole::focus, ColourRoleSide::foreground),
                "and remembers it");
    }

    const Compilation substituted =
        compile(":root { --ink: #405060; ink-on-paper: var(--ink) on white } #a { color: role(ink-on-paper) }");
    require(compiled_clean(substituted) && substituted.sheet.rules.size() == 1U &&
                recipe_of(substituted.sheet.rules[0], RecipeField::text) != nullptr &&
                (*recipe_of(substituted.sheet.rules[0], RecipeField::text)).colour == Color::rgba(0x40, 0x50, 0x60),
            "a palette reads custom properties and named colours");

    const Compilation later = compile(std::string(palette) + "#a { color: role(ink-on-paper) } "
                                                             ":root { ink-on-paper: #000000 on #ffffff }");
    require(compiled_clean(later) && later.sheet.rules.size() == 1U &&
                (*recipe_of(later.sheet.rules[0], RecipeField::text)).colour == Color::rgba(0, 0, 0),
            "the palette's last declaration of a role holds, wherever the block stands");
}

// -- literals ------------------------------------------------------------------

void test_literals() {
    const Compilation text = compile("#a { color: #123456 }");
    require(text.outcome == CompileOutcome::compiled && text.diagnostics.size() == 1U &&
                text.diagnostics[0].code == DiagnosticCode::literal_colour_unremappable &&
                text.diagnostics[0].detail == "#123456" && text.diagnostics[0].property == "color",
            "a literal text colour is W201 and still applies");
    if (text.sheet.rules.size() == 1U) {
        const RecipeDeclaration* written = recipe_of(text.sheet.rules[0], RecipeField::text);
        require(written != nullptr && (*written).colour == Color::rgba(0x12, 0x34, 0x56) &&
                    (*written).origin == literal_origin(),
                "it records a literal");
    }
    const Compilation edge = compile("#a { border: 1px solid red }");
    require(edge.outcome == CompileOutcome::compiled &&
                count_code(edge, DiagnosticCode::literal_colour_unremappable) == 1U &&
                edge.diagnostics.size() == 1U && edge.diagnostics[0].detail == "red",
            "a literal edge colour is W201, once for the declaration");
    const Compilation ring = compile("#a { outline: 2px solid rgb(0 0 255) }");
    require(ring.outcome == CompileOutcome::compiled &&
                count_code(ring, DiagnosticCode::literal_colour_unremappable) == 1U,
            "a literal ring colour is W201");
    const Compilation fill = compile("#a { background: #ffffff; background-image: linear-gradient(red, blue) }");
    require(compiled_clean(fill), "a literal fill is no warning: forced high contrast takes the plate");
    if (fill.sheet.rules.size() == 1U) {
        const MaterialDeclaration* plate = material_of(fill.sheet.rules[0], MaterialField::background_colour);
        require(plate != nullptr && (*plate).origin == literal_origin(), "a literal fill records a literal");
    }
    const Compilation half =
        compile(std::string(palette) + "#a { color: color-mix(in srgb, role(ink-on-paper), red) }");
    require(half.outcome == CompileOutcome::compiled && half.diagnostics.size() == 1U &&
                half.diagnostics[0].code == DiagnosticCode::literal_colour_unremappable,
            "a mix with a literal is a literal");
    const Compilation parameter = compile("#a { primitive: bulge; light-colour: red }");
    require(compiled_clean(parameter) && parameter.sheet.rules.size() == 1U &&
                parameter.sheet.rules[0].declarations[1].origin == literal_origin(),
            "a literal parameter records a literal, without a position to warn of");
    const Compilation initial = compile("#a { color: initial; border-color: initial }");
    require(compiled_clean(initial) && initial.sheet.rules.size() == 1U &&
                (*recipe_of(initial.sheet.rules[0], RecipeField::text)).origin == ColourOrigin{},
            "initial records no origin");
}

// -- refusals ------------------------------------------------------------------

struct Refusal final {
    std::string_view sheet;
    DiagnosticCode code;
    std::string_view detail;
};

void test_refusals() {
    const std::array<Refusal, 14> refusals{{
        {"#a { color: role(ink-on-paper) }", DiagnosticCode::colour_role_undefined, "ink-on-paper"},
        {":root { focus: blue } #a { color: role(ink-on-paper) }", DiagnosticCode::colour_role_undefined,
         "ink-on-paper"},
        {"@media (prefers-color-scheme: dark) { :root { ink-on-paper: white on black } } "
         "#a { color: role(ink-on-paper) }",
         DiagnosticCode::colour_role_undefined, "ink-on-paper"},
        {":root { focus: blue } #a { color: role(ink-on-cardboard) }", DiagnosticCode::keyword_unknown,
         "ink-on-cardboard"},
        {":root { ink-on-paper: black on white } #a { color: role(ink-on-paper middle) }",
         DiagnosticCode::keyword_unknown, "middle"},
        {":root { ink-on-paper: black on white } #a { border-color: role(ink-on-paper) }",
         DiagnosticCode::value_form_not_admitted, "role() of a pair without its side"},
        {":root { focus: blue } #a { outline-color: role(focus foreground) }",
         DiagnosticCode::value_form_not_admitted, "a side of a single colour role"},
        {":root { focus: blue } #a { color: role() }", DiagnosticCode::value_syntax,
         "role(<role> [foreground | background])"},
        {":root { focus: blue } #a { color: role(focus foreground background) }", DiagnosticCode::value_syntax,
         "role(<role> [foreground | background])"},
        {":root { focus: blue; chrome: role(focus) }", DiagnosticCode::value_form_not_admitted, "role() in a palette"},
        {":root { ink-on-paper: black }", DiagnosticCode::value_wrong_kind, "<colour> on <colour>"},
        {":root { ink-on-paper: black over white }", DiagnosticCode::value_wrong_kind, "<colour> on <colour>"},
        {":root { focus: blue on white }", DiagnosticCode::value_wrong_kind, "one colour"},
        {":root { focus: blue !important }", DiagnosticCode::value_form_not_admitted, "!important in the palette"},
    }};
    for (const Refusal& entry : refusals) {
        const Compilation compiled = compile(entry.sheet);
        const bool refused = compiled.outcome == CompileOutcome::refused && compiled.sheet.rules.empty() &&
                             !compiled.diagnostics.empty();
        require(refused, std::string("refused: ") + std::string(entry.sheet));
        if (!refused) continue;
        const ThemeDiagnostic& last = compiled.diagnostics.back();
        require(last.code == entry.code && last.detail == entry.detail,
                std::string("refused as ") + std::string(diagnostic_code_name(entry.code)) + " (" +
                    std::string(entry.detail) + "): " + std::string(entry.sheet) + " gave " +
                    std::string(diagnostic_code_name(last.code)) + " (" + last.detail + ")");
    }
    require(diagnostic_code_name(DiagnosticCode::colour_role_undefined) == "colour_role_undefined" &&
                diagnostic_code_name(DiagnosticCode::literal_colour_unremappable) == "literal_colour_unremappable" &&
                theme_language::diagnostic_code_label(DiagnosticCode::colour_role_undefined) == "E415" &&
                theme_language::diagnostic_code_label(DiagnosticCode::literal_colour_unremappable) == "W201",
            "the codes are E415 colour_role_undefined and W201 literal_colour_unremappable");

    const Compilation stray = compile(":root { opacity: 0.5; focus: blue } #a { outline-color: role(focus) }");
    require(stray.outcome == CompileOutcome::compiled && stray.diagnostics.size() == 1U &&
                stray.diagnostics[0].code == DiagnosticCode::property_not_admitted &&
                stray.diagnostics[0].detail == "opacity in the palette" && stray.sheet.rules.size() == 1U,
            "a palette's other property is W107 and the palette holds");
}

// -- conditional palettes ------------------------------------------------------

MediaCondition dark() {
    MediaCondition condition{};
    condition.dark_scheme = true;
    return condition;
}

MediaCondition light() {
    MediaCondition condition{};
    condition.dark_scheme = false;
    return condition;
}

MediaCondition forced() {
    MediaCondition condition{};
    condition.forced_colours = true;
    return condition;
}

Color text_of(const ThemeRule& rule) {
    const RecipeDeclaration* text = recipe_of(rule, RecipeField::text);
    return text == nullptr ? Color{} : (*text).colour;
}

void test_conditional_palettes() {
    constexpr std::string_view two =
        ":root { color-scheme: light dark; ink-on-paper: #111111 on #ffffff } "
        "@media (prefers-color-scheme: dark) { :root { ink-on-paper: #eeeeee on #000000 } } ";
    const Compilation switched = compile(std::string(two) + "#a { color: role(ink-on-paper) } #b { opacity: 0.5 }");
    require(compiled_clean(switched) && switched.sheet.rules.size() == 3U, "a rule reading a varying role is copied");
    if (switched.sheet.rules.size() == 3U) {
        const std::vector<ThemeRule>& rules = switched.sheet.rules;
        require(media_condition_unconditional(rules[0].media) && text_of(rules[0]) == Color::rgba(17, 17, 17) &&
                    rules[1].media == dark() && text_of(rules[1]) == Color::rgba(238, 238, 238) &&
                    rules[1].selector.stable_id == "a" && rules[2].selector.stable_id == "b",
                "the dark copy follows the rule, before the next");
    }

    const Compilation joined = compile(std::string(two) +
                                       "@media (forced-colors: active) { #a { color: role(ink-on-paper) } }");
    require(compiled_clean(joined) && joined.sheet.rules.size() == 2U, "a conditional rule is copied too");
    if (joined.sheet.rules.size() == 2U) {
        MediaCondition both = forced();
        both.dark_scheme = true;
        require(joined.sheet.rules[0].media == forced() && joined.sheet.rules[1].media == both,
                "its copy holds where both conditions do");
    }
    const Compilation contradicted =
        compile(std::string(two) + "@media (prefers-color-scheme: light) { #a { color: role(ink-on-paper) } }");
    require(compiled_clean(contradicted) && contradicted.sheet.rules.size() == 1U &&
                contradicted.sheet.rules[0].media == light(),
            "a copy no window can meet is skipped");

    const Compilation crossed = compile(
        ":root { ink-on-paper: #111111 on #ffffff; focus: #0000ff } "
        "@media (prefers-color-scheme: dark) { :root { ink-on-paper: #eeeeee on #000000 } } "
        "@media (prefers-color-scheme: light) { :root { focus: #00ff00 } } "
        "#a { color: role(ink-on-paper); outline-color: role(focus) }");
    require(compiled_clean(crossed) && crossed.sheet.rules.size() == 3U,
            "two varying roles copy each holding combination once");
    if (crossed.sheet.rules.size() == 3U) {
        const std::vector<ThemeRule>& rules = crossed.sheet.rules;
        require(media_condition_unconditional(rules[0].media) && rules[1].media == light() &&
                    rules[2].media == dark(),
                "in order, the contradictory combination skipped");
    }

    const Compilation replaced = compile(std::string(two) + ":root { ink-on-paper: #222222 on #ffffff } "
                                                           "#a { color: role(ink-on-paper) }");
    require(compiled_clean(replaced) && replaced.sheet.rules.size() == 1U &&
                text_of(replaced.sheet.rules[0]) == Color::rgba(34, 34, 34),
            "a later unconditional declaration replaces the conditional ones");

    const Compilation listed = compile(
        ":root { color-scheme: light dark; ink-on-paper: #111111 on #ffffff } "
        "@media (prefers-color-scheme: dark), (forced-colors: active) { :root { ink-on-paper: #eeeeee on #000000 } } "
        "#a { color: role(ink-on-paper) }");
    require(compiled_clean(listed) && listed.sheet.rules.size() == 3U && listed.sheet.rules[1].media == dark() &&
                listed.sheet.rules[2].media == forced(),
            "each query of a palette's list is a variant");

    const Compilation once = compile(std::string(two) + "#a { color: role(ink-on-paper); border-color: red }");
    require(once.outcome == CompileOutcome::compiled && once.sheet.rules.size() == 2U &&
                once.diagnostics.size() == 1U &&
                once.diagnostics[0].code == DiagnosticCode::literal_colour_unremappable,
            "a copied rule's warnings are reported once");
    const Compilation repeated = compile(
        "@media (prefers-color-scheme: dark), (forced-colors: active) { #a { color: red } }");
    require(repeated.outcome == CompileOutcome::compiled && repeated.sheet.rules.size() == 2U &&
                repeated.diagnostics.size() == 1U,
            "a block's warnings are reported once for its query list");
}

// -- forced high contrast ------------------------------------------------------

void test_high_contrast_remap() {
    const Color black = Color::rgba(0, 0, 0);
    const Color white = Color::rgba(255, 255, 255);
    const Color cyan = Color::rgba(0, 255, 255);
    const Color yellow = Color::rgba(255, 255, 0);
    const Color grey_text = Color::rgba(160, 160, 160);
    const Color grey_edge = Color::rgba(128, 128, 128);
    const Color red_edge = Color::rgba(255, 96, 96);
    const ControlSurfaceState normal = ControlSurfaceState::normal;

    // The literal fixture: a position, and only that.
    const Compilation literal = compile("#a { color: #123456; background: #abcdef; border-color: #345678 }");
    require(literal.outcome == CompileOutcome::compiled && literal.sheet.rules.size() == 1U,
            "the literal fixture compiles");
    if (literal.sheet.rules.size() == 1U) {
        const ThemeRule& rule = literal.sheet.rules[0];
        const ColourOrigin text = (*recipe_of(rule, RecipeField::text)).origin;
        const ColourOrigin plate = (*material_of(rule, MaterialField::background_colour)).origin;
        const ColourOrigin edge = (*material_of(rule, MaterialField::border_top_colour)).origin;
        require(high_contrast_colour(text, ColourPosition::text, normal, false) == white &&
                    high_contrast_colour(plate, ColourPosition::fill, normal, false) == black &&
                    high_contrast_colour(edge, ColourPosition::edge, normal, false) == white,
                "a literal maps by its position");
        require(high_contrast_colour(edge, ColourPosition::edge, ControlSurfaceState::hot, false) == cyan &&
                    high_contrast_colour(text, ColourPosition::text, ControlSurfaceState::pressed, false) == black &&
                    high_contrast_colour(plate, ColourPosition::fill, ControlSurfaceState::pressed, false) == white &&
                    high_contrast_colour(edge, ColourPosition::edge, ControlSurfaceState::pressed, false) == yellow,
                "by the surface state's colours");
        require(high_contrast_colour(text, ColourPosition::text, normal, true) == black &&
                    high_contrast_colour(plate, ColourPosition::fill, normal, true) == white,
                "a selected part's plate is white under black text");
        require(high_contrast_colour(text, ColourPosition::none, normal, false) == black,
                "a colour with no position takes the plate");
    }

    // The role fixture: the role's meaning outranks the position.
    const Compilation roles = compile(
        ":root { ink-on-paper: #102030 on #f0f0e0; disabled-ink-on-paper: #808080 on #f0f0e0; "
        "invalid-ink-on-tint: #900000 on #ffe0e0; selection-ink-on-selection: #ffffff on #3060a0; focus: #2672b9; "
        "chrome-low: #808080; dark-line: #404040 } "
        "#a { color: role(disabled-ink-on-paper); background: role(selection-ink-on-selection); "
        "border-color: role(invalid-ink-on-tint foreground); outline-color: role(focus) } "
        "#b { color: role(selection-ink-on-selection); background: role(ink-on-paper); "
        "border-top-color: role(chrome-low); border-bottom-color: role(dark-line); "
        "border-left-color: role(ink-on-paper foreground) }");
    require(compiled_clean(roles) && roles.sheet.rules.size() == 2U, "the role fixture compiles cleanly");
    if (roles.sheet.rules.size() != 2U) return;
    const ThemeRule& first = roles.sheet.rules[0];
    const ThemeRule& second = roles.sheet.rules[1];
    const ColourOrigin disabled = (*recipe_of(first, RecipeField::text)).origin;
    const ColourOrigin selection_plate = (*material_of(first, MaterialField::background_colour)).origin;
    const ColourOrigin invalid = (*material_of(first, MaterialField::border_top_colour)).origin;
    const ColourOrigin focus = (*recipe_of(first, RecipeField::outline_colour)).origin;
    require(high_contrast_colour(disabled, ColourPosition::text, normal, false) == grey_text &&
                high_contrast_colour(disabled, ColourPosition::edge, normal, false) == grey_edge,
            "a disabled role takes the disabled grey in any state");
    require(high_contrast_colour(invalid, ColourPosition::edge, normal, false) == red_edge,
            "an invalid role takes the invalid edge");
    require(high_contrast_colour(focus, ColourPosition::edge, normal, false) == cyan &&
                high_contrast_colour(focus, ColourPosition::edge, ControlSurfaceState::pressed, false) ==
                    Color::rgba(0, 0, 0),
            "focus takes the cue colour, and the selection ink on a pressed (inverse) plate");
    require(high_contrast_colour(selection_plate, ColourPosition::fill, normal, false) == white,
            "a selection background is the selected plate");
    const ColourOrigin selection_text = (*recipe_of(second, RecipeField::text)).origin;
    const ColourOrigin ink_plate = (*material_of(second, MaterialField::background_colour)).origin;
    const ColourOrigin relief = (*material_of(second, MaterialField::border_top_colour)).origin;
    const ColourOrigin line = (*material_of(second, MaterialField::border_bottom_colour)).origin;
    const ColourOrigin ink_edge = (*material_of(second, MaterialField::border_left_colour)).origin;
    require(high_contrast_colour(selection_text, ColourPosition::text, normal, false) == black,
            "selection ink is black on the white plate");
    require(high_contrast_colour(ink_plate, ColourPosition::fill, normal, false) == black &&
                high_contrast_colour(ink_edge, ColourPosition::edge, ControlSurfaceState::hot, false) == cyan,
            "an ordinary pair maps by side: background to plate, foreground at an edge to the edge");
    require(high_contrast_colour(relief, ColourPosition::edge, normal, false) == black &&
                high_contrast_colour(line, ColourPosition::edge, ControlSurfaceState::invalid, false) == red_edge,
            "the relief ramp takes the plate and a line the edge, even at an edge position");
}

// -- windows -------------------------------------------------------------------

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
};

void test_light_and_dark_palette() {
    const Compilation compiled = compile(
        ":root { color-scheme: light dark; ink-on-paper: #111111 on #ffffff } "
        "@media (prefers-color-scheme: dark) { :root { ink-on-paper: #eeeeee on #000000 } } "
        "#pad { color: role(ink-on-paper) }");
    require(compiled_clean(compiled), "the palette sheet compiles");
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "theme-colour-role-css-test";
    (*definition).rules = compiled.sheet;
    const std::shared_ptr<const Theme> theme = Theme::create(*definition);
    require((*theme).engine().rules().diagnostics().empty(), "the theme compiles cleanly");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*pad).set_requested_bounds(Rect{100.0, 100.0, 100.0, 80.0});
    (*root).add_child(pad);
    Window window(root, Size{400.0, 300.0});
    window.set_theme(theme);
    const ResolvedControlRecipe light = window.theme().resolve(*pad, ControlVisualRole::button, ControlVisualContext{});
    const Color light_text = light.recipe().text;
    require(light_text == Color::rgba(17, 17, 17), "a light window reads the light palette");
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = ColourScheme::dark;
    window.set_presentation_settings(settings);
    const ResolvedControlRecipe dark = window.theme().resolve(*pad, ControlVisualRole::button, ControlVisualContext{});
    const Color dark_text = dark.recipe().text;
    require(dark_text == Color::rgba(238, 238, 238), "switched dark, the dark palette");
}

// -- application roles (owner request 2026-11-09) ------------------------------

constexpr std::string_view application_palette =
    ":root { color-scheme: light dark; ink-on-paper: #111111 on #ffffff; signal-on-paper: #c00000 on #ffffff; "
    "color-roles: syntax-keyword from ink-on-paper, find-mark from signal-on-paper, gutter-line from ordinary-line; "
    "syntax-keyword: #0000c0 on #fffff0 } "
    "@media (prefers-color-scheme: dark) { :root { ink-on-paper: #eeeeee on #000000; "
    "syntax-keyword: #80a0ff on #101010 } } ";

void test_application_roles() {
    const Compilation compiled =
        compile(std::string(application_palette) + "#a { color: role(syntax-keyword); "
                                                   "background: role(syntax-keyword background) }");
    require(compiled_clean(compiled), "a sheet declaring application roles compiles");
    const std::vector<ApplicationColourRole>& roles = compiled.sheet.application_roles;
    require(roles.size() == 3U, "the sheet keeps its three application roles");
    if (roles.size() == 3U) {
        require(roles[0].name == "syntax-keyword" && roles[0].fallback == ColourRole::ink_on_paper &&
                    roles[0].paired && roles[0].variants.size() == 2U,
                "a role with its own light and dark colours");
        if (roles[0].variants.size() == 2U) {
            require(media_condition_unconditional(roles[0].variants[0].media) &&
                        roles[0].variants[0].foreground == Color::rgba(0, 0, 192) &&
                        roles[0].variants[0].background == Color::rgba(255, 255, 240) &&
                        roles[0].variants[1].media == dark() &&
                        roles[0].variants[1].foreground == Color::rgba(128, 160, 255),
                    "its base, then its dark declaration");
        }
        require(roles[1].name == "find-mark" && roles[1].fallback == ColourRole::signal_on_paper &&
                    roles[1].variants.size() == 1U && roles[1].variants[0].foreground == Color::rgba(192, 0, 0),
                "a role declaring no colours takes its fallback's");
        require(roles[2].name == "gutter-line" && !roles[2].paired && roles[2].variants.empty(),
                "a single fallback the sheet leaves undeclared gives none");
    }
    require(compiled.sheet.rules.size() == 2U, "a rule reading a varying application role is copied");
    if (compiled.sheet.rules.size() == 2U) {
        const std::vector<ThemeRule>& rules = compiled.sheet.rules;
        require(text_of(rules[0]) == Color::rgba(0, 0, 192) && rules[1].media == dark() &&
                    text_of(rules[1]) == Color::rgba(128, 160, 255),
                "role() reads the application role's light and dark colours");
        const RecipeDeclaration* text = recipe_of(rules[0], RecipeField::text);
        require(text != nullptr &&
                    (*text).origin == role_origin(ColourRole::ink_on_paper, ColourRoleSide::foreground),
                "the colour remembers its fallback, which forced high contrast remaps by");
    }

    const Compilation undeclared = compile(std::string(application_palette) + "#a { color: role(gutter-line) }");
    require(undeclared.outcome == CompileOutcome::refused &&
                count_code(undeclared, DiagnosticCode::colour_role_undefined) == 1U,
            "reading a role with no colours is E415");
    const Compilation unknown = compile(":root { ink-on-paper: #111111 on #ffffff } #a { color: role(syntax-keyword) }");
    require(unknown.outcome == CompileOutcome::refused && count_code(unknown, DiagnosticCode::keyword_unknown) == 1U,
            "an undeclared application role is E406");
    const Compilation bad_fallback = compile(":root { color-roles: syntax-keyword from inky }");
    require(bad_fallback.outcome == CompileOutcome::refused &&
                count_code(bad_fallback, DiagnosticCode::keyword_unknown) == 1U,
            "a fallback that is not a built-in role is E406");
    const Compilation built_in_name = compile(":root { color-roles: focus from ink-on-paper }");
    require(built_in_name.outcome == CompileOutcome::refused &&
                count_code(built_in_name, DiagnosticCode::value_form_not_admitted) == 1U,
            "an application role may not take a built-in role's name");
    const Compilation twice = compile(":root { color-roles: a-role from focus, a-role from chrome }");
    require(twice.outcome == CompileOutcome::refused &&
                count_code(twice, DiagnosticCode::value_form_not_admitted) == 1U,
            "a role declared twice is refused");
    const Compilation shapeless = compile(":root { color-roles: a-role focus }");
    require(shapeless.outcome == CompileOutcome::refused && count_code(shapeless, DiagnosticCode::value_syntax) == 1U,
            "a declaration without `from` is E401");
    const Compilation conditional =
        compile("@media (prefers-color-scheme: dark) { :root { color-roles: a-role from focus } }");
    require(conditional.outcome == CompileOutcome::refused &&
                count_code(conditional, DiagnosticCode::value_form_not_admitted) == 1U,
            "color-roles under @media is refused");
    std::string many = ":root { color-roles: ";
    for (std::size_t index = 0U; index <= maximum_application_colour_roles; ++index) {
        if (index != 0U) many += ", ";
        many += "role-" + std::to_string(index) + " from focus";
    }
    many += " }";
    const Compilation crowded = compile(many);
    require(crowded.outcome == CompileOutcome::refused &&
                count_code(crowded, DiagnosticCode::value_form_not_admitted) == 1U,
            "more than the maximum of application roles is refused");
    require(application_colour_role_name_valid("syntax-keyword") && !application_colour_role_name_valid("Syntax") &&
                !application_colour_role_name_valid("9role") && !application_colour_role_name_valid("ink-on-paper") &&
                !application_colour_role_name_valid(""),
            "application role names: lowercase, a letter first, no built-in name");
}

void test_application_role_lookup() {
    const Compilation compiled = compile(std::string(application_palette) +
                                         "@media (forced-colors: active) { :root { find-mark: #ffff00 on #000000 } }");
    require(compiled_clean(compiled), "the application palette compiles");
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "theme-application-role-test";
    (*definition).rules = compiled.sheet;
    const std::shared_ptr<const Theme> theme = Theme::create(*definition);

    const MediaFacts light_facts{};
    MediaFacts dark_facts{};
    dark_facts.dark_scheme = true;
    MediaFacts forced_facts{};
    forced_facts.forced_colours = true;
    const std::optional<ThemeRoleColours> light = (*theme).application_role("syntax-keyword", light_facts);
    require(light && (*light).foreground == Color::rgba(0, 0, 192) && (*light).background == Color::rgba(255, 255, 240) &&
                (*light).fallback == ColourRole::ink_on_paper && (*light).paired,
            "the light colours");
    const std::optional<ThemeRoleColours> dark = (*theme).application_role("syntax-keyword", dark_facts);
    require(dark && (*dark).foreground == Color::rgba(128, 160, 255), "the dark colours");
    const std::optional<ThemeRoleColours> forced = (*theme).application_role("syntax-keyword", forced_facts);
    require(forced && (*forced).foreground == Color::rgba(255, 255, 255) && (*forced).background == Color::rgba(0, 0, 0),
            "forced colours: the fallback's high-contrast pair");
    const std::optional<ThemeRoleColours> declared = (*theme).application_role("find-mark", forced_facts);
    require(declared && (*declared).foreground == Color::rgba(255, 255, 0),
            "forced colours: the role's own forced-colors declaration wins");
    require(!(*theme).application_role("gutter-line", light_facts), "a role without colours has none");
    require(!(*theme).application_role("missing", light_facts), "an undeclared role has none");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*root).add_child(pad);
    Window window(root, Size{200.0, 100.0});
    window.set_theme(theme);
    const std::optional<ThemeRoleColours> through_control = (*pad).theme_role_colours("syntax-keyword");
    require(through_control && (*through_control).foreground == Color::rgba(0, 0, 192),
            "a control reads the role through its window's theme");
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = ColourScheme::dark;
    window.set_presentation_settings(settings);
    const std::optional<ThemeRoleColours> dark_control = (*pad).theme_role_colours("syntax-keyword");
    require(dark_control && (*dark_control).foreground == Color::rgba(128, 160, 255),
            "switched dark, the control reads the dark colours");
}

} // namespace

int main() {
    test_role_names();
    test_reading_roles();
    test_literals();
    test_refusals();
    test_conditional_palettes();
    test_high_contrast_remap();
    test_light_and_dark_palette();
    test_application_roles();
    test_application_role_lookup();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme colour role css tests passed\n";
    return 0;
}
