// The theme compiler (stage T3, phase P5): the first end-to-end
// compilation, a sheet in and a Theme out through Window::set_theme. Gate: a
// sheet written to match a C++-authored test theme compiles to equal
// ThemeRuleSheet records, and the parts the two themes draw hash equal. The
// rest checks each lowered property, each refusal with its column, each
// warning, and the lowered-size limit; and that the engine accepts every
// sheet the compiler accepts, with no diagnostic of its own.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/rgba_order.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/engine_expression/engine_expression.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "../support/routed_input_probe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
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
using theme_language::ThemeSource;
using theme_language::diagnostic_code_label;
using theme_language::diagnostic_code_name;
using theme_language::maximum_lowered_bytes;
using Instruction = ExpressionInstruction;
using Operation = ExpressionOperation;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A colour parameter a sheet wrote as a literal (EC4).
RuleDeclaration literal_colour(std::string_view name, Color colour) {
    RuleDeclaration declaration = RuleDeclaration::colour_value(name, colour);
    declaration.origin.source = ColourSource::literal;
    return declaration;
}

// -- the vocabulary and the compilation -------------------------------------------

constexpr std::array<PartDeclaration, 2> button_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
    {.name = "label", .role = PartRole::label, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = button_parts, .states = {}, .custom_states = {}, .item_systems = {}},
}};
const std::array<std::string, 1> published{{"progress"}};
const std::array<std::string_view, 1> resources{{"icon.check"}};

struct Options final {
    const PrimitiveRegistry* registry{};
    EngineVersion requires_engine{current_engine_version};
    // The effects an `effect` may name; null checks the name's form only.
    const EffectRegistry* effects{};
};

struct Compilation final {
    CompileOutcome outcome{CompileOutcome::refused};
    ThemeRuleSheet sheet{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

// Compiles `text`. A compiled sheet must also compile in the engine with no
// diagnostic: the compiler checks every condition the engine does first.
Compilation compile(std::string_view text, Options options = Options{}) {
    const PrimitiveRegistry& registry =
        options.registry == nullptr ? builtin_primitive_registry() : *options.registry;
    ThemeSource source{};
    source.file = "test.theme.css";
    source.text = text;
    ThemeCompileInput input{.source = source,
                            .registry = registry,
                            .vocabulary = SelectorVocabulary{},
                            .published_values = published,
                            .resources = resources,
                            .requires_engine = options.requires_engine,
                            .effects = options.effects};
    input.vocabulary.controls = controls;
    ThemeCompiler compiler{};
    ThemeDiagnosticList diagnostics{};
    Compilation result{};
    result.outcome = compiler.compile(input, result.sheet, diagnostics);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    if (result.outcome == CompileOutcome::compiled) {
        const CompiledThemeRules engine = CompiledThemeRules::compile(result.sheet, registry);
        for (const RuleDiagnostic& diagnostic : engine.diagnostics()) {
            std::cerr << "  engine: " << rule_diagnostic_name(diagnostic.kind) << " " << diagnostic.property << '\n';
        }
        require(engine.diagnostics().empty(), std::string("the engine accepts the compiled sheet: ") + std::string(text));
    } else {
        require(result.sheet.rules.empty() && result.sheet.published_values.empty(), "a refused sheet is empty");
    }
    return result;
}

void print_diagnostics(const Compilation& compilation) {
    for (const ThemeDiagnostic& diagnostic : compilation.diagnostics) {
        std::cerr << "  got " << diagnostic_code_label(diagnostic.code) << ' ' << diagnostic_code_name(diagnostic.code)
                  << " at " << diagnostic.position.line.value << ':' << diagnostic.position.column.value << " ("
                  << diagnostic.detail << ")\n";
    }
}

// The 1-based column of `marker` in the one-line `text`.
std::uint32_t column_of(std::string_view text, std::string_view marker) {
    const std::size_t at = text.find(marker);
    require(at != std::string_view::npos, std::string("the marker is in the sheet: ") + std::string(marker));
    return static_cast<std::uint32_t>(at + 1U);
}

// -- the gate ----------------------------------------------------------------------

constexpr std::string_view gate_sheet =
    "/* The P5 gate: the sheet reference_sheet() builds by hand. */\n"
    "#pad::part(face) {\n"
    "  --lift: 0.25;\n"
    "  primitive: bulge;\n"
    "  amplitude: clamp(0, var(--pointer-x) / 400px + var(--lift), 1);\n"
    "  exponent: 3;\n"
    "  light-colour: rgb(255 240 200);\n"
    "  shade-colour: steelblue;\n"
    "  opacity: 90%;\n"
    "  mix-blend-mode: multiply;\n"
    "}\n"
    "#pad::part(face):hover {\n"
    "  primitive: bulge;\n"
    "  exponent: 4;\n"
    "  exponent: 5;\n"
    "  scale: 1.25;\n"
    "  translate: 2px -1px;\n"
    "}\n"
    "#plate::part(face) {\n"
    "  primitive: material;\n"
    "  recipe: accent-button;\n"
    "  shape: pill;\n"
    "}\n";

PropertyTransition still(std::string_view property, double target) {
    PropertyTransition machine{};
    machine.property = std::string(property);
    PresentationState state{};
    state.target = target;
    machine.states.push_back(state);
    machine.curve = MotionCurve::timed(FrameInterval{}, MotionEasing::linear);
    return machine;
}

ThemeRule face_rule(std::string label, std::string stable_id) {
    ThemeRule rule{};
    rule.label = std::move(label);
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::move(stable_id);
    rule.selector.part = "face";
    return rule;
}

// The theme as C++ writes it, in the shape of engine_input_tests.cpp's
// bulge rules: amplitude = max(0, min(pointer_x / 400 + 0.25, 1)), CSS's
// clamp(), falling back to the parameter's default.
ThemeRuleSheet reference_sheet() {
    ThemeRuleSheet sheet{};
    sheet.published_values = {"progress"};
    ThemeRule pad = face_rule("#pad::part(face)", "pad");
    pad.declarations.push_back(RuleDeclaration::primitive("bulge"));
    const std::array<Instruction, 9> amplitude{
        Instruction::number(0.0),
        Instruction::input(engine_input_position(EngineInput::pointer_x)),
        Instruction::number(400.0),
        Instruction::apply(Operation::divide),
        Instruction::number(0.25),
        Instruction::apply(Operation::add),
        Instruction::number(1.0),
        Instruction::apply(Operation::minimum),
        Instruction::apply(Operation::maximum),
    };
    pad.declarations.push_back(RuleDeclaration::expression_value("amplitude", amplitude, 1.0));
    pad.declarations.push_back(RuleDeclaration::number_value("exponent", 3.0));
    pad.declarations.push_back(literal_colour("light_colour", Color::rgba(255, 240, 200, 255)));
    pad.declarations.push_back(literal_colour("shade_colour", Color::rgba(70, 130, 180, 255)));
    pad.declarations.push_back(RuleDeclaration::choice_value(blend_property, "multiply"));
    pad.transitions.push_back(still(opacity_property, 0.9));
    sheet.rules.push_back(pad);

    ThemeRule hot = face_rule("#pad::part(face):hover", "pad");
    hot.selector.states.push_back(AnatomyState::hot);
    hot.declarations.push_back(RuleDeclaration::primitive("bulge"));
    hot.declarations.push_back(RuleDeclaration::number_value("exponent", 5.0));
    hot.transitions.push_back(still(scale_property, 1.25));
    hot.transitions.push_back(still(translate_x_property, 2.0));
    hot.transitions.push_back(still(translate_y_property, -1.0));
    sheet.rules.push_back(hot);

    ThemeRule plate = face_rule("#plate::part(face)", "plate");
    plate.declarations.push_back(RuleDeclaration::primitive("material"));
    plate.declarations.push_back(RuleDeclaration::choice_value("recipe", "accent_button"));
    plate.declarations.push_back(RuleDeclaration::choice_value("shape", "pill"));
    sheet.rules.push_back(plate);
    return sheet;
}

std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : gui_forms::test_support::rgba_order(bytes)) {
        hash = hash ^ byte;
        hash = hash * 0x100000001b3ULL;
    }
    return hash;
}

// A painter that writes down what it is asked to draw: each part raster by
// its pixels' hash, where and at what opacity, and each group and offset.
class PaintLog final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point offset) override {
        lines.push_back("translate " + std::to_string(offset.x) + " " + std::to_string(offset.y));
    }
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point at, double opacity) override {
        const std::uint64_t hash = hash_bytes((*raster).pixels());
        lines.push_back("raster " + std::to_string(hash) + " at " + std::to_string(at.x) + " " +
                        std::to_string(at.y) + " opacity " + std::to_string(opacity));
        ++rasters;
    }
    void begin_group(const PaintGroup& group) override {
        lines.push_back("group " + std::to_string(group.opacity) + " " +
                        std::to_string(static_cast<int>(group.composite)) + " " + std::to_string(group.isolated));
    }
    void end_group() override { lines.push_back("end group"); }
    std::vector<std::string> lines;
    std::size_t rasters{};
};

// A control showing one face part, hot while the pointer is over it.
class Pad final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    explicit Pad(StableId stable_id) : gui_forms::tests::RoutedInputProbe<Control>(std::move(stable_id)) {}
    void routed_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::enter) {
            hot = true;
            invalidate(Dirty::paint);
        }
        if (event.action == PointerAction::leave) {
            hot = false;
            invalidate(Dirty::paint);
        }
    }
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{4.0, 4.0, 72.0, 28.0};
        part.key = PartKey{"face", {}, {}};
        if (hot) part.states.insert(AnatomyState::hot);
        paint_part(painter, *this, part);
    }
    bool hot{};
};

struct GateWindow final {
    std::shared_ptr<Panel> root;
    std::unique_ptr<Window> window;
};

std::unique_ptr<GateWindow> gate_window(const ThemeRuleSheet& rules) {
    std::unique_ptr<GateWindow> made = std::make_unique<GateWindow>();
    (*made).root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*pad).set_requested_bounds(Rect{20.0, 20.0, 80.0, 36.0});
    std::shared_ptr<Pad> plate = make_control<Pad>(StableId("plate"));
    (*plate).set_requested_bounds(Rect{20.0, 80.0, 80.0, 36.0});
    (*(*made).root).add_child(pad);
    (*(*made).root).add_child(plate);
    (*made).window = std::make_unique<Window>((*made).root, Size{200.0, 140.0});
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "theme-compiler-gate";
    definition.rules = rules;
    (*(*made).window).set_theme(Theme::create(definition));
    require((*(*made).window).theme().engine().rules().diagnostics().empty(), "the gate theme compiles cleanly");
    return made;
}

// What a fresh window with `rules` draws once the pointer has moved to
// `pointer`: its first paint, which covers the whole window.
std::vector<std::string> paint_with_pointer(const ThemeRuleSheet& rules, Point pointer) {
    std::unique_ptr<GateWindow> made = gate_window(rules);
    Window& window = *(*made).window;
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, pointer}));
    window.perform_layout();
    PaintLog painter{};
    static_cast<void>(window.paint(painter));
    require(painter.rasters >= 2U, "both parts draw a raster");
    return painter.lines;
}

void test_gate() {
    const Compilation compiled = compile(gate_sheet);
    print_diagnostics(compiled);
    require(compiled.outcome == CompileOutcome::compiled && compiled.diagnostics.empty(),
            "the gate sheet compiles with no diagnostic");
    const ThemeRuleSheet reference = reference_sheet();
    // The sheet names custom properties, so its rules carry what an inline
    // style's definitions need (P12b): the names each reads, and their
    // source positions, in source order. The hand-built records predate
    // them.
    ThemeRuleSheet lowered = compiled.sheet;
    std::uint32_t previous = 0U;
    bool ordered = true;
    for (ThemeRule& rule : lowered.rules) {
        if (rule.source_position <= previous) ordered = false;
        previous = rule.source_position;
        rule.source_position = 0U;
    }
    require(ordered, "the gate rules' source positions follow the source");
    const std::vector<std::string> pad_reads{"--lift"};
    require(!lowered.rules.empty() && lowered.rules[0].reads == pad_reads, "the pad rule reads --lift");
    for (ThemeRule& rule : lowered.rules) rule.reads.clear();
    require(lowered == reference, "the gate sheet lowers to the hand-built records");
    if (compiled.sheet.rules.size() == reference.rules.size()) {
        for (std::size_t index = 0U; index < reference.rules.size(); ++index) {
            const ThemeRule& got = compiled.sheet.rules[index];
            const ThemeRule& want = reference.rules[index];
            require(got.declarations == want.declarations, "rule " + std::to_string(index) + ": declarations");
            require(got.transitions == want.transitions, "rule " + std::to_string(index) + ": transitions");
            require(got.selector == want.selector && got.level == want.level && got.label == want.label &&
                        got.specificity == want.specificity,
                    "rule " + std::to_string(index) + ": selector, level and label");
        }
    }

    // At rest, then hot with the pointer over the pad at two places (the
    // amplitude reads it), and away from both parts.
    const std::array<Point, 4> pointers{{Point{190.0, 130.0}, Point{30.0, 30.0}, Point{90.0, 40.0}, Point{60.0, 100.0}}};
    std::array<std::vector<std::string>, 4> drawn{};
    for (std::size_t index = 0U; index < pointers.size(); ++index) {
        const Point pointer = pointers[index];
        drawn[index] = paint_with_pointer(compiled.sheet, pointer);
        const std::vector<std::string> built = paint_with_pointer(reference, pointer);
        require(drawn[index] == built, "with the pointer at " + std::to_string(pointer.x) + ", " +
                                           std::to_string(pointer.y) + ", the two themes draw the same parts");
    }
    // The comparison is not vacuous: the hot rule and the pointer each change
    // what is drawn.
    require(drawn[0] != drawn[1] && drawn[1] != drawn[2], "the hover rule and the pointer change the pad");
}

// -- each lowered property ---------------------------------------------------------

// The one rule `text` compiles to.
ThemeRule only_rule(std::string_view text) {
    const Compilation compiled = compile(text);
    print_diagnostics(compiled);
    require(compiled.outcome == CompileOutcome::compiled && compiled.sheet.rules.size() == 1U,
            std::string("one rule: ") + std::string(text));
    if (compiled.sheet.rules.size() != 1U) return ThemeRule{};
    return compiled.sheet.rules[0];
}

bool holds_still(const ThemeRule& rule, std::size_t index, std::string_view property, double target) {
    if (index >= rule.transitions.size()) return false;
    const bool same = rule.transitions[index] == still(property, target);
    return same;
}

void test_properties() {
    require(holds_still(only_rule("#a { opacity: 0.5 }"), 0U, opacity_property, 0.5), "opacity as a number");
    require(holds_still(only_rule("#a { opacity: 25% }"), 0U, opacity_property, 0.25), "opacity as a percentage");
    require(holds_still(only_rule("#a { opacity: calc(1 / 4) }"), 0U, opacity_property, 0.25),
            "a constant expression is folded to a number");
    require(holds_still(only_rule("#a { opacity: initial }"), 0U, opacity_property, 1.0), "opacity: initial");
    require(holds_still(only_rule("#a { scale: none }"), 0U, scale_property, 1.0), "scale: none");
    require(holds_still(only_rule("#a { scale: 150% }"), 0U, scale_property, 1.5), "scale as a percentage");
    const ThemeRule one_axis = only_rule("#a { translate: 4px }");
    require(holds_still(one_axis, 0U, translate_x_property, 4.0) && holds_still(one_axis, 1U, translate_y_property, 0.0),
            "one length translates across, and not down");
    const ThemeRule unitless = only_rule("#a { translate: 0 -3px }");
    require(holds_still(unitless, 0U, translate_x_property, 0.0) &&
                holds_still(unitless, 1U, translate_y_property, -3.0),
            "a zero length may omit its unit");
    const ThemeRule none = only_rule("#a { translate: none }");
    require(holds_still(none, 0U, translate_x_property, 0.0) && holds_still(none, 1U, translate_y_property, 0.0),
            "translate: none");
    const ThemeRule blend = only_rule("#a { MIX-BLEND-MODE: Screen; isolation: isolate }");
    require(blend.declarations.size() == 1U &&
                blend.declarations[0] == RuleDeclaration::choice_value(blend_property, "screen") &&
                blend.transitions.empty(),
            "a blend mode, matched in any case; isolation adds nothing");
    const Compilation isolated = compile("#a { isolation: auto } #b { --x: 1px; }");
    require(isolated.outcome == CompileOutcome::compiled && isolated.sheet.rules.empty(),
            "a rule with nothing to declare gives no rule");

    const ThemeRule last = only_rule("#a { opacity: 0.5; scale: 1.5; opacity: 0.75 }");
    require(last.transitions.size() == 2U && holds_still(last, 0U, scale_property, 1.5) &&
                holds_still(last, 1U, opacity_property, 0.75),
            "the later declaration of a property wins, in its place");
    const ThemeRule built_in = only_rule("#a { primitive: bulge; primitive: built-in }");
    require(built_in.declarations.size() == 1U &&
                built_in.declarations[0] == RuleDeclaration::primitive(built_in_primitive_name),
            "the last primitive wins; built-in selects the built-in recipe");
    const ThemeRule initial = only_rule("#a { primitive: initial }");
    require(initial.declarations.size() == 1U &&
                initial.declarations[0] == RuleDeclaration::primitive(built_in_primitive_name),
            "primitive: initial is the built-in recipe");
    const ThemeRule defaults =
        only_rule("#a { exponent: initial; primitive: bulge; light-colour: initial; amplitude: 1px }");
    require(defaults.declarations.size() == 4U &&
                defaults.declarations[1] == RuleDeclaration::number_value("exponent", 2.2) &&
                defaults.declarations[2] == RuleDeclaration::colour_value("light_colour", Color::rgba(255, 255, 255, 255)) &&
                defaults.declarations[3] == RuleDeclaration::number_value("amplitude", 1.0),
            "parameters before the primitive belong to it; initial is the default; px is a number");
    const ThemeRule choice = only_rule("#a { primitive: material; recipe: initial; shape: CAP-START }");
    require(choice.declarations.size() == 3U &&
                choice.declarations[1] == RuleDeclaration::choice_value("recipe", "family") &&
                choice.declarations[2] == RuleDeclaration::choice_value("shape", "cap_start"),
            "a choice's initial is its default; keywords match in any case");
    const ThemeRule named = only_rule("#a { primitive: bulge; light-colour: transparent }");
    require(named.declarations.size() == 2U &&
                named.declarations[1] == literal_colour("light_colour", Color::rgba(0, 0, 0, 0)),
            "a named colour");
    const ThemeRule state = only_rule("#a { primitive: bulge; amplitude: calc(state(hot) * 0.5 + param(exponent) / 10) }");
    require(state.declarations.size() == 2U && state.declarations[1].kind == RuleValueKind::expression &&
                state.declarations[1].number == 1.0,
            "an expression may read a state and a number parameter, falling back to the default");
    const ThemeRule published_value = only_rule("#a { primitive: bulge; amplitude: var(--progress) }");
    require(published_value.declarations.size() == 2U &&
                published_value.declarations[1].kind == RuleValueKind::expression,
            "a published value is an input");

    const Compilation listed = compile(".a, .b:is(.c, .d) { opacity: 0.5 }");
    require(listed.outcome == CompileOutcome::compiled && listed.sheet.rules.size() == 3U, "a list expands");
    if (listed.sheet.rules.size() == 3U) {
        const ThemeRule& first = listed.sheet.rules[0];
        const ThemeRule& third = listed.sheet.rules[2];
        require(first.selector.classes == std::vector<std::string>{"a"} && first.level == CascadeLevel::theme_sheet &&
                    !first.specificity && first.label == ".a, .b:is(.c, .d)",
                "each expanded selector is one rule with the rule's label");
        require(third.selector.classes == (std::vector<std::string>{"b", "d"}) && !third.specificity &&
                    holds_still(third, 0U, opacity_property, 0.5),
                "each copy holds the rule's declarations");
    }
    const Compilation given = compile(":is(#x, .y).z { opacity: 0.5 }");
    require(given.outcome == CompileOutcome::compiled && given.sheet.rules.size() == 2U,
            "an :is() of two arguments gives two rules");
    if (given.sheet.rules.size() == 2U) {
        const ThemeRule& own = given.sheet.rules[0];
        const ThemeRule& lifted = given.sheet.rules[1];
        require(!own.specificity && lifted.specificity && (*lifted.specificity).ids == 1U &&
                    lifted.level == CascadeLevel::theme_sheet,
                "an :is() gives its most specific argument's, given only where the selector's own differs");
    }
}

// -- refusals ----------------------------------------------------------------------

struct Refusal final {
    std::string_view sheet{};
    DiagnosticCode code{};
    std::string_view marker{};
};

void test_refusals() {
    const std::array<Refusal, 42> refusals{{
        {"#a { --x: 0.5 } #b { --x: 1 } @keyframes k { from { opacity: var(--x) } }",
         DiagnosticCode::variable_cannot_vary, "--x) }"},
        {"#a { primitive: bulge; amplitude: 0.5 !important }", DiagnosticCode::value_form_not_admitted, "amplitude"},
        {"@import \"x\"; #a { opacity: 1 }", DiagnosticCode::import_not_installed, "\"x\""},
        {"#a { opacity: 1 } @media print { #a { opacity: 1 } }", DiagnosticCode::value_form_not_admitted, "print"},
        {"@keyframes k { from { color: red } }", DiagnosticCode::value_form_not_admitted, "color"},
        {"#a { backdrop-filter: blur(2px) }", DiagnosticCode::not_yet_built, "backdrop-filter"},
        {"#a { border-image-slice: 1 }", DiagnosticCode::not_yet_built, "border"},
        {"#a { animation: fade 1s }", DiagnosticCode::keyframes_undefined, "fade"},
        {"#a { font: huge }", DiagnosticCode::keyword_unknown, "huge"},
        {"#a { effect: wobble_band }", DiagnosticCode::keyword_unknown, "wobble_band"},
        {"#a { effect: inherit }", DiagnosticCode::value_wrong_kind, "inherit"},
        {"#a { effect: 3 }", DiagnosticCode::value_wrong_kind, "3"},
        {"#a { effect-primary: red }", DiagnosticCode::value_wrong_kind, "red"},
        {"#a { effect-secondary: inherit }", DiagnosticCode::value_wrong_kind, "inherit"},
        {"#a { effect: progress-pulse progress-laser }", DiagnosticCode::value_form_not_admitted, "progress-laser"},
        {"#a { opacity: inherit }", DiagnosticCode::value_wrong_kind, "inherit"},
        {"#a { opacity: unset }", DiagnosticCode::value_form_not_admitted, "unset"},
        {"#a { scale: revert-layer }", DiagnosticCode::value_form_not_admitted, "revert-layer"},
        {"#a { opacity: 1.5 }", DiagnosticCode::value_out_of_range, "1.5"},
        {"#a { opacity: red }", DiagnosticCode::value_wrong_kind, "red"},
        {"#a { opacity: 1 2 }", DiagnosticCode::value_syntax, "2 }"},
        {"#a { opacity: calc(var(--pointer-x) / 1px) }", DiagnosticCode::value_form_not_admitted, "calc"},
        {"#a { scale: 3 }", DiagnosticCode::value_out_of_range, "3"},
        {"#a { scale: 1 2 }", DiagnosticCode::value_form_not_admitted, "2 }"},
        {"#a { translate: 40px }", DiagnosticCode::value_out_of_range, "40px"},
        {"#a { translate: 10% }", DiagnosticCode::value_form_not_admitted, "10%"},
        {"#a { translate: 1px 2px 3px }", DiagnosticCode::value_form_not_admitted, "3px"},
        {"#a { translate: 2 }", DiagnosticCode::value_wrong_kind, "2 }"},
        {"#a { mix-blend-mode: overlay }", DiagnosticCode::value_form_not_admitted, "overlay"},
        {"#a { mix-blend-mode: sideways }", DiagnosticCode::keyword_unknown, "sideways"},
        {"#a { isolation: sideways }", DiagnosticCode::keyword_unknown, "sideways"},
        {"#a { primitive: 3 }", DiagnosticCode::value_wrong_kind, "3"},
        {"#a::role(label) { primitive: bulge }", DiagnosticCode::value_wrong_kind, "bulge"},
        {"#a { primitive: bulge; amplitude: 2 }", DiagnosticCode::value_out_of_range, "2 }"},
        {"#a { primitive: bulge; amplitude: 1 2 }", DiagnosticCode::value_syntax, "2 }"},
        {"#a { primitive: bulge; amplitude: 50% }", DiagnosticCode::value_wrong_kind, "50%"},
        {"#a { primitive: bulge; amplitude: red }", DiagnosticCode::value_wrong_kind, "red"},
        {"#a { primitive: bulge; light-colour: 3 }", DiagnosticCode::value_wrong_kind, "3"},
        {"#a { primitive: bulge; amplitude: calc(param(light-colour) * 1) }", DiagnosticCode::value_wrong_kind,
         "calc"},
        {"#a { primitive: material; shape: hexagon }", DiagnosticCode::keyword_unknown, "hexagon"},
        {"#a { primitive: material; shape: 3 }", DiagnosticCode::value_wrong_kind, "3"},
        {"#a { primitive: bulge; amplitude: var(--missing) }", DiagnosticCode::variable_undefined, "--missing"},
    }};
    for (const Refusal& entry : refusals) {
        const std::string what = std::string(diagnostic_code_name(entry.code)) + ": " + std::string(entry.sheet);
        const Compilation compiled = compile(entry.sheet);
        const bool one = compiled.outcome == CompileOutcome::refused && !compiled.diagnostics.empty() &&
                         compiled.diagnostics.back().code == entry.code;
        if (!one) print_diagnostics(compiled);
        require(one, what + ": refused with the code");
        if (!one) continue;
        const ThemeDiagnostic& diagnostic = compiled.diagnostics.back();
        const std::uint32_t column = column_of(entry.sheet, entry.marker);
        if (diagnostic.position.column.value != column) print_diagnostics(compiled);
        require(diagnostic.position.column.value == column, what + ": at the marker");
    }
    const Compilation named = compile("#a { primitive: bulge; amplitude: var(--missing) }");
    require(!named.diagnostics.empty() && named.diagnostics.back().rule == "#a" &&
                named.diagnostics.back().property == "amplitude",
            "a value's error names its rule and property");
}

// -- effect names against the registry (EP-3) -----------------------------------------

// Given a registry, a theme naming an effect it does not hold is refused at
// the name (E417), the diagnostic naming the rule and property; a name it
// holds, `none` and `initial` compile. Without one, only the name's form is
// checked.
void test_effect_registry() {
    Options checked{};
    checked.effects = &built_in_effect_registry();

    constexpr std::string_view unknown = "#a { opacity: 1; effect: wobble-band }";
    const Compilation refused = compile(unknown, checked);
    const bool one = refused.outcome == CompileOutcome::refused && refused.diagnostics.size() == 1U &&
                     refused.diagnostics.front().code == DiagnosticCode::effect_unknown;
    if (!one) print_diagnostics(refused);
    require(one, "an effect the registry does not hold refuses the theme with E417");
    if (one) {
        const ThemeDiagnostic& diagnostic = refused.diagnostics.front();
        require(diagnostic.position.line.value == 1U &&
                    diagnostic.position.column.value == column_of(unknown, "wobble-band"),
                "E417 is at the effect's name");
        require(diagnostic.rule == "#a" && diagnostic.property == "effect" && diagnostic.detail == "wobble-band",
                "E417 names the rule, the property and the name as written");
    }

    // The second of two rules, on its own line: the position is the name's.
    constexpr std::string_view second = "#a { effect: progress-pulse }\n#b { effect: progress-sparkle }";
    const Compilation placed = compile(second, checked);
    const bool at_second = placed.outcome == CompileOutcome::refused && placed.diagnostics.size() == 1U &&
                           placed.diagnostics.front().code == DiagnosticCode::effect_unknown &&
                           placed.diagnostics.front().position.line.value == 2U &&
                           placed.diagnostics.front().position.column.value == 14U &&
                           placed.diagnostics.front().rule == "#b";
    if (!at_second) print_diagnostics(placed);
    require(at_second, "E417 is at the unknown name's line and column, in its rule");

    const Compilation known = compile("#a { effect: progress-pulse }", checked);
    if (known.outcome != CompileOutcome::compiled) print_diagnostics(known);
    require(known.outcome == CompileOutcome::compiled && known.diagnostics.empty(),
            "an effect the registry holds compiles");
    const Compilation none = compile("#a { effect: none } #b { effect: initial }", checked);
    if (none.outcome != CompileOutcome::compiled) print_diagnostics(none);
    require(none.outcome == CompileOutcome::compiled, "none and initial need no registered effect");

    // The registry given is the one checked: an empty one holds nothing.
    const EffectRegistry empty{};
    Options bare{};
    bare.effects = &empty;
    const Compilation stock = compile("#a { effect: progress-pulse }", bare);
    require(stock.outcome == CompileOutcome::refused && !stock.diagnostics.empty() &&
                stock.diagnostics.back().code == DiagnosticCode::effect_unknown,
            "a stock effect is unknown to a registry that does not hold it");

    // A malformed name is still the form's refusal, not E417.
    const Compilation malformed = compile("#a { effect: wobble_band }", checked);
    require(malformed.outcome == CompileOutcome::refused && !malformed.diagnostics.empty() &&
                malformed.diagnostics.back().code == DiagnosticCode::keyword_unknown,
            "a name of the wrong form is keyword_unknown before the registry is read");

    const Compilation unchecked = compile(unknown);
    if (unchecked.outcome != CompileOutcome::compiled) print_diagnostics(unchecked);
    require(unchecked.outcome == CompileOutcome::compiled, "without a registry, a well-formed name compiles");
}

// -- warnings ----------------------------------------------------------------------

class CostlyPrimitive final : public EnginePrimitive {
public:
    explicit CostlyPrimitive(const PrimitiveDescriptor& described) : described_(described) {}
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override { return described_; }

private:
    void derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept override {}
    void fill(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<const double>,
              const PartRasterView&) const noexcept override {}
    PrimitiveDescriptor described_;
};

const PartRole face_role[] = {PartRole::face};

bool warned(const Compilation& compilation, DiagnosticCode code, std::string_view detail) {
    for (const ThemeDiagnostic& diagnostic : compilation.diagnostics) {
        if (diagnostic.code == code && diagnostic.detail == detail) return true;
    }
    return false;
}

void test_warnings() {
    const Compilation unknown = compile("#a { glimmer: 3; width: 4px; float: left; grid-template: none;"
                                        " aspect-ratio: 2; margin-top: 1px; opacity: 0.5 }");
    require(unknown.outcome == CompileOutcome::compiled && unknown.diagnostics.size() == 6U &&
                warned(unknown, DiagnosticCode::unknown_property, "glimmer") &&
                warned(unknown, DiagnosticCode::property_not_admitted, "width") &&
                warned(unknown, DiagnosticCode::property_not_admitted, "float") &&
                warned(unknown, DiagnosticCode::property_not_admitted, "grid-template") &&
                warned(unknown, DiagnosticCode::property_not_admitted, "aspect-ratio") &&
                warned(unknown, DiagnosticCode::property_not_admitted, "margin-top") &&
                unknown.sheet.rules.size() == 1U && holds_still(unknown.sheet.rules[0], 0U, opacity_property, 0.5),
            "unknown and left-out properties are dropped and the rest applies");
    // A part's metrics, shape and per-state font are admitted on a named
    // part only (look integration): on a control they are not.
    const Compilation part_fields =
        compile("#a::part(face) { height: 27px; gap: 0; margin-top: 3px; taper: 4px #496c9f }\n"
                "#a::part(face):hover { margin-bottom: -2px; flare: 3px #e8f2ff;"
                " edge-shadow: 5px rgba(24, 56, 97, 0.3) }\n"
                "#a::part(label):hover { font-weight: bold; font-size: 1.1em }");
    // (Its literal colours warn as unremappable, as any literal does.)
    bool part_fields_admitted = true;
    for (const ThemeDiagnostic& diagnostic : part_fields.diagnostics) {
        if (diagnostic.code != DiagnosticCode::literal_colour_unremappable) part_fields_admitted = false;
    }
    require(part_fields.outcome == CompileOutcome::compiled && part_fields_admitted &&
                part_fields.sheet.rules.size() == 3U,
            "part metrics, shapes and state fonts compile on a named part");
    const Compilation control_fields = compile("#a { height: 27px; taper: 4px }\n"
                                               "#a:hover { font-weight: bold }");
    require(control_fields.outcome == CompileOutcome::compiled && control_fields.diagnostics.size() == 3U &&
                warned(control_fields, DiagnosticCode::property_not_admitted, "height") &&
                warned(control_fields, DiagnosticCode::property_not_admitted, "taper") &&
                warned(control_fields, DiagnosticCode::property_not_admitted, "font-weight in a state rule"),
            "part metrics and state fonts are refused on a control");
    // A progress fill's stripe properties (the barber pole's knobs, owner
    // rulings of 2026-11-11) are part fields: admitted on a named part
    // (this vocabulary's button face stands in for a progress fill),
    // range-checked, and refused on a control.
    const Compilation stripes =
        compile("#a::part(face) { stripe-pitch: auto; stripe-ratio: 0.35; stripe-angle: 30deg;"
                " stripe-lean: backward; stripe-direction: forward; stripe-speed: 1.5; stripe-color: #ffffff;"
                " stripe-ground: #2060c0; stripe-gloss: none; stripe-reduced-motion: still }\n"
                "#b::part(face) { stripe-pitch: 12px; stripe-lean: forward; stripe-gloss: auto;"
                " stripe-reduced-motion: none }");
    bool stripes_admitted = true;
    for (const ThemeDiagnostic& diagnostic : stripes.diagnostics) {
        if (diagnostic.code == DiagnosticCode::literal_colour_unremappable) continue;
        stripes_admitted = false;
        std::cerr << "  stripes: " << diagnostic_code_label(diagnostic.code) << ' '
                  << diagnostic_code_name(diagnostic.code) << " (" << diagnostic.detail << ")\n";
    }
    require(stripes.outcome == CompileOutcome::compiled && stripes_admitted && stripes.sheet.rules.size() == 2U,
            "the stripe properties compile on a named part");
    // Each bad value is an error, which refuses its sheet at the first one
    // (the guide: the first error is named and the old theme stays), so
    // each is compiled on its own.
    struct BadStripe final {
        const char* sheet;
        DiagnosticCode code;
    };
    const std::array<BadStripe, 5> bad_stripes{{
        {"#a::part(face) { stripe-pitch: 2px }", DiagnosticCode::value_out_of_range},
        {"#a::part(face) { stripe-ratio: 0.95 }", DiagnosticCode::value_out_of_range},
        {"#a::part(face) { stripe-angle: 80deg }", DiagnosticCode::value_out_of_range},
        {"#a::part(face) { stripe-lean: sideways }", DiagnosticCode::keyword_unknown},
        {"#a::part(face) { stripe-speed: 4 }", DiagnosticCode::value_out_of_range},
    }};
    bool each_refused = true;
    for (const BadStripe& bad : bad_stripes) {
        const Compilation refused = compile(bad.sheet);
        const bool one = refused.outcome == CompileOutcome::refused && refused.diagnostics.size() == 1U &&
                         refused.diagnostics[0].code == bad.code;
        if (!one) {
            each_refused = false;
            std::cerr << "  bad stripe not refused as expected: " << bad.sheet << '\n';
        }
    }
    require(each_refused, "each stripe value outside its range or vocabulary refuses its sheet with one error");
    const Compilation control_stripes = compile("#a { stripe-speed: 1 }");
    require(control_stripes.outcome == CompileOutcome::compiled && control_stripes.diagnostics.size() == 1U &&
                warned(control_stripes, DiagnosticCode::property_not_admitted, "stripe-speed"),
            "the stripe properties are refused on a control");
    const Compilation not_read = compile("#a { width: var(--missing); glimmer: 1 2 3 }");
    require(not_read.outcome == CompileOutcome::compiled && not_read.diagnostics.size() == 2U,
            "a dropped property's value is not read");
    const Compilation no_primitive = compile("#a { amplitude: 0.5 }");
    require(no_primitive.outcome == CompileOutcome::compiled && no_primitive.diagnostics.size() == 1U &&
                warned(no_primitive, DiagnosticCode::unknown_property, "amplitude"),
            "a parameter with no primitive is an unknown property");
    const Compilation dropped = compile("#a { primitive: felt-cloth; nap: 3; opacity: 0.5 }");
    require(dropped.outcome == CompileOutcome::compiled && dropped.diagnostics.size() == 1U &&
                warned(dropped, DiagnosticCode::unknown_primitive, "felt-cloth") && dropped.sheet.rules.size() == 1U &&
                dropped.sheet.rules[0].declarations.empty(),
            "an unknown primitive is dropped with its parameters");
    const Compilation kept = compile("#a { primitive: bulge; primitive: felt-cloth; exponent: 3 }");
    require(kept.outcome == CompileOutcome::compiled && kept.diagnostics.size() == 1U &&
                kept.sheet.rules.size() == 1U && kept.sheet.rules[0].declarations.size() == 2U,
            "a dropped primitive leaves the earlier one selected");
    const Compilation function = compile("#a { primitive: bulge; amplitude: wobble(1); opacity: shimmer(2) }");
    require(function.outcome == CompileOutcome::compiled && function.diagnostics.size() == 2U &&
                warned(function, DiagnosticCode::unknown_function, "wobble") &&
                warned(function, DiagnosticCode::unknown_function, "shimmer"),
            "an unknown function as a value drops its declaration");

    const PrimitiveDescriptor heavy{"heavy", EngineVersion{0, 1}, face_role, {}, {}, PrimitiveCost{513U, 0U}, {}};
    const PrimitiveDescriptor unbounded{"unbounded", EngineVersion{0, 1}, face_role, {}, {}, PrimitiveCost{0U, 0U}, {}};
    const CostlyPrimitive heavy_primitive{heavy};
    const CostlyPrimitive unbounded_primitive{unbounded};
    PrimitiveRegistry registry{};
    require(registry.register_primitive(heavy_primitive) == RegistrationOutcome::registered &&
                registry.register_primitive(unbounded_primitive) == RegistrationOutcome::registered,
            "the costly primitives register");
    Options costly{};
    costly.registry = &registry;
    const Compilation budgets = compile("#a { primitive: heavy } #b { primitive: unbounded }", costly);
    require(budgets.outcome == CompileOutcome::compiled && budgets.diagnostics.size() == 2U &&
                warned(budgets, DiagnosticCode::primitive_over_budget, "513") &&
                warned(budgets, DiagnosticCode::primitive_cost_unbounded, "unbounded") && budgets.sheet.rules.empty(),
            "a primitive over the budgets is dropped with a warning");

    Options newer{};
    newer.requires_engine = EngineVersion{current_engine_version.major, static_cast<std::uint16_t>(current_engine_version.minor + 1U)};
    const Compilation ahead = compile("#a { primitive: material; shape: hexagon; recipe: card; mix-blend-mode: glow }", newer);
    const std::string version =
        std::to_string(newer.requires_engine.major) + "." + std::to_string(newer.requires_engine.minor);
    print_diagnostics(ahead);
    require(ahead.outcome == CompileOutcome::compiled && ahead.diagnostics.size() == 3U &&
                ahead.diagnostics[0].code == DiagnosticCode::needs_newer_engine && ahead.diagnostics[0].detail == version &&
                warned(ahead, DiagnosticCode::keyword_from_newer_engine, "hexagon") &&
                warned(ahead, DiagnosticCode::keyword_from_newer_engine, "glow") &&
                ahead.sheet.requires_engine == current_engine_version && ahead.sheet.rules.size() == 1U &&
                ahead.sheet.rules[0].declarations.size() == 2U,
            "a newer engine's keywords are dropped with a warning, and the rest applies on this engine");
    Options older{};
    older.requires_engine = EngineVersion{0, 1};
    const Compilation behind = compile("#a { opacity: 1 }", older);
    require(behind.outcome == CompileOutcome::compiled && behind.diagnostics.empty() &&
                behind.sheet.requires_engine == older.requires_engine && behind.sheet.published_values == std::vector<std::string>{"progress"},
            "an older engine version is kept, and the published values are the sheet's");
}

// -- the lowered size ----------------------------------------------------------------

// `count` (at most 32) classes in an :is(): `.p0, .p1, ...`.
std::string is_list(std::string_view prefix, std::size_t count) {
    std::string list = ":is(";
    for (std::size_t index = 0U; index < count; ++index) {
        if (index != 0U) list += ", ";
        list += "." + std::string(prefix) + std::to_string(index);
    }
    list += ")";
    return list;
}

// A rule of `expansions` / 32 by 32 selectors declaring all eight of the
// bulge's number parameters, each an expression of 59 instructions.
std::string heavy_sheet(std::size_t third) {
    std::string term = "var(--pointer-x) / 1px";
    std::string sum = term;
    for (int index = 1; index < 15; ++index) sum += " + " + term;
    const std::array<std::string_view, 8> parameters{
        "amplitude", "exponent", "light-peak", "shade-peak", "crown-peak", "gold-light-peak", "gold-shade-peak",
        "gold-crown-peak"};
    std::string sheet = "#p" + is_list("a", 32) + is_list("b", 32) + is_list("c", third) + " { primitive: bulge;";
    for (const std::string_view parameter : parameters) sheet += " " + std::string(parameter) + ": calc(" + sum + ");";
    sheet += " }";
    return sheet;
}

void test_lowered_size() {
    const Compilation within = compile(heavy_sheet(1U));
    require(within.outcome == CompileOutcome::compiled && within.sheet.rules.size() == 1024U &&
                within.sheet.rules[0].declarations.size() == 9U &&
                within.sheet.rules[0].declarations[1].program.size() == 59U,
            "1,024 copies of a rule of eight long expressions compile");
    const Compilation past = compile(heavy_sheet(2U));
    require(past.outcome == CompileOutcome::refused && past.diagnostics.size() == 1U &&
                past.diagnostics[0].code == DiagnosticCode::lowered_sheet_too_large &&
                past.diagnostics[0].detail == std::to_string(maximum_lowered_bytes) &&
                past.diagnostics[0].position.column.value == 1U,
            "2,048 copies pass the lowered size and are refused");
}

// The codes P5, P10 and P12 add, by number.
struct CodeLabel final {
    DiagnosticCode code{};
    std::string_view label{};
};

void test_codes() {
    const std::array<CodeLabel, 20> codes{{
        {DiagnosticCode::variable_cannot_vary, "E416"},
        {DiagnosticCode::effect_unknown, "E417"},
        {DiagnosticCode::too_many_variable_combinations, "E508"},
        {DiagnosticCode::too_many_scoped_variables, "E515"},
        {DiagnosticCode::manifest_invalid, "E601"},
        {DiagnosticCode::file_hash_mismatch, "E602"},
        {DiagnosticCode::import_not_installed, "E603"},
        {DiagnosticCode::import_hash_mismatch, "E604"},
        {DiagnosticCode::import_cycle, "E605"},
        {DiagnosticCode::published_value_invalid, "E606"},
        {DiagnosticCode::engine_too_old, "E607"},
        {DiagnosticCode::lowered_sheet_too_large, "E513"},
        {DiagnosticCode::too_many_keyframes, "E514"},
        {DiagnosticCode::unknown_primitive, "W102"},
        {DiagnosticCode::unknown_function, "W103"},
        {DiagnosticCode::primitive_over_budget, "W108"},
        {DiagnosticCode::primitive_cost_unbounded, "W109"},
        {DiagnosticCode::needs_newer_engine, "W301"},
        {DiagnosticCode::keyword_from_newer_engine, "W302"},
        {DiagnosticCode::inline_with_theme, "W401"},
    }};
    for (const CodeLabel& entry : codes) {
        require(diagnostic_code_label(entry.code) == entry.label, entry.label);
        require(diagnostic_code_name(entry.code) != "unknown", entry.label);
    }
}

} // namespace

int main() {
    test_codes();
    test_gate();
    test_properties();
    test_refusals();
    test_effect_registry();
    test_warnings();
    test_lowered_size();
    if (failures != 0) {
        std::cerr << failures << " theme compiler check(s) failed\n";
        return 1;
    }
    std::cout << "theme compiler tests passed\n";
    return 0;
}
