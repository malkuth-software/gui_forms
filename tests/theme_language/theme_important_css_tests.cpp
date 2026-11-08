// CSS's `!important` (stage T3, phase P9, engine change EC6): a style
// rule's important declarations lower to a sibling rule with the same
// selector, label and media condition at the important level, and the
// cascade orders the owner's rule E1: an important declaration wins
// whichever side it is on, and where neither or both are important the
// inline style wins.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using theme_language::CompileOutcome;
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
// With `base_end`, the text before that byte is the GUI.Forms base sheet
// (ThemeCompileInput::base_origin_end).
Compilation compile(std::string_view text, std::optional<std::uint32_t> base_end = std::nullopt) {
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
    input.base_origin_end = base_end;
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

bool has_primitive(const ThemeRule& rule, std::string_view name) {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == "primitive" && declaration.name == name) return true;
    }
    return false;
}

std::size_t amplitude_index() {
    const EnginePrimitive* const bulge = builtin_primitive_registry().find("bulge");
    const std::optional<std::size_t> found =
        bulge == nullptr ? std::optional<std::size_t>{} : find_parameter((*bulge).descriptor(), "amplitude");
    return found ? *found : 0U;
}

// The face of the control with the stable ID `a` (and the class `loud`).
CompiledThemeRules::Resolution face_of(const CompiledThemeRules& rules) {
    const std::array<std::string, 1> classes{{"loud"}};
    CompiledThemeRules::PartFacts facts{};
    facts.stable_id = "a";
    facts.classes = classes;
    facts.role = PartRole::face;
    return rules.resolve(facts);
}

double amplitude_of(const CompiledThemeRules::Resolution& resolution) {
    if (resolution.outcome != CompiledThemeRules::Outcome::primitive) return -1.0;
    return resolution.parameters.values[amplitude_index()].number;
}

// An inline style of the control `a` selecting the bulge at `amplitude`,
// as C++ data (Control::set_inline_style lowers its text to the same).
ThemeRule inline_bulge(double amplitude, bool important) {
    ThemeRule rule{};
    rule.label = important ? "inline important" : "inline";
    rule.level = important ? CascadeLevel::important_inline_style : CascadeLevel::inline_style;
    rule.selector.stable_id = "a";
    rule.declarations.push_back(RuleDeclaration::primitive("bulge"));
    rule.declarations.push_back(RuleDeclaration::number_value("amplitude", amplitude));
    return rule;
}

// -- lowering ------------------------------------------------------------------

void test_split() {
    const Compilation split = compile("#a { primitive: bulge; amplitude: 0.3; opacity: 0.5 !important }");
    require(compiled_clean(split) && split.sheet.rules.size() == 2U, "one rule splits in two");
    if (split.sheet.rules.size() == 2U) {
        const ThemeRule& important = split.sheet.rules[0];
        const ThemeRule& ordinary = split.sheet.rules[1];
        require(important.level == CascadeLevel::important_theme_sheet &&
                    ordinary.level == CascadeLevel::theme_sheet,
                "the important declarations at the important level, the rest at the selector's");
        require(important.selector == ordinary.selector && important.label == ordinary.label &&
                    important.media == ordinary.media,
                "with the same selector, label and media condition");
        require(!has_primitive(important, "bulge") && important.declarations.empty() &&
                    important.transitions.size() == 1U,
                "the sibling holds only its own declarations, selecting nothing it did not write");
        require(has_primitive(ordinary, "bulge") && ordinary.declarations.size() == 2U &&
                    ordinary.transitions.empty(),
                "and the ordinary rule the rest");
    }

    const Compilation whole = compile("#a { primitive: bulge !important; amplitude: 0.4 !important }");
    require(compiled_clean(whole) && whole.sheet.rules.size() == 1U &&
                whole.sheet.rules[0].level == CascadeLevel::important_theme_sheet &&
                has_primitive(whole.sheet.rules[0], "bulge"),
            "a rule all important lowers to the sibling alone");

    const Compilation borrowed = compile("#a { primitive: bulge !important; amplitude: 0.4 }");
    require(compiled_clean(borrowed) && borrowed.sheet.rules.size() == 2U, "an important primitive");
    if (borrowed.sheet.rules.size() == 2U) {
        require(has_primitive(borrowed.sheet.rules[0], "bulge") && has_primitive(borrowed.sheet.rules[1], "bulge") &&
                    borrowed.sheet.rules[1].declarations.size() == 2U,
                "carries the ordinary parameters on the important primitive");
        const CompiledThemeRules rules = CompiledThemeRules::compile(borrowed.sheet, builtin_primitive_registry());
        const CompiledThemeRules::Resolution face = face_of(rules);
        require(face.level == CascadeLevel::important_theme_sheet && amplitude_of(face) == 0.4,
                "and the important level selects it with them");
    }

    const Compilation material = compile("#a { background-color: red !important }");
    require(compiled_clean(material) && material.sheet.rules.size() == 1U &&
                material.sheet.rules[0].level == CascadeLevel::important_theme_sheet &&
                material.sheet.rules[0].materials.size() == 1U && has_primitive(material.sheet.rules[0], "material"),
            "important material fields select the material primitive with them (C7)");

    const Compilation mixed = compile("#a { border-radius: 4px; background-color: red !important }");
    require(compiled_clean(mixed) && mixed.sheet.rules.size() == 2U, "ordinary and important material fields");
    if (mixed.sheet.rules.size() == 2U) {
        const ThemeRule& important = mixed.sheet.rules[0];
        const ThemeRule& ordinary = mixed.sheet.rules[1];
        require(has_primitive(important, "material") && important.materials.size() == 1U &&
                    ordinary.materials.size() == 4U,
                "the ordinary fields borrow the important selection");
    }

    const Compilation ordinary_only = compile("#a { border-radius: 4px; opacity: 0.5 !important }");
    require(compiled_clean(ordinary_only) && ordinary_only.sheet.rules.size() == 2U &&
                !has_primitive(ordinary_only.sheet.rules[0], "material") &&
                has_primitive(ordinary_only.sheet.rules[1], "material"),
            "ordinary material fields alone select it at the ordinary level");

    const Compilation declared = compile("#a { primitive: bulge; background-color: red !important }");
    require(compiled_clean(declared) && declared.sheet.rules.size() == 2U &&
                !has_primitive(declared.sheet.rules[0], "material") && has_primitive(declared.sheet.rules[1], "bulge"),
            "a declared primitive is not replaced");

    const Compilation colour = compile("#a { outline-offset: 2px !important }");
    require(compiled_clean(colour) && colour.sheet.rules.size() == 1U &&
                colour.sheet.rules[0].level == CascadeLevel::important_theme_sheet &&
                colour.sheet.rules[0].recipe_fields.size() == 1U && colour.sheet.rules[0].declarations.empty(),
            "a recipe field selects no primitive");

    const Compilation spaced = compile("#a { opacity: 0.5 ! IMPORTANT ; scale: 1.1 }");
    require(compiled_clean(spaced) && spaced.sheet.rules.size() == 2U, "`! IMPORTANT` is `!important`");

    const Compilation listed = compile("#a, .loud { opacity: 0.5 !important }");
    require(compiled_clean(listed) && listed.sheet.rules.size() == 2U &&
                listed.sheet.rules[0].level == CascadeLevel::important_theme_sheet &&
                listed.sheet.rules[1].level == CascadeLevel::important_theme_sheet,
            "each selector of a list has its sibling");

    const Compilation media = compile("@media (prefers-color-scheme: dark) { #a { opacity: 0.5 !important } }");
    require(compiled_clean(media) && media.sheet.rules.size() == 1U &&
                media.sheet.rules[0].media.dark_scheme == std::optional<bool>(true),
            "a sibling keeps the rule's media condition");
}

// -- the cascade (E1) ---------------------------------------------------------------

// The four cases of E1 between a theme rule and an inline style.
void test_e1_cascade() {
    struct Case final {
        std::string_view theme{};
        bool inline_important{};
        double expected{};
        CascadeLevel level{};
        std::string_view name{};
    };
    const std::array<Case, 4> cases{{
        {"#a { primitive: bulge; amplitude: 0.3 }", false, 0.9, CascadeLevel::inline_style, "neither important"},
        {"#a { primitive: bulge !important; amplitude: 0.3 !important }", false, 0.3,
         CascadeLevel::important_theme_sheet, "the theme only"},
        {"#a { primitive: bulge; amplitude: 0.3 }", true, 0.9, CascadeLevel::important_inline_style,
         "the inline style only"},
        {"#a { primitive: bulge !important; amplitude: 0.3 !important }", true, 0.9,
         CascadeLevel::important_inline_style, "both important"},
    }};
    for (const Case& entry : cases) {
        const Compilation theme = compile(entry.theme);
        require(compiled_clean(theme), std::string(entry.name) + ": the theme compiles");
        ThemeRuleSheet sheet = theme.sheet;
        sheet.rules.push_back(inline_bulge(0.9, entry.inline_important));
        const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
        require(rules.diagnostics().empty(), std::string(entry.name) + ": the rules compile");
        const CompiledThemeRules::Resolution face = face_of(rules);
        require(face.level == entry.level && amplitude_of(face) == entry.expected,
                std::string(entry.name) + ": " + std::string(cascade_level_name(face.level)));
    }

    // Within the important level, specificity and then order, as at any
    // level; an important rule beats any ordinary one.
    const Compilation specific =
        compile("#a { primitive: bulge !important; amplitude: 0.7 !important }\n"
                ".loud { primitive: bulge !important; amplitude: 0.5 !important }\n"
                "#a { primitive: bulge; amplitude: 0.1 }");
    require(compiled_clean(specific), "the specificity sheet compiles");
    const CompiledThemeRules ordered = CompiledThemeRules::compile(specific.sheet, builtin_primitive_registry());
    require(amplitude_of(face_of(ordered)) == 0.7, "the stable ID's important rule beats the class's");
    const Compilation lower =
        compile(".loud { primitive: bulge !important; amplitude: 0.5 !important }\n"
                "#a { primitive: bulge; amplitude: 0.1 }");
    const CompiledThemeRules classed = CompiledThemeRules::compile(lower.sheet, builtin_primitive_registry());
    require(amplitude_of(face_of(classed)) == 0.5, "an important class rule beats an ordinary stable-ID rule");
    const Compilation later =
        compile("#a { primitive: bulge !important; amplitude: 0.2 !important }\n"
                "#a { primitive: bulge !important; amplitude: 0.6 !important }");
    const CompiledThemeRules sequence = CompiledThemeRules::compile(later.sheet, builtin_primitive_registry());
    require(amplitude_of(face_of(sequence)) == 0.6, "among equals the later rule");
    const Compilation same = compile("#a { primitive: bulge; amplitude: 0.2 !important; amplitude: 0.6 }");
    require(same.outcome == CompileOutcome::refused,
            "an important parameter of an ordinary primitive is refused");
    const Compilation over = compile("#a { primitive: bulge !important; amplitude: 0.6 !important; amplitude: 0.2 }");
    const CompiledThemeRules overruled = CompiledThemeRules::compile(over.sheet, builtin_primitive_registry());
    require(compiled_clean(over) && amplitude_of(face_of(overruled)) == 0.6,
            "a rule's important declaration beats its own later ordinary one");
}

void test_levels() {
    require(cascade_level_name(CascadeLevel::important_theme_sheet) == "important_theme_sheet" &&
                cascade_level_name(CascadeLevel::important_inline_style) == "important_inline_style",
            "the important levels' names");
    require(CascadeLevel::inline_style < CascadeLevel::important_theme_sheet &&
                CascadeLevel::important_theme_sheet < CascadeLevel::forced_high_contrast &&
                CascadeLevel::forced_high_contrast < CascadeLevel::important_inline_style,
            "the levels' order (E1)");
    ThemeRuleSheet sheet{};
    ThemeRule nameless = inline_bulge(0.5, true);
    nameless.selector.stable_id.clear();
    nameless.selector.classes.push_back("loud");
    sheet.rules.push_back(nameless);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(rules.diagnostics().size() == 1U &&
                rules.diagnostics()[0].kind == RuleDiagnosticKind::selector_missing_level_field,
            "an important inline style names its control's stable ID");
}

} // namespace

// CSS's specificity and the base sheet as CSS's user-agent origin
// (selector step 1).
void test_origins() {
    // A pseudo-class counts with the classes: `:hover::role(face)` (0,1,1)
    // beats the later `.loud` (0,1,0), where a class once beat any state.
    const Compilation hovered = compile(":hover::role(face) { primitive: bulge; amplitude: 0.8 }\n"
                                        ".loud { primitive: bulge; amplitude: 0.3 }");
    require(compiled_clean(hovered), "the specificity sheet compiles");
    const CompiledThemeRules by_specificity = CompiledThemeRules::compile(hovered.sheet, builtin_primitive_registry());
    const std::array<std::string, 1> classes{{"loud"}};
    CompiledThemeRules::PartFacts hot{};
    hot.stable_id = "a";
    hot.classes = classes;
    hot.role = PartRole::face;
    hot.states.insert(AnatomyState::hot);
    require(amplitude_of(by_specificity.resolve(hot)) == 0.8, "a state and a part beat a class alone");
    require(amplitude_of(face_of(by_specificity)) == 0.3, "without the state, the class rule");
    for (const ThemeRule& rule : hovered.sheet.rules) {
        require(rule.level == CascadeLevel::theme_sheet, "every sheet rule at the sheet's level");
    }

    // The base's rules lose to every rule after it, whatever the
    // specificity; its important ones beat ordinary rules, and lose to
    // important ones after it.
    const std::string_view base = "#a.loud::role(face) { primitive: bulge; amplitude: 0.9 }\n";
    const std::string under = std::string(base) + "* { primitive: bulge; amplitude: 0.4 }";
    const Compilation layered = compile(under, static_cast<std::uint32_t>(base.size()));
    require(compiled_clean(layered) && layered.sheet.rules.size() == 2U, "the layered sheet compiles");
    if (layered.sheet.rules.size() == 2U) {
        require(layered.sheet.rules[0].level == CascadeLevel::base_sheet &&
                    layered.sheet.rules[1].level == CascadeLevel::theme_sheet,
                "the base's rule at the base level, the package's at the sheet's");
    }
    const CompiledThemeRules origins = CompiledThemeRules::compile(layered.sheet, builtin_primitive_registry());
    require(amplitude_of(face_of(origins)) == 0.4, "the package's universal rule beats the base's stable-ID rule");
    const std::string_view important_base = "#a { primitive: bulge !important; amplitude: 0.9 !important }\n";
    const std::string over = std::string(important_base) + "* { primitive: bulge; amplitude: 0.4 }";
    const Compilation raised = compile(over, static_cast<std::uint32_t>(important_base.size()));
    require(compiled_clean(raised) && !raised.sheet.rules.empty() &&
                raised.sheet.rules[0].level == CascadeLevel::important_base_sheet,
            "the base's important declarations at the important base level");
    const CompiledThemeRules raised_rules = CompiledThemeRules::compile(raised.sheet, builtin_primitive_registry());
    require(amplitude_of(face_of(raised_rules)) == 0.9, "the base's important rule beats an ordinary rule");
    const std::string both = std::string(important_base) + "* { primitive: bulge !important; amplitude: 0.4 !important }";
    const Compilation contested = compile(both, static_cast<std::uint32_t>(important_base.size()));
    const CompiledThemeRules contested_rules =
        CompiledThemeRules::compile(contested.sheet, builtin_primitive_registry());
    require(amplitude_of(face_of(contested_rules)) == 0.4, "an important rule after the base beats the base's");
}

int main() {
    test_split();
    test_e1_cascade();
    test_levels();
    test_origins();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme important CSS tests passed\n";
    return 0;
}
