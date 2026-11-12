// CSS's transitions (stage T3, phase P7a): `transition` and its four
// longhands lower onto the curves of the engine's presentational state
// machines, and each transition is grouped with the rules that refine its
// own (R2-18), their values becoming the machine's states. Gate: the pop
// (engine_pop_tests) written as a sheet gives the records the C++ pop
// writes, and on a hand-driven clock the same frames with the same bakes;
// each timing function lowers to its engine curve; and the forms the
// engine cannot run are refused.

#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_material/presets/material_presets.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The spring's expected overshoot is computed as the lowering computes it.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

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

// A machine resting at `target` in every state, as a static value lowers.
PropertyTransition still(std::string_view property, double target, const MotionCurve& curve) {
    PropertyTransition machine{};
    machine.property = std::string(property);
    PresentationState state{};
    state.target = target;
    machine.states.push_back(state);
    machine.curve = curve;
    return machine;
}

PresentationState state_when(AnatomyState when, double target) {
    PresentationState state{};
    state.when.push_back(when);
    state.target = target;
    return state;
}

// -- the pop ------------------------------------------------------------------------

// The popped scale of engine_pop_tests: 1.08, within the grant.
constexpr double popped_scale = 1.08;

// engine_pop_tests' pop_rule(), written in C++.
ThemeRule written_pop_rule() {
    ThemeRule rule{};
    rule.label = "pop";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    PropertyTransition scale{};
    scale.property = std::string(scale_property);
    scale.states.push_back(state_when(AnatomyState::hot, popped_scale));
    scale.states.push_back(state_when(AnatomyState::selected, popped_scale));
    scale.states.push_back(state_when(AnatomyState::focus_visible, popped_scale));
    PresentationState rest{};
    rest.target = 1.0;
    scale.states.push_back(rest);
    scale.curve = MotionCurve::timed(milliseconds(120), MotionEasing::linear);
    rule.transitions.push_back(scale);
    PropertyTransition fade{};
    fade.property = std::string(face_property);
    fade.curve = MotionCurve::timed(milliseconds(120), MotionEasing::linear);
    rule.transitions.push_back(fade);
    return rule;
}

// The same pop as a sheet. The refinements are written least precedent
// first, as CSS orders rules of one specificity; the machine takes the
// most precedent state first.
constexpr std::string_view pop_sheet =
    "::part(face) {\n"
    "  primitive: material;\n"
    "  recipe: button;\n"
    "  scale: 1;\n"
    "  transition: scale 120ms linear, face 120ms linear;\n"
    "}\n"
    "::part(face):focus-visible { scale: 1.08 }\n"
    "::part(face):state(selected) { scale: 1.08 }\n"
    "::part(face):hover { scale: 1.08 }\n";

void test_pop_records() {
    const Compilation compiled = compile(pop_sheet);
    const ThemeRule& rule = only_rule(compiled, pop_sheet);
    const ThemeRule written = written_pop_rule();
    require(rule.level == written.level && rule.selector == written.selector,
            "the pop's rule selects every face at the theme's level");
    require(rule.declarations == written.declarations, "the pop's face is the material primitive's");
    require(rule.transitions == written.transitions,
            "the pop's machines are the C++ pop's: the scale with its three raised states, then the face fade");
}

// One part raster composited: where, and whether it was stretched.
struct RasterDraw final {
    std::shared_ptr<const PartRaster> raster{};
    Rect destination{};
    bool stretched{};
};

// A painter that keeps the part rasters composited, in window units, and
// draws nothing.
class RasterLog final : public Painter {
public:
    void save() override { saved.push_back(offset); }
    void restore() override {
        if (saved.empty()) return;
        offset = saved.back();
        saved.pop_back();
    }
    void translate(Point by) override { offset = Point{offset.x + by.x, offset.y + by.y}; }
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        if (!raster) return;
        const double scale = (*raster).scale().value;
        const Rect placed{origin.x + offset.x, origin.y + offset.y,
                          static_cast<double>((*raster).extent().width) / scale,
                          static_cast<double>((*raster).extent().height) / scale};
        draws.push_back(RasterDraw{raster, placed, false});
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double) override {
        if (!raster) return;
        const Rect placed{destination.x + offset.x, destination.y + offset.y, destination.width,
                          destination.height};
        draws.push_back(RasterDraw{raster, placed, true});
    }
    std::vector<RasterDraw> draws{};
    Point offset{};
    std::vector<Point> saved{};
};

// The motion clock's time, advanced by hand.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// engine_pop_tests' tile: a 60 × 24 button face, hot under the pointer,
// granted 4 units of overflow on each side.
class Tile final : public Control {
public:
    explicit Tile(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_pointer(PointerEvent& event) override {
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
        part.bounds = Rect{20.0, 28.0, 60.0, 24.0};
        part.key = PartKey{"face", {}, {}};
        part.granted_overflow = Insets{4.0, 4.0, 4.0, 4.0};
        if (hot) part.states.insert(AnatomyState::hot);
        paint_part(painter, *this, part);
    }
    bool hot{};
};

// engine_pop_tests' faces: a gloss at rest, a brighter gloss with a deeper
// shadow and a glow when hot or selected.
SurfaceMaterial resting_face() {
    SurfaceMaterial material = gloss_material(GlossColours{}, 4.0);
    material.shadows.push_back(MaterialShadow{Point{0.0, 1.0}, 1.0, 0.0, Color{20, 30, 42, 60}, false});
    return material;
}

SurfaceMaterial raised_face() {
    GlossColours colours{};
    colours.top = Color{246, 250, 255, 255};
    colours.above_break = Color{216, 232, 250, 255};
    colours.below_break = Color{172, 204, 240, 255};
    colours.bottom = Color{204, 226, 250, 255};
    SurfaceMaterial material = gloss_material(colours, 4.0);
    material.shadows.push_back(MaterialShadow{Point{0.0, 3.0}, 3.0, 0.0, Color{20, 30, 42, 96}, false});
    MaterialGlow glow{};
    glow.color = Color{255, 255, 255, 120};
    glow.blur_radius = 1.5;
    glow.spread = 0.5;
    material.glows.push_back(glow);
    return material;
}

struct PopWindow final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::unique_ptr<Window> window;
    RasterLog painter{};
};

// Three tiles side by side, drawn with the professional theme's button
// recipes given the pop's faces and `rules` added to its sheet.
std::unique_ptr<PopWindow> make_pop_window(const ThemeRuleSheet& rules) {
    std::unique_ptr<PopWindow> made_window = std::make_unique<PopWindow>();
    PopWindow& made = *made_window;
    made.root = make_control<Panel>(StableId("root"));
    const std::array<const char*, 3> names{{"left", "middle", "right"}};
    for (std::size_t index = 0U; index < names.size(); ++index) {
        std::shared_ptr<Tile> tile = make_control<Tile>(StableId(std::string(names[index])));
        (*tile).set_requested_bounds(Rect{static_cast<double>(index) * 100.0, 0.0, 100.0, 80.0});
        (*made.root).add_child(tile);
    }
    made.window = std::make_unique<Window>(made.root, Size{300.0, 80.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    const std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "theme-motion-css-pop";
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    const std::size_t normal = static_cast<std::size_t>(ControlSurfaceState::normal);
    const std::size_t hot = static_cast<std::size_t>(ControlSurfaceState::hot);
    button.ordinary[normal].material = resting_face();
    button.ordinary[hot].material = raised_face();
    button.selected[normal].material = raised_face();
    button.selected[hot].material = raised_face();
    for (const ThemeRule& rule : rules.rules) definition.rules.rules.push_back(rule);
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the pop theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return made_window;
}

void repaint(PopWindow& made) {
    Window& window = *made.window;
    made.painter.draws.clear();
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(made.painter, damage.bounds());
}

std::uint64_t bakes(const Window& window) {
    const std::uint64_t count = window.engine_parts().cache_statistics().bakes;
    return count;
}

bool same_raster(const PartRaster& left, const PartRaster& right) {
    if (!(left.extent() == right.extent())) return false;
    const bool same = std::equal(left.pixels().begin(), left.pixels().end(), right.pixels().begin(),
                                 right.pixels().end());
    return same;
}

// Whether the two windows composited the same rasters, pixel for pixel, at
// the same places.
bool same_frame(const RasterLog& left, const RasterLog& right) {
    if (left.draws.size() != right.draws.size()) return false;
    for (std::size_t index = 0U; index < left.draws.size(); ++index) {
        const RasterDraw& one = left.draws[index];
        const RasterDraw& other = right.draws[index];
        if (!(one.destination == other.destination) || one.stretched != other.stretched) return false;
        if (!same_raster(*one.raster, *other.raster)) return false;
    }
    return true;
}

// Moves the pointer to `at` in both windows, then runs both clocks in
// 16 ms frames until they rest, comparing every frame. Returns the frames
// that moved.
std::size_t run_both(PopWindow& written, PopWindow& sheet, Point at) {
    const PointerEvent move{PointerAction::move, PointerButton::none, at};
    static_cast<void>((*written.window).dispatch_pointer(move));
    static_cast<void>((*sheet.window).dispatch_pointer(move));
    repaint(written);
    repaint(sheet);
    require(same_frame(written.painter, sheet.painter), "the change draws the same frame");
    const std::uint64_t changed = bakes(*sheet.window);
    std::size_t moving = 0U;
    for (int step = 0; step < 40 && (*written.window).motion_clock().armed(); ++step) {
        written.time.time += milliseconds(16);
        sheet.time.time += milliseconds(16);
        static_cast<void>((*written.window).poll_frame_schedule(written.time.time));
        static_cast<void>((*sheet.window).poll_frame_schedule(sheet.time.time));
        repaint(written);
        repaint(sheet);
        require((*sheet.window).motion_clock().armed() == (*written.window).motion_clock().armed(),
                "both clocks run and stop together");
        require(same_frame(written.painter, sheet.painter), "each frame is the same frame");
        require(bakes(*sheet.window) == bakes(*written.window), "with the same bakes");
        if ((*sheet.window).motion_clock().armed()) {
            require(bakes(*sheet.window) == changed, "nothing is baked while the transition runs");
        }
        moving = moving + 1U;
    }
    require(!(*sheet.window).motion_clock().armed(), "the sheet's pop comes to rest");
    return moving;
}

void test_pop_frames() {
    ThemeRuleSheet written{};
    written.rules.push_back(written_pop_rule());
    const Compilation compiled = compile(pop_sheet);
    std::unique_ptr<PopWindow> from_cpp = make_pop_window(written);
    std::unique_ptr<PopWindow> from_sheet = make_pop_window(compiled.sheet);
    require(same_frame((*from_cpp).painter, (*from_sheet).painter), "the resting frame is the same");
    const std::size_t rising = run_both(*from_cpp, *from_sheet, Point{150.0, 40.0});
    require(rising > 2U, "hovering the middle tile pops it over several frames");
    const std::size_t falling = run_both(*from_cpp, *from_sheet, Point{250.0, 40.0});
    require(falling > 2U, "moving to the right tile settles the middle one and pops the right one");
}

// -- the curves ---------------------------------------------------------------------

struct CurveRow final {
    std::string_view transition{};
    MotionCurve curve{};
};

MotionCurve spring_with_damping(FrameInterval response, double damping) {
    const double pi = 3.14159265358979323846;
    const double overshoot = std::exp(-pi * damping / std::sqrt(1.0 - damping * damping));
    const MotionCurve curve = MotionCurve::spring(response, overshoot);
    return curve;
}

std::vector<CurveRow> curve_rows() {
    const FrameInterval moving = milliseconds(120);
    std::vector<CurveRow> rows{
        {"opacity 120ms linear", MotionCurve::timed(moving, MotionEasing::linear)},
        {"opacity 120ms", MotionCurve::timed(moving, MotionEasing::ease)},
        {"opacity 0.12s ease", MotionCurve::timed(moving, MotionEasing::ease)},
        {"opacity 120ms ease-in-out", MotionCurve::timed(moving, MotionEasing::smoothstep)},
        {"opacity 120ms ease-in", MotionCurve::cubic_bezier(moving, 0.42, 0.0, 1.0, 1.0)},
        {"opacity 120ms ease-out", MotionCurve::cubic_bezier(moving, 0.0, 0.0, 0.58, 1.0)},
        {"opacity 120ms step-start", MotionCurve::stepped(moving, 1U, MotionStepPosition::start)},
        {"opacity 120ms step-end", MotionCurve::stepped(moving, 1U, MotionStepPosition::end)},
        {"opacity 120ms cubic-bezier(0.2, 0, 0.4, 1.05)", MotionCurve::cubic_bezier(moving, 0.2, 0.0, 0.4, 1.05)},
        {"opacity 120ms steps(4)", MotionCurve::stepped(moving, 4U, MotionStepPosition::end)},
        {"opacity 120ms steps(4, jump-start)", MotionCurve::stepped(moving, 4U, MotionStepPosition::start)},
        {"opacity 120ms steps(3, start)", MotionCurve::stepped(moving, 3U, MotionStepPosition::start)},
        {"opacity 120ms steps(32, end)", MotionCurve::stepped(moving, 32U, MotionStepPosition::end)},
        {"opacity follow(50ms)", MotionCurve::follow(milliseconds(50))},
        {"opacity decay(80ms)", MotionCurve::decay(milliseconds(80))},
        {"opacity spring(90ms, 1)", MotionCurve::spring(milliseconds(90), 0.0)},
        {"opacity spring(90ms, 0.7)", spring_with_damping(milliseconds(90), 0.7)},
        {"opacity linear 120ms 30ms", MotionCurve::timed(moving, MotionEasing::linear).delayed(milliseconds(30))},
        {"opacity follow(50ms) 0s 20ms", MotionCurve::follow(milliseconds(50)).delayed(milliseconds(20))},
        // Every curve is clamped to the engine's 400 ms ceiling.
        {"opacity 1s linear", MotionCurve::timed(milliseconds(400), MotionEasing::linear)},
        {"opacity 120ms linear 1s", MotionCurve::timed(moving, MotionEasing::linear).delayed(milliseconds(400))},
        // A later transition of the property replaces an earlier one.
        {"opacity 50ms, opacity 120ms linear", MotionCurve::timed(moving, MotionEasing::linear)},
        // No time: the value jumps.
        {"opacity", MotionCurve::timed(FrameInterval{}, MotionEasing::ease)},
    };
    return rows;
}

void test_curves() {
    for (const CurveRow& row : curve_rows()) {
        const std::string sheet = "#pad::part(face) { opacity: 0.5; transition: " + std::string(row.transition) + " }";
        const Compilation compiled = compile(sheet);
        const ThemeRule& rule = only_rule(compiled, sheet);
        const PropertyTransition* const machine = machine_of(rule, opacity_property);
        require(machine != nullptr && rule.transitions.size() == 1U, "one machine: " + sheet);
        if (machine == nullptr) continue;
        require((*machine).curve == row.curve, "the curve: " + sheet);
        require((*machine).states.size() == 1U && (*machine).states[0].target == 0.5,
                "the static value is the machine's resting state: " + sheet);
    }
}

void test_longhands() {
    const std::string_view longhands =
        "#pad::part(face) { opacity: 0.5; scale: 0.9;"
        " transition-property: opacity, scale;"
        " transition-duration: 100ms, 200ms;"
        " transition-timing-function: linear;"
        " transition-delay: 10ms }";
    const Compilation compiled = compile(longhands);
    const ThemeRule& rule = only_rule(compiled, longhands);
    const PropertyTransition* const opacity = machine_of(rule, opacity_property);
    const PropertyTransition* const scale = machine_of(rule, scale_property);
    require(opacity != nullptr && scale != nullptr, "both machines are kept");
    if (opacity == nullptr || scale == nullptr) return;
    require((*opacity).curve ==
                MotionCurve::timed(milliseconds(100), MotionEasing::linear).delayed(milliseconds(10)),
            "the longhands' lists give the first property their first entries");
    require((*scale).curve == MotionCurve::timed(milliseconds(200), MotionEasing::linear).delayed(milliseconds(10)),
            "and a short list repeats, as CSS's does");

    const std::string_view reset = "#pad::part(face) { opacity: 0.5; transition: opacity 100ms; transition: none }";
    const Compilation none_compiled = compile(reset);
    const ThemeRule& none = only_rule(none_compiled, reset);
    require(none.transitions.size() == 1U &&
                none.transitions[0] == still(opacity_property, 0.5, MotionCurve::timed(FrameInterval{},
                                                                                       MotionEasing::linear)),
            "`none` declares no transition: the static value jumps");
    const std::string_view initial =
        "#pad::part(face) { opacity: 0.5; transition: opacity 100ms; transition: initial }";
    const Compilation initialised_compiled = compile(initial);
    const ThemeRule& initialised = only_rule(initialised_compiled, initial);
    require(initialised.transitions.size() == 1U && initialised.transitions[0].curve.duration == FrameInterval{},
            "`initial` declares no transition either");
    const std::string_view longhand_later =
        "#pad::part(face) { opacity: 0.5; transition: opacity 100ms linear; transition-duration: 200ms }";
    const Compilation later_compiled = compile(longhand_later);
    const ThemeRule& later = only_rule(later_compiled, longhand_later);
    require(later.transitions.size() == 1U &&
                later.transitions[0].curve == MotionCurve::timed(milliseconds(200), MotionEasing::linear),
            "a later longhand replaces the shorthand's list");
}

void test_translate_and_idle() {
    const std::string_view translate =
        "#pad::part(face) { translate: 2px -1px; transition: translate 100ms linear }";
    const Compilation rule_compiled = compile(translate);
    const ThemeRule& rule = only_rule(rule_compiled, translate);
    const MotionCurve curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    const PropertyTransition* const x = machine_of(rule, translate_x_property);
    const PropertyTransition* const y = machine_of(rule, translate_y_property);
    require(rule.transitions.size() == 2U && x != nullptr && y != nullptr, "translate moves both translations");
    if (x == nullptr || y == nullptr) return;
    require(*x == still(translate_x_property, 2.0, curve) && *y == still(translate_y_property, -1.0, curve),
            "each translation takes the transition's curve");

    const std::string_view idle = "#pad::part(face) { transition: opacity 100ms }";
    const Compilation nothing = compile(idle);
    require(nothing.outcome == CompileOutcome::compiled && nothing.sheet.rules.empty() && nothing.diagnostics.empty(),
            "a transition with no value to move moves nothing, and its rule declares nothing");
    const std::string_view face = "#pad::part(face) { transition: face 100ms linear }";
    const Compilation fade_compiled = compile(face);
    const ThemeRule& fade = only_rule(fade_compiled, face);
    require(fade.transitions.size() == 1U && fade.transitions[0].property == face_property &&
                fade.transitions[0].states.empty(),
            "a face cross-fade needs no value");
}

// -- the grouping -------------------------------------------------------------------

void test_grouping() {
    // Two refinements of different specificity, the more specific first in
    // the machine whatever the order written; a refinement's other values
    // stay in its rule.
    const std::string_view sheet =
        "#pad::part(face) { scale: 1; transition: scale 100ms linear }\n"
        "#pad::part(face):hover:focus { scale: 1.06 }\n"
        "#pad::part(face):hover { scale: 1.03; opacity: 0.5 }\n"
        "#pad::part(face):hover:focus-visible { scale: 1.06 }\n";
    const Compilation compiled = compile(sheet);
    require(compiled.outcome == CompileOutcome::compiled && compiled.sheet.rules.size() == 2U,
            "the grouped refinements leave the host and the refinement with another value");
    if (compiled.sheet.rules.size() != 2U) return;
    const ThemeRule& host = compiled.sheet.rules[0];
    const PropertyTransition* const scale = machine_of(host, scale_property);
    require(scale != nullptr && (*scale).states.size() == 4U, "the host's machine has the three states and its own");
    if (scale == nullptr || (*scale).states.size() != 4U) return;
    const std::vector<PresentationState>& states = (*scale).states;
    require(states[0].when.size() == 2U && states[1].when.size() == 2U && states[2].when.size() == 1U &&
                states[3].when.empty(),
            "the more specific states come first, the host's own last");
    require(std::find(states[0].when.begin(), states[0].when.end(), AnatomyState::focus_visible) !=
                states[0].when.end(),
            "of equal specificity, the later rule first");
    require(states[2].target == 1.03 && states[3].target == 1.0, "each state keeps its rule's value");
    const ThemeRule& kept = compiled.sheet.rules[1];
    require(kept.transitions.size() == 1U && kept.transitions[0].property == opacity_property,
            "the hover rule keeps its opacity and loses its scale");

    // Refinements of another selector, or of another level, are not grouped.
    const std::string_view other =
        "#pad::part(face) { scale: 1; transition: scale 100ms linear }\n"
        "#other::part(face):hover { scale: 1.03 }\n"
        "::part(face):hover { scale: 1.03 }\n";
    const Compilation separate = compile(other);
    require(separate.outcome == CompileOutcome::compiled && separate.sheet.rules.size() == 3U,
            "only rules refining the transition's own are grouped");
}

void test_parameter() {
    const std::string_view sheet =
        "#pad::part(face) { primitive: bulge; amplitude: 0; transition: amplitude 100ms ease-in-out }\n"
        "#pad::part(face):hover { primitive: bulge; amplitude: 1 }\n"
        "#pad::part(face):active { primitive: bulge; amplitude: 0.5; exponent: 4 }\n"
        "#pad::part(face):focus { primitive: material; recipe: button }\n";
    const Compilation compiled = compile(sheet);
    require(compiled.outcome == CompileOutcome::compiled, "the parameter sheet compiles");
    if (compiled.sheet.rules.size() != 4U) {
        require(false, "each rule keeps its primitive");
        return;
    }
    const ThemeRule& host = compiled.sheet.rules[0];
    const PropertyTransition* const amplitude = machine_of(host, "amplitude");
    require(amplitude != nullptr, "the amplitude has a machine");
    if (amplitude == nullptr) return;
    PropertyTransition expected{};
    expected.property = "amplitude";
    expected.states.push_back(state_when(AnatomyState::pressed, 0.5));
    expected.states.push_back(state_when(AnatomyState::hot, 1.0));
    expected.curve = MotionCurve::timed(milliseconds(100), MotionEasing::smoothstep);
    require(*amplitude == expected,
            "the refinements' amplitudes are the machine's states, the later first; the rest is the cascade's 0");
    const ThemeRule& hover = compiled.sheet.rules[1];
    bool hover_amplitude = false;
    for (const RuleDeclaration& declaration : hover.declarations) {
        if (declaration.property == "amplitude") hover_amplitude = true;
    }
    require(!hover_amplitude, "the hover's amplitude declaration is absorbed");
    const ThemeRule& active = compiled.sheet.rules[2];
    require(active.declarations.size() == 2U && active.declarations[1].property == "exponent",
            "the active rule keeps its primitive and its exponent");
}

// -- the refusals -------------------------------------------------------------------

struct Refusal final {
    std::string_view sheet{};
    DiagnosticCode code{};
};

void test_refusals() {
    const std::array<Refusal, 22> refusals{{
        {"#a { opacity: 1; transition: all 100ms }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition-duration: 100ms }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition: opacity 100ms follow(50ms) }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition: opacity 100ms steps(2, jump-none) }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition: opacity 100ms steps(2, jump-both) }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition: opacity 100ms linear -10ms }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition: opacity -100ms }", DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity 100ms cubic-bezier(1.2, 0, 1, 1) }",
         DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity 100ms steps(0) }", DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity 100ms steps(33) }", DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity 100ms steps(2.5) }", DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity spring(90ms, 0.5) }", DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity spring(90ms, 1.2) }", DiagnosticCode::value_out_of_range},
        {"#a { opacity: 1; transition: opacity spring(90ms, 0) }", DiagnosticCode::value_out_of_range},
        {"#a { primitive: bulge; exponent: 3; transition: exponent 100ms }", DiagnosticCode::value_wrong_kind},
        {"#a { primitive: bulge; transition: sideways 100ms }", DiagnosticCode::value_form_not_admitted},
        {"#a { opacity: 1; transition: opacity 100ms 10ms 10ms }", DiagnosticCode::value_syntax},
        {"#a { opacity: 1; transition: opacity 100ms linear ease }", DiagnosticCode::value_syntax},
        {"#a { opacity: 1; transition: opacity, none }", DiagnosticCode::value_syntax},
        {"#a { opacity: 1; transition: opacity 100ms steps(2, sideways) }", DiagnosticCode::keyword_unknown},
        {"#a { opacity: 1; transition: opacity 3px }", DiagnosticCode::value_wrong_kind},
        {"#a { opacity: 1; transition: inherit }", DiagnosticCode::value_wrong_kind},
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

    // Nine states of one transition: eight refinements and its own.
    const std::string_view crowded =
        "#a::part(face) { scale: 1; transition: scale 100ms }\n"
        "#a::part(face):hover { scale: 1.01 }\n"
        "#a::part(face):active { scale: 1.02 }\n"
        "#a::part(face):focus { scale: 1.03 }\n"
        "#a::part(face):focus-visible { scale: 1.04 }\n"
        "#a::part(face):checked { scale: 1.05 }\n"
        "#a::part(face):disabled { scale: 1.06 }\n"
        "#a::part(face):state(selected) { scale: 1.07 }\n"
        "#a::part(face):default { scale: 1.08 }\n";
    const Compilation too_many = compile(crowded);
    require(too_many.outcome == CompileOutcome::refused && !too_many.diagnostics.empty() &&
                too_many.diagnostics.back().code == DiagnosticCode::too_many_layers,
            "more states than a machine holds are refused");

    const std::string_view unknown = "#a { opacity: 0.5; transition: opacity 100ms bounce(2) }";
    const Compilation dropped = compile(unknown);
    require(dropped.outcome == CompileOutcome::compiled && dropped.diagnostics.size() == 1U &&
                dropped.diagnostics[0].code == DiagnosticCode::unknown_function,
            "an unknown function drops the transition with a warning");
    if (dropped.sheet.rules.size() == 1U) {
        require(dropped.sheet.rules[0].transitions.size() == 1U &&
                    dropped.sheet.rules[0].transitions[0].curve.duration == FrameInterval{},
                "and the rule keeps its static value");
    } else {
        require(false, "the rule is kept");
    }
}

} // namespace

int main() {
    test_pop_records();
    test_pop_frames();
    test_curves();
    test_longhands();
    test_translate_and_idle();
    test_grouping();
    test_parameter();
    test_refusals();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
