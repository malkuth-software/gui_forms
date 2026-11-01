// Custom properties through the cascade (stage T3, phase P12a, engine
// change EC7; owner decision of 2026-10-29): resolved when the theme loads.
// A name defined in one place is substituted where it is read. A name
// defined in the palette alone under media conditions gives its readers a
// copy for each combination of the conditions. A name some rule's selector
// redefines gives its readers a copy for each definition that may win for
// their parts, conditioned on the part's winner, which the engine finds by
// the cascade; copies whose definitions cannot win together are not made.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
using theme_language::maximum_variable_combinations;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::array<PartDeclaration, 2> button_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
    {.name = "caption", .role = PartRole::caption, .composed_from = {}, .states = {}},
}};
constexpr std::array<PartDeclaration, 2> checkbox_parts{{
    {.name = "box", .role = PartRole::face, .composed_from = {}, .states = {}},
    {.name = "check", .role = PartRole::indicator, .composed_from = {}, .states = {}},
}};
constexpr std::array<PartDeclaration, 2> textbox_parts{{
    {.name = "well", .role = PartRole::well, .composed_from = {}, .states = {}},
    {.name = "caption", .role = PartRole::caption, .composed_from = {}, .states = {}},
}};
constexpr std::array<PartDeclaration, 3> slider_parts{{
    {.name = "track", .role = PartRole::track, .composed_from = {}, .states = {}},
    {.name = "fill", .role = PartRole::fill, .composed_from = {}, .states = {}},
    {.name = "thumb", .role = PartRole::thumb, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 5> controls{{
    {.control_id = "button", .parts = button_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "checkbox", .parts = checkbox_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "textbox", .parts = textbox_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "slider", .parts = slider_parts, .states = {}, .custom_states = {}, .item_systems = {}},
    {.control_id = "tab", .parts = button_parts, .states = {}, .custom_states = {}, .item_systems = {}},
}};

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
    input.vocabulary.controls = controls;
    ThemeCompiler compiler{};
    ThemeDiagnosticList diagnostics{};
    Compilation result{};
    result.outcome = compiler.compile(input, result.sheet, diagnostics);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) {
        result.diagnostics.push_back(diagnostic);
        std::cerr << "  " << diagnostic_code_name(diagnostic.code) << " " << diagnostic.detail << '\n';
    }
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

void require_refused(std::string_view text, DiagnosticCode code, std::string_view message) {
    const Compilation refused = compile(text);
    const bool coded = !refused.diagnostics.empty() && refused.diagnostics.back().code == code;
    require(refused.outcome == CompileOutcome::refused && coded && refused.sheet.rules.empty(), message);
}

// The rules lowered from the style rule labelled `label`: its copies.
std::size_t copies_of(const Compilation& compilation, std::string_view label) {
    std::size_t copies = 0U;
    for (const ThemeRule& rule : compilation.sheet.rules) {
        if (rule.label == label && rule.scoped_definitions.empty()) copies = copies + 1U;
    }
    return copies;
}

std::size_t amplitude_index() {
    const EnginePrimitive* const bulge = builtin_primitive_registry().find("bulge");
    const std::optional<std::size_t> found =
        bulge == nullptr ? std::optional<std::size_t>{} : find_parameter((*bulge).descriptor(), "amplitude");
    return found ? *found : 0U;
}

// What a part asks the cascade.
struct Part final {
    std::string_view control_type{};
    std::string_view stable_id{"a"};
    std::span<const std::string> classes{};
    std::string_view part{};
    PartRole role{PartRole::face};
    bool dark{};
    bool reduced_motion{};
};

CompiledThemeRules::Resolution resolve(const CompiledThemeRules& rules, const Part& part) {
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = part.control_type;
    facts.stable_id = part.stable_id;
    facts.classes = part.classes;
    facts.part = part.part;
    facts.role = part.role;
    facts.media.dark_scheme = part.dark;
    facts.media.reduced_motion = part.reduced_motion;
    return rules.resolve(facts);
}

// The amplitude the part `a` resolves to under `text`, or -1 without a
// primitive.
double amplitude(std::string_view text, const Part& part) {
    const Compilation compiled = compile(text);
    if (compiled.outcome != CompileOutcome::compiled) return -2.0;
    const CompiledThemeRules rules = CompiledThemeRules::compile(compiled.sheet, builtin_primitive_registry());
    const CompiledThemeRules::Resolution resolution = resolve(rules, part);
    if (resolution.outcome != CompiledThemeRules::Outcome::primitive) return -1.0;
    return resolution.parameters.values[amplitude_index()].number;
}

const std::array<std::string, 1> loud{{"loud"}};
const std::array<std::string, 1> quiet{{"quiet"}};
const std::array<std::string, 2> loud_and_quiet{{"loud", "quiet"}};

// -- a name defined once -----------------------------------------------------------

void test_defined_once() {
    const std::string_view palette = ":root { --lift: 0.4 } #a { primitive: bulge; amplitude: var(--lift) }";
    const Compilation once = compile(palette);
    require(compiled_clean(once) && once.sheet.rules.size() == 1U && once.sheet.rules[0].scoped_conditions.empty(),
            "a name defined once is substituted: one rule, no conditions");
    require(amplitude(palette, Part{}) == 0.4, "and read as written");
    // Defined once in a rule's scope, it is read everywhere, as before P12.
    const std::string_view scoped = "#b { --lift: 0.4 } #a { primitive: bulge; amplitude: var(--lift) }";
    const Compilation global = compile(scoped);
    require(compiled_clean(global) && global.sheet.rules.size() == 1U, "a scoped name defined once stays global");
    require(amplitude(scoped, Part{}) == 0.4, "and is read by every rule");
    // Defined twice in the palette, the later definition holds everywhere.
    const std::string_view twice = ":root { --lift: 0.4 } :root { --lift: 0.6 } #a { primitive: bulge; "
                                   "amplitude: var(--lift) }";
    const Compilation later = compile(twice);
    require(compiled_clean(later) && later.sheet.rules.size() == 1U, "the palette's later definition replaces");
    require(amplitude(twice, Part{}) == 0.6, "the earlier one");
}

// -- names a rule's selector redefines ----------------------------------------------

void test_scoped_override() {
    // The owner's restyle example: a selector's definition changes its own
    // parts only, and every rule reading the name follows.
    const std::string_view sheet = ":root { --lift: 0.2 } button { --lift: 0.8 } "
                                   "#a { primitive: bulge; amplitude: var(--lift) }";
    const Compilation compiled = compile(sheet);
    require(compiled_clean(compiled) && compiled.sheet.rules.size() == 4U,
            "the palette's definition, the button's, and a copy of the reader for each");
    if (compiled.sheet.rules.size() == 4U) {
        const ThemeRule& palette = compiled.sheet.rules[0];
        const ThemeRule& button = compiled.sheet.rules[1];
        require(palette.label == ":root" && palette.selector == RuleSelector{} &&
                    palette.level == CascadeLevel::theme_sheet && palette.scoped_definitions.size() == 1U &&
                    palette.scoped_definitions[0].name == "--lift",
                "the palette's definition first, matching every part");
        require(button.selector.control_type == "button" && button.scoped_definitions.size() == 1U &&
                    button.declarations.empty(),
                "the button's definition a rule of its own");
        const ThemeRule& first = compiled.sheet.rules[2];
        const ThemeRule& second = compiled.sheet.rules[3];
        require(first.scoped_conditions.size() == 1U && second.scoped_conditions.size() == 1U &&
                    first.scoped_conditions[0].definition == palette.scoped_definitions[0].definition &&
                    second.scoped_conditions[0].definition == button.scoped_definitions[0].definition,
                "each copy conditioned on its definition winning");
    }
    require(amplitude(sheet, Part{.control_type = "button"}) == 0.8, "a button's part reads the button's");
    require(amplitude(sheet, Part{.control_type = "checkbox"}) == 0.2, "another control's reads the palette's");

    const std::string_view classes = ":root { --lift: 0.2 } .loud { --lift: 0.8 } .quiet { --lift: 0.1 } "
                                     "#a { primitive: bulge; amplitude: var(--lift) }";
    require(amplitude(classes, Part{.classes = loud}) == 0.8 && amplitude(classes, Part{.classes = quiet}) == 0.1,
            "each class reads its own");
    require(amplitude(classes, Part{.classes = loud_and_quiet}) == 0.1, "and the later of equal specificity wins");
    const std::string_view specific = ":root { --lift: 0.2 } #a { --lift: 0.6 } .loud { --lift: 0.8 } "
                                      "#a { primitive: bulge; amplitude: var(--lift) }";
    require(amplitude(specific, Part{.classes = loud}) == 0.6, "the more specific definition wins");
    // A name read only through another's definition varies its readers.
    const std::string_view through = ":root { --base: 0.2; --lift: var(--base) } .loud { --base: 0.8 } "
                                     "#a { primitive: bulge; amplitude: var(--lift) }";
    const Compilation transitive = compile(through);
    require(compiled_clean(transitive) && copies_of(transitive, "#a") == 2U, "read through a definition");
    require(amplitude(through, Part{.classes = loud}) == 0.8 && amplitude(through, Part{}) == 0.2,
            "and resolved per part");
    // A name whose definitions no rule reads gives no rules.
    const Compilation unread = compile(":root { --lift: 0.2 } .loud { --lift: 0.8 } #a { opacity: 0.5 }");
    require(compiled_clean(unread) && unread.sheet.rules.size() == 1U, "an unread name lowers nothing");
}

void test_no_definition_holds() {
    // Defined in scopes only: where none matches, var() reads its fallback.
    const std::string_view fallback = ".loud { --lift: 0.8 } .quiet { --lift: 0.1 } "
                                      "#a { primitive: bulge; amplitude: var(--lift, 0.5) }";
    const Compilation compiled = compile(fallback);
    require(compiled_clean(compiled) && copies_of(compiled, "#a") == 3U, "a copy for each class and for none");
    require(amplitude(fallback, Part{}) == 0.5, "none: the fallback");
    require(amplitude(fallback, Part{.classes = loud}) == 0.8, "a class: its definition");
    // Without a fallback, the declaration is not declared there, as CSS's
    // invalid at computed value time; an earlier rule's value stands.
    const std::string_view dropped = "#a { primitive: bulge; amplitude: 0.3 } .loud { --lift: 0.8 } "
                                     ".quiet { --lift: 0.1 } #a { primitive: bulge; amplitude: var(--lift) }";
    const Compilation undeclared = compile(dropped);
    require(compiled_clean(undeclared), "no definition holding is not an error");
    require(amplitude(dropped, Part{}) == 0.3, "the earlier rule's value stands");
    require(amplitude(dropped, Part{.classes = quiet}) == 0.1, "where a definition holds, it is read");
}

void test_importance() {
    // `!important` on a definition is the cascade's, as on any declaration.
    const std::string_view important = ".loud { --lift: 0.8 !important } #a { --lift: 0.6 } "
                                       "#a { primitive: bulge; amplitude: var(--lift) }";
    const Compilation compiled = compile(important);
    require(compiled_clean(compiled), "an important definition compiles");
    bool important_level = false;
    for (const ThemeRule& rule : compiled.sheet.rules) {
        important_level = important_level || (!rule.scoped_definitions.empty() &&
                                              rule.level == CascadeLevel::important_theme_sheet);
    }
    require(important_level, "at the important level");
    require(amplitude(important, Part{}) == 0.6, "the ID's definition where the class does not match");
    require(amplitude(important, Part{.classes = loud}) == 0.8, "the important class's over the ID's");
    // The palette's definitions are what a CSS part inherits: a rule's own
    // definition replaces even an important one.
    const std::string_view palette = ":root { --lift: 0.1 !important; --lift: 0.3 } .loud { --lift: 0.8 } "
                                     "#a { primitive: bulge; amplitude: var(--lift) }";
    require(amplitude(palette, Part{}) == 0.1, "the palette's important definition over its ordinary one");
    require(amplitude(palette, Part{.classes = loud}) == 0.8, "a rule's definition over the palette's");
}

// -- names the palette varies by media ----------------------------------------------

void test_media_tokens() {
    const std::string_view sheet = ":root { --lift: 0.2; --depth: 0.3 } "
                                   "@media (prefers-color-scheme: dark) { :root { --lift: 0.7; --depth: 0.9 } } "
                                   "#a { primitive: bulge; amplitude: var(--lift); opacity: var(--depth) }";
    const Compilation compiled = compile(sheet);
    require(compiled_clean(compiled) && compiled.sheet.rules.size() == 2U,
            "two names varying under one condition: two copies, not four");
    if (compiled.sheet.rules.size() == 2U) {
        require(media_condition_unconditional(compiled.sheet.rules[0].media) &&
                    compiled.sheet.rules[1].media.dark_scheme == std::optional<bool>{true} &&
                    compiled.sheet.rules[0].scoped_conditions.empty(),
                "the light copy everywhere, the dark one where dark holds, unconditioned");
    }
    require(amplitude(sheet, Part{}) == 0.2 && amplitude(sheet, Part{.dark = true}) == 0.7,
            "each window reads its definition");
    // A rule already under the condition needs no copy without it.
    const std::string_view under = ":root { --lift: 0.2 } #a { primitive: bulge; amplitude: 0.1 } "
                                   "@media (prefers-color-scheme: dark) { :root { --lift: 0.7 } "
                                   "#a { primitive: bulge; amplitude: var(--lift) } }";
    const Compilation dark = compile(under);
    require(compiled_clean(dark) && dark.sheet.rules.size() == 2U, "a dark rule is copied once");
    require(amplitude(under, Part{}) == 0.1 && amplitude(under, Part{.dark = true}) == 0.7, "and reads dark");
    // Two conditions: a copy for each window they make.
    const std::string_view two = ":root { --lift: 0.2; --speed: 0.5 } "
                                 "@media (prefers-color-scheme: dark) { :root { --lift: 0.7 } } "
                                 "@media (prefers-reduced-motion: reduce) { :root { --speed: 0.1 } } "
                                 "#a { primitive: bulge; amplitude: var(--lift); opacity: var(--speed) }";
    const Compilation windows = compile(two);
    require(compiled_clean(windows) && windows.sheet.rules.size() == 4U, "two conditions, four copies");
    require(amplitude(two, Part{.dark = true, .reduced_motion = true}) == 0.7, "dark with reduced motion");
}

void test_pruning() {
    // One class redefining three names: its definitions win together.
    const std::string_view block = ":root { --a: 0.1; --b: 0.2; --c: 0.9 } .loud { --a: 0.4; --b: 0.5; --c: 1.1 } "
                                   "#a { primitive: bulge; amplitude: var(--a); opacity: var(--b); scale: var(--c) }";
    const Compilation together = compile(block);
    require(compiled_clean(together) && copies_of(together, "#a") == 2U, "two copies, not eight");
    // A name the palette varies and a class redefines.
    const std::string_view mixed = ":root { --a: 0.1; --b: 0.2 } "
                                   "@media (prefers-color-scheme: dark) { :root { --a: 0.3; --b: 0.4 } } "
                                   ".loud { --a: 0.5 } #a { primitive: bulge; amplitude: var(--a); opacity: var(--b) }";
    const Compilation varied = compile(mixed);
    require(compiled_clean(varied) && copies_of(varied, "#a") == 4U, "light and dark, each with and without the class");
    require(amplitude(mixed, Part{}) == 0.1 && amplitude(mixed, Part{.dark = true}) == 0.3,
            "the palette's light and dark");
    require(amplitude(mixed, Part{.classes = loud, .dark = true}) == 0.5, "the class over both");
}

// -- refusals ------------------------------------------------------------------------

void test_refusals() {
    require_refused(".loud { --x: 0.5 } #b { --x: 1 } @keyframes k { from { opacity: var(--x) } }",
                    DiagnosticCode::variable_cannot_vary, "a varying name in @keyframes cannot vary (E416)");
    require_refused(":root { --ink: #405060; ink-on-paper: var(--ink) on white } .loud { --ink: #ff0000 } "
                    "#a { color: role(ink-on-paper) }",
                    DiagnosticCode::variable_cannot_vary, "nor in the palette");
    // Eight names each redefined by its own class: 256 combinations.
    std::string many = ":root {";
    std::string reads = "#a { primitive: bulge; amplitude: calc(0";
    for (std::size_t index = 0U; index < 8U; ++index) {
        const std::string name = "--n" + std::to_string(index);
        many += " " + name + ": 0.1;";
        reads += " + var(" + name + ")";
    }
    many += " }";
    for (std::size_t index = 0U; index < 8U; ++index) {
        many += " .c" + std::to_string(index) + " { --n" + std::to_string(index) + ": 0.01 }";
    }
    require_refused(many + " " + reads + ") }", DiagnosticCode::too_many_variable_combinations,
                    "more combinations than the limit (E508)");
    // One more scoped name read than the limit.
    std::string scoped = "";
    for (std::size_t index = 0U; index <= maximum_scoped_custom_properties; ++index) {
        const std::string name = "--s" + std::to_string(index);
        scoped += ":root { " + name + ": 0.1 } .loud { " + name + ": 0.2 } #a { primitive: bulge; amplitude: var(" +
                  name + ") } ";
    }
    require_refused(scoped, DiagnosticCode::too_many_scoped_variables, "more scoped names than the limit (E515)");
}

// -- the engine ----------------------------------------------------------------------

void test_engine() {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeRuleSheet sheet{};
    ThemeRule definition{};
    definition.label = "definition";
    definition.scoped_definitions.push_back(ScopedDefinition{.name = "--unread", .definition = 7U});
    sheet.rules.push_back(definition);
    const CompiledThemeRules unread = CompiledThemeRules::compile(sheet, registry);
    require(unread.rule_count() == 0U && unread.diagnostics().empty() && unread.scoped_name_count() == 0U,
            "a definition no condition reads is dropped without a diagnostic");

    ThemeRule reader{};
    reader.label = "reader";
    reader.selector.stable_id = "a";
    reader.declarations.push_back(RuleDeclaration::primitive("bulge"));
    reader.scoped_conditions.push_back(ScopedCondition{.name = "--lift", .definition = 7U});
    ThemeRule other = reader;
    other.label = "other";
    other.scoped_conditions[0].definition = custom_property_undefined;
    other.declarations.clear();
    other.declarations.push_back(RuleDeclaration::primitive(built_in_primitive_name));
    sheet.rules.clear();
    definition.scoped_definitions[0].name = "--lift";
    definition.selector.classes.push_back("loud");
    sheet.rules.push_back(definition);
    sheet.rules.push_back(reader);
    sheet.rules.push_back(other);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, registry);
    require(rules.diagnostics().empty() && rules.scoped_name_count() == 1U, "one scoped name");
    require(resolve(rules, Part{.classes = loud}).outcome == CompiledThemeRules::Outcome::primitive,
            "where the definition wins, its copy applies");
    require(resolve(rules, Part{}).outcome == CompiledThemeRules::Outcome::built_in_selected,
            "where none does, the copy for none");

    ThemeRule invalid = definition;
    invalid.scoped_definitions[0].name = "lift";
    sheet.rules.push_back(invalid);
    const CompiledThemeRules refused = CompiledThemeRules::compile(sheet, registry);
    require(refused.diagnostics().size() == 1U &&
                refused.diagnostics()[0].kind == RuleDiagnosticKind::scoped_property_invalid,
            "a definition of no custom property is dropped with a diagnostic");
}

// -- a token-heavy theme --------------------------------------------------------------

// About a hundred tokens in the palette (colour scales, semantic colours,
// radii, widths, shadows, motion, opacity, and the spacing, type and size
// scales a forms theme leaves to layout), light and dark and reduced
// motion through @media, and a few scoped overrides.
constexpr std::string_view token_palette =
    ":root {\n"
    "  ink-on-paper: #1d232b on #ffffff; muted-ink-on-paper: #5b6470 on #ffffff;\n"
    "  disabled-ink-on-paper: #9aa3ad on #ffffff; on-accent-on-accent: #ffffff on #2563eb;\n"
    "  ordinary-line: #c9ced6; dark-line: #8a929c; focus: #2563eb; shadow: #000000;\n"
    "  --gray-0: #f8f9fa; --gray-1: #f1f3f5; --gray-2: #e9ecef; --gray-3: #dee2e6; --gray-4: #ced4da;\n"
    "  --gray-5: #adb5bd; --gray-6: #868e96; --gray-7: #495057; --gray-8: #343a40; --gray-9: #212529;\n"
    "  --blue-0: #e7f5ff; --blue-1: #d0ebff; --blue-2: #a5d8ff; --blue-3: #74c0fc; --blue-4: #4dabf7;\n"
    "  --blue-5: #339af0; --blue-6: #228be6; --blue-7: #1c7ed6; --blue-8: #1971c2; --blue-9: #1864ab;\n"
    "  --red-5: #ff6b6b; --red-6: #fa5252; --green-5: #51cf66; --green-6: #40c057; --amber-5: #fcc419;\n"
    "}\n"
    // A rule declares at most 64 properties (E503), so the palette is
    // written in three blocks.
    ":root {\n"
    "  --surface: var(--gray-0); --surface-raised: #ffffff; --surface-sunken: var(--gray-1);\n"
    "  --surface-hover: var(--gray-2); --surface-pressed: var(--gray-3); --track: var(--gray-3);\n"
    "  --text: role(ink-on-paper); --text-muted: role(muted-ink-on-paper);\n"
    "  --text-disabled: role(disabled-ink-on-paper); --text-on-accent: role(on-accent-on-accent);\n"
    "  --accent: var(--blue-6); --accent-hover: var(--blue-7); --accent-pressed: var(--blue-8);\n"
    "  --border: role(ordinary-line); --border-strong: role(dark-line); --border-focus: role(focus);\n"
    "  --danger: var(--red-6); --success: var(--green-6); --warning: var(--amber-5);\n"
    "  --shadow-colour: role(shadow);\n"
    "}\n"
    ":root {\n"
    "  --radius-xs: 2px; --radius-sm: 4px; --radius-md: 6px; --radius-lg: 10px; --radius-full: 999px;\n"
    "  --border-thin: 1px; --border-thick: 2px; --focus-width: 3px; --focus-offset: 1px;\n"
    "  --shadow-1: 0 1px 2px var(--shadow-colour); --shadow-2: 0 2px 6px var(--shadow-colour);\n"
    "  --shadow-3: 0 6px 16px var(--shadow-colour); --shadow-4: 0 12px 32px var(--shadow-colour);\n"
    "  --motion-instant: 0ms; --motion-fast: 120ms; --motion-normal: 200ms; --motion-slow: 320ms;\n"
    "  --ease-standard: ease-out; --ease-emphasis: ease-in-out;\n"
    "  --opacity-disabled: 0.45; --opacity-muted: 0.7; --opacity-overlay: 0.5;\n"
    "  --hover-lift: 1.01; --press-scale: 0.98;\n"
    "  --space-0: 0; --space-1: 2px; --space-2: 4px; --space-3: 6px; --space-4: 8px; --space-5: 12px;\n"
    "  --space-6: 16px; --space-7: 20px; --space-8: 24px; --space-9: 32px; --space-10: 40px; --space-11: 48px;\n"
    "  --font-size-xs: 11px; --font-size-sm: 12px; --font-size-md: 13px; --font-size-lg: 15px;\n"
    "  --font-size-xl: 18px; --font-size-2xl: 22px;\n"
    "  --weight-regular: 400; --weight-medium: 500; --weight-bold: 650;\n"
    "  --layer-base: 0; --layer-raised: 10; --layer-overlay: 100; --layer-toast: 1000;\n"
    "  --control-height-sm: 22px; --control-height-md: 28px; --control-height-lg: 34px;\n"
    "  --thumb-size: 14px; --track-height: 4px; --icon-size: 16px;\n"
    "}\n";

constexpr std::string_view token_media =
    "@media (prefers-color-scheme: dark) {\n"
    "  :root {\n"
    "    ink-on-paper: #e6e9ee on #14181d; muted-ink-on-paper: #a3abb6 on #14181d;\n"
    "    disabled-ink-on-paper: #5f6874 on #14181d; ordinary-line: #39414b; dark-line: #6b7480;\n"
    "    --surface: var(--gray-9); --surface-raised: var(--gray-8); --surface-sunken: #16191c;\n"
    "    --surface-hover: var(--gray-8); --surface-pressed: var(--gray-7); --track: var(--gray-7);\n"
    "    --accent: var(--blue-4); --accent-hover: var(--blue-3); --accent-pressed: var(--blue-2);\n"
    "    --danger: var(--red-5); --success: var(--green-5);\n"
    "  }\n"
    "}\n"
    "@media (prefers-reduced-motion: reduce) {\n"
    "  :root { --motion-fast: 0ms; --motion-normal: 0ms; --motion-slow: 0ms; --press-scale: 1; }\n"
    "}\n";

constexpr std::string_view token_overrides =
    ".primary { --surface: var(--accent); --surface-hover: var(--accent-hover);\n"
    "  --surface-pressed: var(--accent-pressed); --text: var(--text-on-accent); }\n"
    ".danger { --accent: var(--danger); --accent-hover: var(--red-5); --accent-pressed: var(--red-6); }\n"
    ".compact { --radius-md: var(--radius-sm); }\n"
    "slider { --accent: var(--success); }\n";

constexpr std::string_view token_rules =
    "button::part(face) { background-color: var(--surface); border: var(--border-thin) solid var(--border);\n"
    "  border-radius: var(--radius-md); box-shadow: var(--shadow-1);\n"
    "  transition: opacity var(--motion-fast) var(--ease-standard); }\n"
    "button::part(face):hover { background-color: var(--surface-hover); box-shadow: var(--shadow-2);\n"
    "  scale: var(--hover-lift); }\n"
    "button::part(face):active { background-color: var(--surface-pressed); box-shadow: none;\n"
    "  scale: var(--press-scale); }\n"
    "button::part(face):focus { border-color: var(--border-focus); }\n"
    "button::part(face):disabled { opacity: var(--opacity-disabled); }\n"
    // The text colour is the control's (a recipe field), not a part's.
    "button { color: var(--text); }\n"
    "button:disabled { color: var(--text-disabled); }\n"
    "checkbox::part(box) { background-color: var(--surface-raised);\n"
    "  border: var(--border-thin) solid var(--border-strong); border-radius: var(--radius-xs); }\n"
    "checkbox::part(box):hover { background-color: var(--surface-hover); }\n"
    "checkbox::part(box):focus { border-color: var(--border-focus); }\n"
    "checkbox::part(box):disabled { opacity: var(--opacity-disabled); }\n"
    "checkbox::part(check) { background-color: var(--accent); border-radius: var(--radius-xs);\n"
    "  transition: opacity var(--motion-fast) var(--ease-standard); }\n"
    "checkbox::part(check):hover { background-color: var(--accent-hover); }\n"
    "checkbox::part(check):active { background-color: var(--accent-pressed); }\n"
    "checkbox { color: var(--text); }\n"
    "textbox::part(well) { background-color: var(--surface-sunken);\n"
    "  border: var(--border-thin) solid var(--border); border-radius: var(--radius-sm); }\n"
    "textbox::part(well):focus { border-color: var(--border-focus);\n"
    "  box-shadow: 0 0 0 var(--focus-width) var(--border-focus); }\n"
    "textbox::part(well):disabled { opacity: var(--opacity-muted); }\n"
    "textbox { color: var(--text); }\n"
    "slider::part(track) { background-color: var(--track); border-radius: var(--radius-full); }\n"
    "slider::part(fill) { background-color: var(--accent); border-radius: var(--radius-full); }\n"
    "slider::part(thumb) { background-color: var(--surface-raised);\n"
    "  border: var(--border-thin) solid var(--border-strong); border-radius: var(--radius-full);\n"
    "  box-shadow: var(--shadow-1); transition: scale var(--motion-fast) var(--ease-standard); }\n"
    "slider::part(thumb):hover { box-shadow: var(--shadow-2); scale: var(--hover-lift); }\n"
    "slider::part(thumb):active { box-shadow: var(--shadow-3); scale: var(--press-scale); }\n"
    "tab::part(face) { background-color: var(--surface); border-radius: var(--radius-md) var(--radius-md) 0 0;\n"
    "  transition: opacity var(--motion-normal) var(--ease-emphasis); }\n"
    "tab::part(face):hover { background-color: var(--surface-hover); }\n"
    "tab::part(face):checked { background-color: var(--surface-raised); box-shadow: var(--shadow-1); }\n"
    "tab { color: var(--text-muted); }\n"
    "tab:checked { color: var(--text); }\n";

// The lowered sheet's size in memory: each rule and what it holds.
std::size_t sheet_bytes(const ThemeRuleSheet& sheet) {
    std::size_t bytes = 0U;
    for (const ThemeRule& rule : sheet.rules) {
        bytes += sizeof(ThemeRule) + rule.label.size();
        bytes += rule.declarations.size() * sizeof(RuleDeclaration);
        bytes += rule.transitions.size() * sizeof(PropertyTransition);
        bytes += rule.materials.size() * sizeof(MaterialDeclaration);
        bytes += rule.recipe_fields.size() * sizeof(RecipeDeclaration);
        for (const ScopedDefinition& definition : rule.scoped_definitions) {
            bytes += sizeof(ScopedDefinition) + definition.name.size();
        }
        for (const ScopedCondition& condition : rule.scoped_conditions) {
            bytes += sizeof(ScopedCondition) + condition.name.size();
        }
    }
    return bytes;
}

struct TokenMeasure final {
    std::size_t rules{};
    std::size_t copies{};
    std::size_t most_copies{};
    std::size_t bytes{};
    std::size_t scoped_names{};
};

TokenMeasure measure(const std::string& text, std::string_view name) {
    TokenMeasure result{};
    const Compilation compiled = compile(text);
    require(compiled_clean(compiled), std::string("the token theme compiles clean: ") + std::string(name));
    if (compiled.outcome != CompileOutcome::compiled) return result;
    const CompiledThemeRules rules = CompiledThemeRules::compile(compiled.sheet, builtin_primitive_registry());
    result.rules = compiled.sheet.rules.size();
    result.bytes = sheet_bytes(compiled.sheet);
    result.scoped_names = rules.scoped_name_count();
    std::vector<std::string> labels{};
    for (const ThemeRule& rule : compiled.sheet.rules) {
        if (!rule.scoped_definitions.empty()) continue;
        bool seen = false;
        for (const std::string& label : labels) seen = seen || label == rule.label;
        if (!seen) labels.push_back(rule.label);
    }
    for (const std::string& label : labels) {
        const std::size_t copies = copies_of(compiled, label);
        result.copies += copies;
        if (copies > result.most_copies) result.most_copies = copies;
    }
    std::cout << "token theme (" << name << "): " << result.rules << " rules, " << labels.size()
              << " style rules, " << result.copies << " copies, at most " << result.most_copies
              << " for one rule, " << result.scoped_names << " scoped names, " << result.bytes << " bytes\n";
    return result;
}

void test_token_theme() {
    const std::string flat = std::string(token_palette) + std::string(token_rules);
    const std::string windows = std::string(token_palette) + std::string(token_media) + std::string(token_rules);
    const std::string full = std::string(token_palette) + std::string(token_media) + std::string(token_overrides) +
                             std::string(token_rules);
    const TokenMeasure one = measure(flat, "light only");
    const TokenMeasure media = measure(windows, "light, dark and reduced motion");
    const TokenMeasure scoped = measure(full, "with scoped overrides");
    // Measured on the M4 (2026-10-31): light only, 28 rules, one each (a
    // :disabled opacity joins the face's transition as its state value);
    // with dark and reduced motion, 60 rules, at most 4 for one rule; with
    // the overrides, 134 rules (127 copies and 7 rules of definitions), at
    // most 24 for one rule (four worlds, each with the palette's surface,
    // the primary's over the palette's accent or the danger's, and each with
    // and without the compact radius), and 8 scoped names. PLAN.md section
    // 1.11 records them with the limits they set.
    require(one.copies == 28U && one.most_copies == 1U, "light only: one rule each");
    require(media.copies == 60U && media.most_copies == 4U, "dark and reduced motion: at most four copies of a rule");
    require(scoped.rules == 134U && scoped.most_copies == 24U && scoped.scoped_names == 8U,
            "with the overrides: at most 24 copies of a rule, and 8 scoped names");
    require(scoped.most_copies * 4U <= maximum_variable_combinations &&
                scoped.scoped_names * 8U <= maximum_scoped_custom_properties,
            "the limits leave at least four and eight times the headroom");

    // And it resolves: a primary button's face in dark reads the dark
    // accent; a compact one its small radius.
    const Compilation compiled = compile(full);
    const CompiledThemeRules rules = CompiledThemeRules::compile(compiled.sheet, builtin_primitive_registry());
    const std::array<std::string, 1> primary{{"primary"}};
    const std::size_t background = static_cast<std::size_t>(MaterialField::background_colour);
    const CompiledThemeRules::Resolution light =
        resolve(rules, Part{.control_type = "button", .part = "face", .role = PartRole::face});
    const CompiledThemeRules::Resolution dark = resolve(
        rules, Part{.control_type = "button", .part = "face", .role = PartRole::face, .dark = true});
    const CompiledThemeRules::Resolution accent = resolve(
        rules,
        Part{.control_type = "button", .classes = primary, .part = "face", .role = PartRole::face, .dark = true});
    const MaterialDeclaration* const light_fill = light.materials[background];
    const MaterialDeclaration* const dark_fill = dark.materials[background];
    const MaterialDeclaration* const accent_fill = accent.materials[background];
    require(light_fill != nullptr && (*light_fill).colour == Color::rgba(0xf8, 0xf9, 0xfa), "light: gray-0");
    require(dark_fill != nullptr && (*dark_fill).colour == Color::rgba(0x21, 0x25, 0x29), "dark: gray-9");
    require(accent_fill != nullptr && (*accent_fill).colour == Color::rgba(0x4d, 0xab, 0xf7),
            "a dark primary button: the dark accent, blue-4");
}

} // namespace

int main() {
    test_defined_once();
    test_scoped_override();
    test_no_definition_holds();
    test_importance();
    test_media_tokens();
    test_pruning();
    test_refusals();
    test_engine();
    test_token_theme();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
