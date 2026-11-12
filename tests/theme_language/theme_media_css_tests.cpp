// CSS's `@media` (stage T3, phase P8, engine change EC3): each query of a
// block's list lowers the block's rules once, carrying the query as the
// rules' media condition, and every window evaluates the conditions
// against its own presentation settings and scale. Gate: one sheet
// switching light and dark in one window, two windows at different scales
// showing one theme differently, and the forms the language refuses.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
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

MediaCondition dark() {
    MediaCondition condition{};
    condition.dark_scheme = true;
    return condition;
}

MediaCondition resolutions(std::optional<double> minimum, std::optional<double> maximum) {
    MediaCondition condition{};
    condition.minimum_resolution = minimum;
    condition.maximum_resolution = maximum;
    return condition;
}

// -- lowering ------------------------------------------------------------------

void test_query_lowering() {
    const Compilation plain = compile("#a { opacity: 0.5 }");
    require(compiled_clean(plain) && plain.sheet.rules.size() == 1U &&
                media_condition_unconditional(plain.sheet.rules[0].media),
            "a rule outside @media holds in every window");

    struct Case final {
        std::string_view sheet{};
        MediaCondition media{};
    };
    MediaCondition light{};
    light.dark_scheme = false;
    MediaCondition forced{};
    forced.forced_colours = true;
    MediaCondition unforced{};
    unforced.forced_colours = false;
    MediaCondition calm{};
    calm.reduced_motion = true;
    MediaCondition lively{};
    lively.reduced_motion = false;
    MediaCondition dark_calm = dark();
    dark_calm.reduced_motion = true;
    MediaCondition dense = resolutions(1.5, 3.0);
    dense.dark_scheme = true;
    const std::array<Case, 17> cases{{
        {"@media (prefers-color-scheme: dark) { #a { opacity: 0.5 } }", dark()},
        {"@media (prefers-color-scheme: light) { #a { opacity: 0.5 } }", light},
        {"@media (PREFERS-color-scheme: Dark) { #a { opacity: 0.5 } }", dark()},
        {"@media (forced-colors: active) { #a { opacity: 0.5 } }", forced},
        {"@media (forced-colors: none) { #a { opacity: 0.5 } }", unforced},
        {"@media (prefers-reduced-motion: reduce) { #a { opacity: 0.5 } }", calm},
        {"@media (prefers-reduced-motion: no-preference) { #a { opacity: 0.5 } }", lively},
        {"@media screen and (prefers-color-scheme: dark) { #a { opacity: 0.5 } }", dark()},
        {"@media all and (prefers-color-scheme: dark) and (prefers-reduced-motion: reduce) { #a { opacity: 0.5 } }",
         dark_calm},
        {"@media screen { #a { opacity: 0.5 } }", MediaCondition{}},
        {"@media all { #a { opacity: 0.5 } }", MediaCondition{}},
        {"@media { #a { opacity: 0.5 } }", MediaCondition{}},
        {"@media (min-resolution: 2dppx) { #a { opacity: 0.5 } }", resolutions(2.0, std::nullopt)},
        {"@media (max-resolution: 1.5x) { #a { opacity: 0.5 } }", resolutions(std::nullopt, 1.5)},
        {"@media (resolution: 2X) { #a { opacity: 0.5 } }", resolutions(2.0, 2.0)},
        {"@media (min-resolution: 1x) and (min-resolution: 1.5dppx) and (max-resolution: 4x) and "
         "(max-resolution: 3x) { #a { opacity: 0.5 } }",
         resolutions(1.5, 3.0)},
        {"@media (min-resolution: 1.5x) and (prefers-color-scheme: dark) and (max-resolution: 3x) "
         "{ #a { opacity: 0.5 } }",
         dense},
    }};
    for (const Case& entry : cases) {
        const Compilation compiled = compile(entry.sheet);
        require(compiled_clean(compiled) && compiled.sheet.rules.size() == 1U &&
                    compiled.sheet.rules[0].media == entry.media,
                std::string("the query lowers to its condition: ") + std::string(entry.sheet));
    }
}

void test_query_lists() {
    const Compilation listed = compile(
        "#a { opacity: 1 } @media (prefers-color-scheme: dark), (min-resolution: 2x) { #a { opacity: 0.5 } "
        "#b { scale: 0.9 } } #c { opacity: 0.25 }");
    require(compiled_clean(listed) && listed.sheet.rules.size() == 6U, "a list of two lowers its block twice");
    if (listed.sheet.rules.size() == 6U) {
        const std::vector<ThemeRule>& rules = listed.sheet.rules;
        require(media_condition_unconditional(rules[0].media) && rules[1].media == dark() &&
                    rules[2].media == dark() && rules[1].selector.stable_id == "a" &&
                    rules[2].selector.stable_id == "b",
                "the first query's copy of the block comes first, in written order");
        require(rules[3].media == resolutions(2.0, std::nullopt) && rules[3].selector.stable_id == "a" &&
                    rules[4].selector.stable_id == "b" && rules[4].media == rules[3].media,
                "then the second query's copy");
        require(media_condition_unconditional(rules[5].media) && rules[5].selector.stable_id == "c",
                "a rule after the block holds everywhere again");
    }

    const Compilation never = compile(
        "@media (prefers-color-scheme: dark) and (prefers-color-scheme: light) { #a { opacity: 0.5 } }");
    require(compiled_clean(never) && never.sheet.rules.empty(), "a query no window meets lowers nothing");
    const Compilation repeated =
        compile("@media (prefers-color-scheme: dark) and (prefers-color-scheme: dark) { #a { opacity: 0.5 } }");
    require(compiled_clean(repeated) && repeated.sheet.rules.size() == 1U && repeated.sheet.rules[0].media == dark(),
            "a feature named twice with one value is one condition");
}

void test_unknown_features() {
    const Compilation unknown = compile("#a { opacity: 1 } @media (min-width: 400px) { #a { opacity: 0.5 } }");
    require(unknown.outcome == CompileOutcome::compiled && unknown.sheet.rules.size() == 1U,
            "a block whose only query names an unknown feature is skipped");
    require(unknown.diagnostics.size() == 1U &&
                unknown.diagnostics[0].code == DiagnosticCode::unknown_media_feature &&
                unknown.diagnostics[0].detail == "min-width" && unknown.diagnostics[0].position.column.value == 27U,
            "W105 names the feature where it is written");
    require(theme_language::diagnostic_code_label(DiagnosticCode::unknown_media_feature) == "W105" &&
                diagnostic_code_name(DiagnosticCode::unknown_media_feature) == "unknown_media_feature",
            "the code is W105 unknown_media_feature");

    const Compilation mixed =
        compile("@media (hover: hover) and (prefers-color-scheme: dark), (prefers-color-scheme: dark) "
                "{ #a { opacity: 0.5 } }");
    require(mixed.outcome == CompileOutcome::compiled && mixed.sheet.rules.size() == 1U &&
                mixed.sheet.rules[0].media == dark() && mixed.diagnostics.size() == 1U &&
                mixed.diagnostics[0].code == DiagnosticCode::unknown_media_feature,
            "only the query naming the unknown feature is skipped");

    const Compilation checked = compile("@media (min-width: 1px) { #a { opacity: 2 } }");
    require(checked.outcome == CompileOutcome::refused && !checked.diagnostics.empty() &&
                checked.diagnostics.back().code == DiagnosticCode::value_out_of_range,
            "a skipped block's errors still refuse the sheet");
    const Compilation discarded = compile("@media (min-width: 1px) { #a { opacity: 0.5 } } #b { opacity: 0.5 }");
    require(discarded.outcome == CompileOutcome::compiled && discarded.sheet.rules.size() == 1U &&
                discarded.sheet.rules[0].selector.stable_id == "b",
            "a skipped block's checked rules are discarded");
}

void test_grouping_respects_media() {
    // A hover value under another condition gets its own instant machine,
    // so it holds only where its condition does.
    const Compilation separate = compile(
        "#a { opacity: 1; transition: opacity 200ms linear } "
        "@media (prefers-color-scheme: dark) { #a:hover { opacity: 0.5 } }");
    require(compiled_clean(separate) && separate.sheet.rules.size() == 2U,
            "a refinement under another condition is not grouped");
    if (separate.sheet.rules.size() == 2U) {
        require(separate.sheet.rules[0].transitions.size() == 1U &&
                    separate.sheet.rules[0].transitions[0].states.size() == 1U,
                "the host keeps only its own state");
        require(separate.sheet.rules[1].media == dark() && separate.sheet.rules[1].transitions.size() == 1U,
                "the refinement keeps its condition and its own machine");
    }
    const Compilation joined = compile(
        "@media (prefers-color-scheme: dark) { #a { opacity: 1; transition: opacity 200ms linear } "
        "#a:hover { opacity: 0.5 } }");
    require(compiled_clean(joined) && joined.sheet.rules.size() == 1U &&
                joined.sheet.rules[0].transitions.size() == 1U &&
                joined.sheet.rules[0].transitions[0].states.size() == 2U && joined.sheet.rules[0].media == dark(),
            "a refinement under the same condition joins the machine");
}

// -- refusals ------------------------------------------------------------------

struct Refusal final {
    std::string_view sheet{};
    DiagnosticCode code{};
    std::string_view detail{};
};

void test_refusals() {
    const std::array<Refusal, 22> refusals{{
        {"@media not all { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted, "not"},
        {"@media only screen { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted, "only"},
        {"@media print { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted, "print"},
        {"@media screen, print { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted, "print"},
        {"@media (forced-colors: active) or (prefers-color-scheme: dark) { #a { opacity: 1 } }",
         DiagnosticCode::value_form_not_admitted, "or"},
        {"@media screen or (forced-colors: active) { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted,
         "or"},
        {"@media (not (forced-colors: active)) { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted,
         "a nested media condition"},
        {"@media ((forced-colors: active)) { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted,
         "a nested media condition"},
        {"@media (forced-colors) { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted,
         "a media feature without a value"},
        {"@media (resolution >= 2x) { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted,
         "a media range"},
        {"@media (min-resolution: calc(2x)) { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted,
         "calc("},
        {"@media (prefers-color-scheme: dim) { #a { opacity: 1 } }", DiagnosticCode::keyword_unknown, "dim"},
        {"@media (forced-colors: 1) { #a { opacity: 1 } }", DiagnosticCode::value_wrong_kind, "1"},
        {"@media (min-resolution: 192dpi) { #a { opacity: 1 } }", DiagnosticCode::unit_not_admitted, "192dpi"},
        {"@media (min-resolution: 0x) { #a { opacity: 1 } }", DiagnosticCode::value_out_of_range, "0x"},
        {"@media (min-resolution: 2) { #a { opacity: 1 } }", DiagnosticCode::value_wrong_kind,
         "a resolution in dppx or x"},
        {"@media (prefers-color-scheme: dark), { #a { opacity: 1 } }", DiagnosticCode::value_syntax,
         "a media query after ','"},
        {"@media screen (prefers-color-scheme: dark) { #a { opacity: 1 } }", DiagnosticCode::value_syntax,
         "'and'"},
        {"@media (prefers-color-scheme: dark) (forced-colors: none) { #a { opacity: 1 } }",
         DiagnosticCode::value_syntax, "'and'"},
        {"@media (prefers-color-scheme dark) { #a { opacity: 1 } }", DiagnosticCode::value_syntax, "':'"},
        {"@media (prefers-color-scheme: dark light) { #a { opacity: 1 } }", DiagnosticCode::value_syntax, "')'"},
        {"@media and (forced-colors: none) { #a { opacity: 1 } }", DiagnosticCode::value_syntax,
         "a media type or feature"},
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
}

// -- the engine ----------------------------------------------------------------

void test_engine_conditions() {
    MediaFacts facts{};
    require(media_condition_holds(MediaCondition{}, facts), "the default condition holds");
    require(!media_condition_holds(dark(), facts), "a dark condition fails in a light window");
    facts.dark_scheme = true;
    require(media_condition_holds(dark(), facts), "and holds in a dark one");
    facts.resolution = 2.0;
    require(media_condition_holds(resolutions(2.0, 2.0), facts), "resolution bounds are inclusive");
    require(!media_condition_holds(resolutions(2.5, std::nullopt), facts) &&
                !media_condition_holds(resolutions(std::nullopt, 1.5), facts),
            "a resolution outside the bounds fails");
    require(media_condition_holds(resolutions(3.0, 1.0), MediaFacts{}) == false,
            "a minimum above the maximum holds nowhere");

    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const std::array<double, 4> bad{0.0, -1.0, std::numeric_limits<double>::infinity(),
                                    std::numeric_limits<double>::quiet_NaN()};
    for (const double bound : bad) {
        ThemeRuleSheet sheet{};
        ThemeRule rule{};
        rule.level = CascadeLevel::stable_id_rule;
        rule.selector.stable_id = "a";
        rule.declarations.push_back(RuleDeclaration::primitive(built_in_primitive_name));
        rule.media.minimum_resolution = bound;
        sheet.rules.push_back(rule);
        const CompiledThemeRules compiled = CompiledThemeRules::compile(sheet, registry);
        require(compiled.empty() && compiled.diagnostics().size() == 1U &&
                    compiled.diagnostics()[0].kind == RuleDiagnosticKind::media_invalid &&
                    rule_diagnostic_name(RuleDiagnosticKind::media_invalid) == "media_invalid",
                "a resolution bound not finite and above zero drops the rule");
    }
}

// -- windows -------------------------------------------------------------------

// A control showing one keyed face.
class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, 40.0, 24.0};
        part.key = PartKey{"face", {}, {}};
        return part;
    }
};

struct PadWindow final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
};

std::unique_ptr<PadWindow> make_pad_window(const std::shared_ptr<const Theme>& theme, double scale) {
    std::unique_ptr<PadWindow> made = std::make_unique<PadWindow>();
    PadWindow& held = *made;
    held.root = make_control<Panel>(StableId("root"));
    held.pad = make_control<Pad>(StableId("pad"));
    (*held.pad).set_requested_bounds(Rect{100.0, 100.0, 100.0, 80.0});
    (*held.root).add_child(held.pad);
    held.window = std::make_unique<Window>(held.root, Size{400.0, 300.0});
    (*held.window).set_scale(scale);
    (*held.window).set_theme(theme);
    return made;
}

std::shared_ptr<const Theme> theme_from(std::string_view text) {
    const Compilation compiled = compile(text);
    require(compiled_clean(compiled), std::string("the window's sheet compiles: ") + std::string(text));
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "theme-media-css-test";
    (*definition).rules = compiled.sheet;
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    require((*theme).engine().rules().diagnostics().empty(), "the theme compiles cleanly");
    return theme;
}

// The opacity the pad's face rests at in its window, or none without one.
std::optional<double> pad_opacity(const PadWindow& held) {
    const Pad& pad = *held.pad;
    const ThemeEngine::PartPlan plan = (*held.window).theme().engine().plan(pad, pad.face());
    for (std::size_t index = 0U; index < plan.resolution.transition_count; ++index) {
        const CompiledThemeRules::CompiledTransition& machine = *plan.resolution.transitions[index];
        if (machine.property == PresentationProperty::opacity && !machine.states.empty()) {
            return machine.states.back().target;
        }
    }
    return std::nullopt;
}

bool pad_recipe_authored(const PadWindow& held) {
    const bool authored =
        (*held.window).theme().resolve(*held.pad, ControlVisualRole::button, ControlVisualContext{}).authored();
    return authored;
}

void test_light_and_dark() {
    const std::shared_ptr<const Theme> theme =
        theme_from(":root { color-scheme: light dark; ink-on-paper: #f0f0f0 on #101010 } #pad { opacity: 0.75 } "
                   "@media (prefers-color-scheme: dark) { #pad { opacity: 0.5; color: role(ink-on-paper) } }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme, 1.0);
    Window& window = *(*held).window;
    require(pad_opacity(*held) == 0.75 && !pad_recipe_authored(*held), "a light window takes the light rules");
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = ColourScheme::dark;
    window.set_presentation_settings(settings);
    require(window_media_facts(window).dark_scheme, "the window's facts follow its settings");
    require(pad_opacity(*held) == 0.5 && pad_recipe_authored(*held), "switched dark, the dark rules hold");
    settings.colour_scheme = ColourScheme::light;
    window.set_presentation_settings(settings);
    require(pad_opacity(*held) == 0.75 && !pad_recipe_authored(*held), "and switched back, the light ones");

    settings.colour_scheme = static_cast<ColourScheme>(7);
    bool thrown = false;
    try {
        window.set_presentation_settings(settings);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown && window.presentation_settings().colour_scheme == ColourScheme::light,
            "a colour scheme outside the enumeration is refused");
}

void test_two_scales() {
    const std::shared_ptr<const Theme> theme = theme_from(
        "#pad { opacity: 0.75 } @media (min-resolution: 2x) { #pad { opacity: 0.5 } } "
        "@media (prefers-reduced-motion: reduce) { #pad { opacity: 0.25 } }");
    const std::unique_ptr<PadWindow> standard = make_pad_window(theme, 1.0);
    const std::unique_ptr<PadWindow> dense = make_pad_window(theme, 2.0);
    require(&(*(*standard).window).theme() == &(*(*dense).window).theme(), "both windows show one theme");
    require(pad_opacity(*standard) == 0.75, "the window at scale 1 takes the base rule");
    require(pad_opacity(*dense) == 0.5, "the window at scale 2 takes the dense rule");
    (*(*dense).window).set_scale(1.5);
    require(pad_opacity(*dense) == 0.75, "rescaled below the bound, it takes the base rule again");

    Window& calm = *(*standard).window;
    PresentationSettings settings = calm.presentation_settings();
    settings.reduced_motion = true;
    calm.set_presentation_settings(settings);
    require(pad_opacity(*standard) == 0.25 && pad_opacity(*dense) == 0.75,
            "reduced motion in one window changes only that window");
}

} // namespace

int main() {
    test_query_lowering();
    test_query_lists();
    test_unknown_features();
    test_grouping_respects_media();
    test_refusals();
    test_engine_conditions();
    test_light_and_dark();
    test_two_scales();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme media css tests passed\n";
    return 0;
}
