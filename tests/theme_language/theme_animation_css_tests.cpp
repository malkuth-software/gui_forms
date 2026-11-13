// CSS's animations (stage T3, phase P7b): `@keyframes` played by
// `animation` and its longhands. An animation run once becomes a machine
// state's entry value (`from`), stops (the keyframes between) and target
// (`to`); in a rule refining a transition's it joins that machine, with the
// transition's curve (owner decision, 2026-10-27, "strict for now"). A
// loop (`infinite`) becomes an expression of time_in_state for each
// parameter it moves, under the rule's ambient effect. Gate: a fixture per
// keyframe form and per loop timing function, the C++ ambient effect of
// engine_motion_tests written as a sheet giving the same values, and the
// forms the engine cannot run refused.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using std::chrono::milliseconds;
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

constexpr std::array<PartDeclaration, 1> pad_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = pad_parts, .states = {}, .custom_states = {}, .item_systems = {}},
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

const PropertyTransition* machine_of(const ThemeRule& rule, std::string_view property) {
    for (const PropertyTransition& machine : rule.transitions) {
        if (machine.property == property) return &machine;
    }
    return nullptr;
}

const PropertyTransition* machine_of(ThemeRule&& rule, std::string_view property) = delete;

const RuleDeclaration* declaration_of(const ThemeRule& rule, std::string_view property) {
    for (const RuleDeclaration& declaration : rule.declarations) {
        if (declaration.property == property) return &declaration;
    }
    return nullptr;
}

const RuleDeclaration* declaration_of(ThemeRule&& rule, std::string_view property) = delete;

MotionStop stop(double offset, double value) {
    MotionStop made{};
    made.offset = offset;
    made.value = value;
    return made;
}

// A machine of one state: `target`, entered from `from` through `stops`.
PropertyTransition entered(std::string_view property, std::optional<double> from, std::vector<MotionStop> stops,
                           double target, const MotionCurve& curve) {
    PropertyTransition machine{};
    machine.property = std::string(property);
    PresentationState state{};
    state.target = target;
    state.enter_from = from;
    state.stops = std::move(stops);
    machine.states.push_back(std::move(state));
    machine.curve = curve;
    return machine;
}

void require_refused(std::string_view sheet, DiagnosticCode code) {
    const Compilation compiled = compile(sheet);
    const bool refused = compiled.outcome == CompileOutcome::refused && !compiled.diagnostics.empty() &&
                         compiled.diagnostics.back().code == code;
    if (!refused && !compiled.diagnostics.empty()) {
        std::cerr << "  got " << diagnostic_code_name(compiled.diagnostics.back().code) << '\n';
    }
    require(refused, "refused (" + std::string(diagnostic_code_name(code)) + "): " + std::string(sheet));
}

// -- moves --------------------------------------------------------------------------

void test_moves() {
    const MotionCurve linear = MotionCurve::timed(milliseconds(200), MotionEasing::linear);
    const std::string_view fade =
        "@keyframes fade-in { from { opacity: 0 } to { opacity: 1 } }\n"
        "#pad::part(face) { animation: fade-in 200ms linear }\n";
    const Compilation fade_compiled = compile(fade);
    const ThemeRule& fade_rule = only_rule(fade_compiled, fade);
    require(fade_rule.transitions.size() == 1U &&
                fade_rule.transitions[0] == entered(opacity_property, 0.0, {}, 1.0, linear),
            "from is the state's entry value, to its target, the animation's curve the machine's");

    const std::string_view pulse =
        "@keyframes pulse { from { scale: 1 } 25% { scale: 1.1 } 50%, 75% { scale: 90% } to { scale: 1 } }\n"
        "#pad::part(face):hover { animation: pulse 300ms ease-in-out }\n";
    const Compilation pulse_compiled = compile(pulse);
    const ThemeRule& pulse_rule = only_rule(pulse_compiled, pulse);
    const std::vector<MotionStop> stops{stop(0.25, 1.1), stop(0.5, 0.9), stop(0.75, 0.9)};
    require(pulse_rule.transitions.size() == 1U &&
                pulse_rule.transitions[0] ==
                    entered(scale_property, 1.0, stops, 1.0,
                            MotionCurve::timed(milliseconds(300), MotionEasing::smoothstep)),
            "the keyframes between are the stops, a selector list placing one value at each offset");

    const std::string_view own =
        "@keyframes appear { from { opacity: 0 } }\n"
        "#pad::part(face) { opacity: 0.8; animation: appear 100ms }\n";
    const Compilation own_compiled = compile(own);
    const ThemeRule& own_rule = only_rule(own_compiled, own);
    require(own_rule.transitions.size() == 1U &&
                own_rule.transitions[0] ==
                    entered(opacity_property, 0.0, {}, 0.8, MotionCurve::timed(milliseconds(100), MotionEasing::ease)),
            "without `to` the target is the rule's own value, and the timing CSS's initial `ease`");

    const std::string_view rest =
        "@keyframes grow { from { scale: 0.5 } }\n"
        "#pad::part(face) { animation: grow 100ms linear }\n";
    const Compilation rest_compiled = compile(rest);
    const ThemeRule& rest_rule = only_rule(rest_compiled, rest);
    require(rest_rule.transitions.size() == 1U && rest_rule.transitions[0].states[0].target == 1.0 &&
                rest_rule.transitions[0].states[0].enter_from == 0.5,
            "without `to` or a value of its own, the target is the property's rest");

    const std::string_view slide =
        "@keyframes slide { from { translate: 4px -2px } }\n"
        "#pad::part(face) { animation: slide 100ms linear 20ms }\n";
    const Compilation slide_compiled = compile(slide);
    const ThemeRule& slide_rule = only_rule(slide_compiled, slide);
    const MotionCurve delayed = MotionCurve::timed(milliseconds(100), MotionEasing::linear).delayed(milliseconds(20));
    const PropertyTransition* const x = machine_of(slide_rule, translate_x_property);
    const PropertyTransition* const y = machine_of(slide_rule, translate_y_property);
    require(x != nullptr && y != nullptr && *x == entered(translate_x_property, 4.0, {}, 0.0, delayed) &&
                *y == entered(translate_y_property, -2.0, {}, 0.0, delayed),
            "translate's keyframes move both translations, the delay delaying the curve");

    const std::string_view rise =
        "@keyframes rise { from { amplitude: 0 } 50% { amplitude: 0.8 } to { amplitude: 0.5 } }\n"
        "#pad::part(face) { primitive: bulge; animation: rise 120ms linear }\n";
    const Compilation rise_compiled = compile(rise);
    const ThemeRule& rise_rule = only_rule(rise_compiled, rise);
    const PropertyTransition* const amplitude = machine_of(rise_rule, "amplitude");
    require(amplitude != nullptr &&
                *amplitude == entered("amplitude", 0.0, {stop(0.5, 0.8)}, 0.5,
                                      MotionCurve::timed(milliseconds(120), MotionEasing::linear)),
            "an animated parameter moves on a machine of its own");

    const std::string_view long_run =
        "@keyframes fade-in { from { opacity: 0 } }\n"
        "#pad::part(face) { animation: fade-in 2s linear }\n";
    const Compilation long_compiled = compile(long_run);
    const ThemeRule& long_rule = only_rule(long_compiled, long_run);
    require(long_rule.transitions.size() == 1U && long_rule.transitions[0].curve.duration == motion_duration_ceiling,
            "a move's duration is clamped to the motion ceiling, as a transition's is");
}

// -- keyframe forms ----------------------------------------------------------------

void test_keyframe_forms() {
    const MotionCurve linear = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    // Defined after the rule; a later definition of the name replaces the
    // earlier; two blocks of one offset merge, the later value winning.
    const std::string_view later =
        "#pad::part(face) { animation: k 100ms linear }\n"
        "@keyframes k { from { opacity: 0.1 } }\n"
        "@keyframes k { from { opacity: 0.2 } 50% { opacity: 0.3 } 50% { opacity: 0.4 } TO { opacity: 0.9 } }\n";
    const Compilation later_compiled = compile(later);
    const ThemeRule& later_rule = only_rule(later_compiled, later);
    require(later_rule.transitions.size() == 1U &&
                later_rule.transitions[0] == entered(opacity_property, 0.2, {stop(0.5, 0.4)}, 0.9, linear),
            "keyframes may follow their rule; the later definition and the later block of an offset win");

    const std::string_view quoted =
        "@keyframes \"two words\" { from { opacity: 0 } }\n"
        "#pad::part(face) { animation-name: \"two words\"; animation-duration: 100ms;"
        " animation-timing-function: linear }\n";
    const Compilation quoted_compiled = compile(quoted);
    const ThemeRule& quoted_rule = only_rule(quoted_compiled, quoted);
    require(quoted_rule.transitions.size() == 1U &&
                quoted_rule.transitions[0] == entered(opacity_property, 0.0, {}, 1.0, linear),
            "a string names keyframes, and the longhands set the lists");

    // Two animations: the later's track of a property wins.
    const std::string_view both =
        "@keyframes a { from { opacity: 0 } from { scale: 0.5 } }\n"
        "@keyframes b { from { opacity: 0.5 } }\n"
        "#pad::part(face) { animation: a 100ms linear, b 200ms linear }\n";
    const Compilation both_compiled = compile(both);
    const ThemeRule& both_rule = only_rule(both_compiled, both);
    const PropertyTransition* const opacity = machine_of(both_rule, opacity_property);
    const PropertyTransition* const scale = machine_of(both_rule, scale_property);
    require(opacity != nullptr && scale != nullptr &&
                *opacity == entered(opacity_property, 0.5, {}, 1.0,
                                    MotionCurve::timed(milliseconds(200), MotionEasing::linear)) &&
                *scale == entered(scale_property, 0.5, {}, 1.0, linear),
            "the later animation of a property wins; the other moves stay");

    const std::string_view none =
        "@keyframes a { from { opacity: 0 } }\n"
        "#pad::part(face) { opacity: 0.5; animation: a 100ms; animation: none }\n";
    const Compilation none_compiled = compile(none);
    const ThemeRule& none_rule = only_rule(none_compiled, none);
    require(none_rule.transitions.size() == 1U && !none_rule.transitions[0].states[0].enter_from.has_value(),
            "`animation: none` plays nothing");
    const std::string_view initial =
        "@keyframes a { from { opacity: 0 } }\n"
        "#pad::part(face) { opacity: 0.5; animation: a 100ms; animation-name: initial }\n";
    const Compilation initial_compiled = compile(initial);
    const ThemeRule& initial_rule = only_rule(initial_compiled, initial);
    require(initial_rule.transitions.size() == 1U && !initial_rule.transitions[0].states[0].enter_from.has_value(),
            "`initial` resets the names to none");

    const std::string_view defaults =
        "@keyframes a { from { opacity: 0 } }\n"
        "#pad::part(face) { animation: a 100ms linear 1 normal none running;"
        " animation-direction: normal; animation-fill-mode: none; animation-play-state: running }\n";
    const Compilation defaults_compiled = compile(defaults);
    const ThemeRule& defaults_rule = only_rule(defaults_compiled, defaults);
    require(defaults_rule.transitions.size() == 1U && defaults_rule.transitions[0].states[0].enter_from == 0.0,
            "the initial direction, fill mode, play state and count are admitted");

    const std::string_view unused = "@keyframes a { from { amplitude: 0.5 } 40% { sparkle: 2 } }";
    const Compilation unused_compiled = compile(unused);
    require(unused_compiled.outcome == CompileOutcome::compiled && unused_compiled.sheet.rules.empty() &&
                unused_compiled.diagnostics.empty(),
            "keyframes no rule plays declare nothing; their parameters are checked where they play");
}

// -- grouping ----------------------------------------------------------------------

void test_grouped_animation() {
    const MotionCurve curve = MotionCurve::timed(milliseconds(120), MotionEasing::linear);
    const std::string_view sheet =
        "@keyframes pop { 50% { scale: 1.1 } to { scale: 1.05 } }\n"
        "#pad::part(face) { scale: 1; transition: scale 120ms linear }\n"
        "#pad::part(face):hover { animation: pop 120ms linear }\n";
    const Compilation compiled = compile(sheet);
    const ThemeRule& host = only_rule(compiled, sheet);
    PropertyTransition expected{};
    expected.property = std::string(scale_property);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = 1.05;
    hot.stops.push_back(stop(0.5, 1.1));
    expected.states.push_back(hot);
    PresentationState own{};
    own.target = 1.0;
    expected.states.push_back(own);
    expected.curve = curve;
    require(host.transitions.size() == 1U && host.transitions[0] == expected,
            "an animation in a refining rule joins the transition's machine, keeping its stops");

    require_refused("@keyframes pop { to { scale: 1.05 } }\n"
                    "#pad::part(face) { scale: 1; transition: scale 120ms linear }\n"
                    "#pad::part(face):hover { animation: pop 200ms linear }\n",
                    DiagnosticCode::value_form_not_admitted);
    require_refused("@keyframes pop { to { scale: 1.05 } }\n"
                    "#pad::part(face) { scale: 1; transition: scale 120ms linear }\n"
                    "#pad::part(face):hover { animation: pop 120ms ease }\n",
                    DiagnosticCode::value_form_not_admitted);
    require_refused("@keyframes pop { to { scale: 1.05 } }\n"
                    "#pad::part(face) { scale: 1; transition: scale 120ms linear; animation: pop 120ms linear }\n",
                    DiagnosticCode::value_form_not_admitted);

    // Without a transition to join, the animation keeps a machine of its
    // own beside the less specific rule's.
    const std::string_view alone =
        "@keyframes flash { from { opacity: 0.2 } }\n"
        "#pad::part(face) { opacity: 0.9 }\n"
        "#pad::part(face):hover { animation: flash 100ms linear }\n";
    const Compilation alone_compiled = compile(alone);
    require(alone_compiled.outcome == CompileOutcome::compiled && alone_compiled.sheet.rules.size() == 2U,
            "an animation with no transition to join stays in its rule");
    if (alone_compiled.sheet.rules.size() == 2U) {
        require(alone_compiled.sheet.rules[1].transitions.size() == 1U &&
                    alone_compiled.sheet.rules[1].transitions[0] ==
                        entered(opacity_property, 0.2, {}, 1.0,
                                MotionCurve::timed(milliseconds(100), MotionEasing::linear)),
                "with its own curve");
    }
}

// -- loops -------------------------------------------------------------------------

// The loop's expression at `seconds` into its period.
double evaluate_at(const RuleDeclaration& declaration, double seconds) {
    EngineExpression expression{};
    const ExpressionLimits limits{.inputs = maximum_expression_inputs, .parameters = maximum_expression_parameters};
    const ExpressionCompilation compiled = expression.compile(declaration.program, declaration.number, limits);
    require(compiled.defect == ExpressionDefect::none, "the loop's expression compiles");
    std::array<double, maximum_expression_inputs> inputs{};
    inputs.fill(std::numeric_limits<double>::quiet_NaN());
    inputs[engine_input_position(EngineInput::time_in_state).value] = seconds;
    const std::array<double, maximum_expression_parameters> parameters{};
    ExpressionEnvironment environment{};
    environment.inputs = inputs;
    environment.parameters = parameters;
    const ExpressionResult result = expression.evaluate(environment);
    require(result.fallback == ExpressionFallback::none, "the loop's expression gives its value, not its fallback");
    return result.value;
}

struct LoopSample final {
    double seconds{};
    double value{};
};

// Compiles a loop of the amplitude over a period of 2 s and checks its
// ambient effect and its values at `samples`.
void require_loop(std::string_view keyframes, std::string_view timing, std::span<const LoopSample> samples) {
    const std::string sheet = std::string(keyframes) +
                              "\n#pad::part(face) { primitive: bulge; amplitude: 0.3; animation: k 2s " +
                              std::string(timing) + " infinite }\n";
    const Compilation compiled = compile(sheet);
    const ThemeRule& rule = only_rule(compiled, sheet);
    AmbientEffect ambient{};
    ambient.period = milliseconds(2000);
    ambient.frames_per_second = maximum_ambient_frames_per_second;
    require(rule.ambient == ambient, "the loop's ambient effect: its period, 30 frames a second, at rest at 0: " + sheet);
    const RuleDeclaration* const amplitude = declaration_of(rule, "amplitude");
    require(amplitude != nullptr && (*amplitude).kind == RuleValueKind::expression,
            "the amplitude is the loop's expression, replacing the rule's own value: " + sheet);
    require(rule.transitions.empty(), "a loop is no machine: " + sheet);
    if (amplitude == nullptr || (*amplitude).kind != RuleValueKind::expression) return;
    for (const LoopSample& sample : samples) {
        const double value = evaluate_at(*amplitude, sample.seconds);
        require(std::abs(value - sample.value) < 1e-12,
                "at " + std::to_string(sample.seconds) + " s the loop shows " + std::to_string(sample.value) +
                    ", not " + std::to_string(value) + ": " + sheet);
    }
}

void test_loops() {
    const std::string_view keyframes =
        "@keyframes k { from { amplitude: 0.1 } 25% { amplitude: 0.5 } to { amplitude: 0.9 } }";
    // Linear: 0.1 to 0.5 over 0 to 0.5 s, then to 0.9 over 0.5 to 2 s.
    const std::array<LoopSample, 5> linear{{
        {.seconds = 0.0, .value = 0.1},
        {.seconds = 0.25, .value = 0.3},
        {.seconds = 0.5, .value = 0.5},
        {.seconds = 1.25, .value = 0.7},
        {.seconds = 1.999, .value = 0.5 + 0.4 * (1.499 / 1.5)},
    }};
    require_loop(keyframes, "linear", linear);
    // ease-in-out is the engine's smoothstep over each interval.
    const double quarter = 0.25 * 0.25 * (3.0 - 2.0 * 0.25);
    const std::array<LoopSample, 3> smooth{{
        {.seconds = 0.125, .value = 0.1 + 0.4 * quarter},
        {.seconds = 0.5, .value = 0.5},
        {.seconds = 1.25, .value = 0.7},
    }};
    require_loop(keyframes, "ease-in-out", smooth);
    // steps(2) jumps at the end of each half interval; jump-start at its
    // start.
    const std::array<LoopSample, 4> end{{
        {.seconds = 0.0, .value = 0.1},
        {.seconds = 0.24, .value = 0.1},
        {.seconds = 0.26, .value = 0.3},
        {.seconds = 0.5, .value = 0.5},
    }};
    require_loop(keyframes, "steps(2)", end);
    const std::array<LoopSample, 4> start{{
        {.seconds = 0.0, .value = 0.3},
        {.seconds = 0.26, .value = 0.5},
        {.seconds = 0.5, .value = 0.7},
        {.seconds = 1.999, .value = 0.9},
    }};
    require_loop(keyframes, "steps(2, jump-start)", start);
    // Without from and to, the loop starts and ends at the rule's own value.
    const std::array<LoopSample, 3> own{{
        {.seconds = 0.0, .value = 0.3},
        {.seconds = 1.0, .value = 0.7},
        {.seconds = 1.5, .value = 0.5},
    }};
    require_loop("@keyframes k { 50% { amplitude: 0.7 } }", "linear", own);

    // The engine-named longhands set the rest of the ambient effect.
    const std::string_view named =
        "@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }\n"
        "#pad::part(face) { primitive: bulge; animation: k 1500ms linear infinite;"
        " animation-frame-rate: 12; animation-rest-pose: 300ms }\n";
    const Compilation named_compiled = compile(named);
    const ThemeRule& named_rule = only_rule(named_compiled, named);
    AmbientEffect expected{};
    expected.period = milliseconds(1500);
    expected.frames_per_second = 12.0;
    expected.pose = milliseconds(300);
    require(named_rule.ambient == expected, "animation-frame-rate and animation-rest-pose set the ambient effect");
}

// engine_motion_tests' ambient_rule(): amplitude = 0.125 + time_in_state / 2
// over a period of 1 s, at most 30 frames a second, resting at 250 ms.
ThemeRule written_ambient_rule() {
    ThemeRule rule{};
    rule.selector.stable_id = "pad";
    rule.declarations.push_back(RuleDeclaration::primitive("bulge"));
    const std::array<ExpressionInstruction, 5> program{
        ExpressionInstruction::input(engine_input_position(EngineInput::time_in_state)),
        ExpressionInstruction::number(0.5), ExpressionInstruction::apply(ExpressionOperation::multiply),
        ExpressionInstruction::number(0.125), ExpressionInstruction::apply(ExpressionOperation::add)};
    rule.declarations.push_back(RuleDeclaration::expression_value("amplitude", program, 0.125));
    AmbientEffect effect{};
    effect.period = milliseconds(1000);
    effect.frames_per_second = 30.0;
    effect.pose = milliseconds(250);
    rule.ambient = effect;
    return rule;
}

void test_ambient_gate() {
    const std::string_view sheet =
        "@keyframes breathe { from { amplitude: 0.125 } to { amplitude: 0.625 } }\n"
        "#pad { primitive: bulge; animation: breathe 1s linear infinite; animation-rest-pose: 250ms }\n";
    const Compilation compiled = compile(sheet);
    const ThemeRule& rule = only_rule(compiled, sheet);
    const ThemeRule written = written_ambient_rule();
    require(rule.ambient == written.ambient, "the sheet's ambient effect is the C++ rule's");
    const RuleDeclaration* const from_sheet = declaration_of(rule, "amplitude");
    const RuleDeclaration* const from_code = declaration_of(written, "amplitude");
    require(from_sheet != nullptr && from_code != nullptr, "both rules declare the amplitude");
    if (from_sheet == nullptr || from_code == nullptr) return;
    // Every frame of the loop at 30 frames a second, and the rest pose:
    // the same value to the bit.
    for (std::size_t frame = 0U; frame < 30U; ++frame) {
        const double seconds = static_cast<double>(frame) / 30.0;
        require(evaluate_at(*from_sheet, seconds) == evaluate_at(*from_code, seconds),
                "the sheet's loop gives the C++ loop's amplitude at frame " + std::to_string(frame));
    }
    require(evaluate_at(*from_sheet, 0.25) == 0.25, "and rests at the C++ rule's pose");
}

// -- refusals ----------------------------------------------------------------------

struct Refusal final {
    std::string_view sheet{};
    DiagnosticCode code{};
};

void test_refusals() {
    const std::array<Refusal, 40> refusals{{
        // The animation lists.
        {"#a { animation: missing 1s }", DiagnosticCode::keyframes_undefined},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s 2 }", DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s; animation-iteration-count: -1 }",
         DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s reverse }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s forwards }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s paused }", DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s; animation-direction: sideways }",
         DiagnosticCode::keyword_unknown},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k spring(90ms, 1) }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k -1s }", DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s linear -10ms }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s linear 1ms 2ms }", DiagnosticCode::value_syntax},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k j 1s }", DiagnosticCode::value_syntax},
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 3px }", DiagnosticCode::value_wrong_kind},
        {"@keyframes k { from { opacity: 0 } } #a { animation: inherit }", DiagnosticCode::value_wrong_kind},
        // Moves.
        {"@keyframes k { 10% { opacity: 0 } 20% { opacity: 0 } 30% { opacity: 0 } 40% { opacity: 0 }"
         " 50% { opacity: 0 } } #a { animation: k 1s }",
         DiagnosticCode::too_many_layers},
        {"@keyframes k { from { exponent: 2 } } #a { primitive: bulge; animation: k 1s }",
         DiagnosticCode::value_wrong_kind},
        {"@keyframes k { from { sparkle: 2 } } #a { primitive: bulge; animation: k 1s }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } } #a { animation: k 1s }", DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } } #a { primitive: bulge; animation: k 1s }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 2 } to { amplitude: 1 } } #a { primitive: bulge; animation: k 1s }",
         DiagnosticCode::value_out_of_range},
        // Loops.
        {"@keyframes k { from { opacity: 0 } } #a { animation: k 1s linear infinite }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } } #a { animation: k 1s linear infinite }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }"
         " #a { primitive: bulge; animation: k 1s linear 10ms infinite }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } } #a { primitive: bulge; animation: k linear infinite }",
         DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }"
         " #a { primitive: bulge; animation: k 61s linear infinite }",
         DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }"
         " #a { primitive: bulge; animation: k 1s linear infinite; animation-rest-pose: 1s }",
         DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }"
         " #a { primitive: bulge; animation: k 1s linear infinite; animation-frame-rate: 31 }",
         DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }"
         " #a { primitive: bulge; animation: k 1s linear infinite; animation-frame-rate: 0 }",
         DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } } #a { primitive: bulge; animation: k 1s infinite }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } }"
         " #a { primitive: bulge; animation: k 1s cubic-bezier(0.1, 0, 0.9, 1) infinite }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { from { amplitude: 0 } 50% { amplitude: 1 } to { amplitude: 0 } }"
         " #a { primitive: bulge; animation: k 1s steps(6) infinite }",
         DiagnosticCode::expression_over_budget},
        {"@keyframes k { from { amplitude: 0 } to { amplitude: 1 } } @keyframes j { from { exponent: 2 } to { exponent: 3 } }"
         " #a { primitive: bulge; animation: k 1s linear infinite, j 2s linear infinite }",
         DiagnosticCode::value_form_not_admitted},
        {"@keyframes k { 50% { amplitude: 1 } } #a { primitive: bulge; animation: k 1s linear infinite }",
         DiagnosticCode::value_form_not_admitted},
        // The keyframes themselves.
        {"@keyframes none { from { opacity: 0 } }", DiagnosticCode::value_syntax},
        {"@keyframes a b { from { opacity: 0 } }", DiagnosticCode::value_syntax},
        {"@keyframes k { 150% { opacity: 0 } }", DiagnosticCode::value_out_of_range},
        {"@keyframes k { 50 { opacity: 0 } }", DiagnosticCode::selector_syntax},
        {"@keyframes k { from, { opacity: 0 } }", DiagnosticCode::selector_syntax},
        {"@keyframes k { from { opacity: 2 } }", DiagnosticCode::value_out_of_range},
        {"@keyframes k { from { transition: none } }", DiagnosticCode::value_form_not_admitted},
    }};
    for (const Refusal& refusal : refusals) require_refused(refusal.sheet, refusal.code);
    require_refused("@keyframes k { from { amplitude: calc(var(--pointer-x) / 1px) } }",
                    DiagnosticCode::value_form_not_admitted);
    require_refused("@keyframes k { from { amplitude: red } }", DiagnosticCode::value_wrong_kind);
}

void test_keyframes_limit() {
    // 64 @keyframes rules pass; one more is refused (R2-14).
    std::string sheet{};
    for (std::size_t index = 0U; index < theme_language::maximum_keyframes_rules; ++index) {
        sheet += "@keyframes k" + std::to_string(index) + " { from { opacity: 0 } }\n";
    }
    const Compilation at_limit = compile(sheet);
    require(at_limit.outcome == CompileOutcome::compiled, "64 @keyframes rules compile");
    sheet += "@keyframes extra { from { opacity: 0 } }\n";
    require_refused(sheet, DiagnosticCode::too_many_keyframes);
}

void test_warnings() {
    const std::string_view sheet =
        "@keyframes k { from { opacity: 0; float: left } }\n"
        "#pad::part(face) { animation: k 100ms linear; animation-composition: add }\n";
    const Compilation compiled = compile(sheet);
    require(compiled.outcome == CompileOutcome::compiled && compiled.diagnostics.size() == 2U &&
                compiled.diagnostics[0].code == DiagnosticCode::property_not_admitted &&
                compiled.diagnostics[1].code == DiagnosticCode::property_not_admitted,
            "properties the language leaves out are dropped with a warning, in keyframes too");
    const ThemeRule& rule = only_rule(compiled, sheet);
    require(rule.transitions.size() == 1U && rule.transitions[0].states[0].enter_from == 0.0,
            "and the rest applies");
}

} // namespace

int main() {
    test_moves();
    test_keyframe_forms();
    test_grouped_animation();
    test_loops();
    test_ambient_gate();
    test_refusals();
    test_keyframes_limit();
    test_warnings();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
