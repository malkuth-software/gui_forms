// Motion curves, delays, keyframe stops and the presentational translation
// (E9.6 G4; CSS_COVERAGE rows 13 and 14): a timed curve takes CSS's
// cubic-bezier and steps easings, with its control points and levels
// clamped; a delay holds the value at its origin before the move; a move
// through stops passes each stop's value at its offset; the samples have
// the same bits on every host. In theme rules, translate_x and translate_y
// are engine properties checked at load, as are a state's stops; a part
// translates only within the overflow its control grants beyond what its
// scale takes, its hit region and layout unchanged; and stops play on
// entering the state. GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed
// hash instead of checking it.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_shape/part_shape.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/routed_input_probe.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_motion_curve_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

bool near(double value, double expected, double tolerance) {
    const bool close = std::fabs(value - expected) <= tolerance;
    return close;
}

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// -- the curves ------------------------------------------------------------------

void cubic_bezier_easing() {
    // CSS `ease`, whose value at half its time is 0.8024033877.
    const MotionCurve ease = MotionCurve::cubic_bezier(milliseconds(200), 0.25, 0.1, 0.25, 1.0);
    require(ease.kind == MotionKind::timed && ease.easing == MotionEasing::cubic_bezier,
            "a cubic Bézier is a timed curve");
    require(motion_curve_easing(ease, 0.0) == 0.0 && motion_curve_easing(ease, 1.0) == 1.0,
            "it runs from exactly 0 to exactly 1");
    require(near(motion_curve_easing(ease, 0.5), 0.8024033877, 1e-9), "CSS ease has its stated shape");
    const MotionCurve straight = MotionCurve::cubic_bezier(milliseconds(200), 0.0, 0.0, 1.0, 1.0);
    const MotionCurve in_out = MotionCurve::cubic_bezier(milliseconds(200), 0.42, 0.0, 0.58, 1.0);
    double previous = 0.0;
    for (int step = 0; step <= 100; ++step) {
        const double progress = static_cast<double>(step) / 100.0;
        require(near(motion_curve_easing(straight, progress), progress, 1e-11),
                "the straight Bézier is linear within the bisection's precision");
        const double eased = motion_curve_easing(in_out, progress);
        require(eased >= previous, "ease-in-out never runs backward");
        previous = eased;
    }
    require(near(motion_curve_easing(in_out, 0.5), 0.5, 1e-11), "ease-in-out is symmetric about its middle");

    const MotionCurve wild = MotionCurve::cubic_bezier(milliseconds(200), -1.0, -0.5, 2.0, 1.5);
    require(wild.bezier[0] == 0.0 && wild.bezier[1] == -maximum_motion_overshoot && wild.bezier[2] == 1.0 &&
                wild.bezier[3] == 1.0 + maximum_motion_overshoot,
            "x is clamped to [0, 1] and y to the overshoot bound");
    const MotionCurve broken = MotionCurve::cubic_bezier(milliseconds(200), std::nan(""), std::numeric_limits<double>::infinity(), 0.5,
                                                               0.5);
    require(broken.bezier[0] == 0.0 && broken.bezier[1] == 0.0 && broken.bezier[2] == 0.5 && broken.bezier[3] == 0.5,
            "a coordinate that is not finite takes the straight curve's");
}

void steps_easing() {
    const MotionCurve end = MotionCurve::stepped(milliseconds(100), 4U);
    const MotionCurve start = MotionCurve::stepped(milliseconds(100), 4U, MotionStepPosition::start);
    require(end.easing == MotionEasing::steps && end.steps == 4U && end.step_position == MotionStepPosition::end,
            "steps(4) jumps at each step's end by default");
    require(motion_curve_easing(end, 0.3) == 0.25 && motion_curve_easing(end, 0.99) == 0.75 &&
                motion_curve_easing(end, 1.0) == 1.0 && motion_curve_easing(end, 0.0) == 0.0,
            "steps(4, end) holds each level to the step's end");
    require(motion_curve_easing(start, 0.3) == 0.5 && motion_curve_easing(start, 0.99) == 1.0 &&
                motion_curve_easing(start, 0.0) == 0.0,
            "steps(4, start) jumps at each step's start");
    require(MotionCurve::stepped(milliseconds(100), 0U).steps == 1U &&
                MotionCurve::stepped(milliseconds(100), 100U).steps == maximum_motion_steps,
            "the levels are clamped to [1, 32]");
}

void delays_are_clamped() {
    const MotionCurve curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    require(curve.delay == FrameInterval::zero(), "a curve has no delay by default");
    require(curve.delayed(milliseconds(50)).delay == milliseconds(50), "a delay is kept");
    require(curve.delayed(std::chrono::seconds(1)).delay == milliseconds(300),
            "the delay and duration together stay within the 400 ms ceiling");
    require(curve.delayed(milliseconds(-5)).delay == FrameInterval::zero(), "a negative delay is none");
    require(curve.delayed(milliseconds(50)) != curve, "the delay is part of the curve's identity");
}

// -- values on the clock ------------------------------------------------------------

class Mover final : public Control {
public:
    explicit Mover(StableId stable_id)
        : Control(std::move(stable_id)), value(*this, MotionCurve::timed(milliseconds(100), MotionEasing::linear)) {}
    MotionValue value;
};

struct ClockFixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Mover> mover;
    std::unique_ptr<Window> window;
};

std::unique_ptr<ClockFixture> make_clock_fixture(MotionCurve curve) {
    std::unique_ptr<ClockFixture> fixture = std::make_unique<ClockFixture>();
    ClockFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.mover = make_control<Mover>(StableId("mover"));
    (*made.root).add_child(made.mover);
    made.window = std::make_unique<Window>(made.root, Size{200.0, 100.0});
    (*made.window).motion_clock().set_time_source(&made.time);
    (*made.mover).value.set_curve(curve);
    return fixture;
}

void advance(ClockFixture& fixture, milliseconds step) {
    fixture.time.time += step;
    static_cast<void>((*fixture.window).poll_frame_schedule(fixture.time.time));
}

void steps_in_motion() {
    std::unique_ptr<ClockFixture> fixture = make_clock_fixture(MotionCurve::stepped(milliseconds(100), 4U));
    MotionValue& value = (*(*fixture).mover).value;
    require(value.animate_to(1.0) == MotionStart::started, "the stepped move starts");
    for (int frame = 0; frame < 20 && !value.settled(); ++frame) {
        advance(*fixture, milliseconds(16));
        const double level = value.current() * 4.0;
        require(level == std::floor(level), "a stepped value shows only its levels");
        require(value.velocity() == 0.0, "and never moves between them");
    }
    require(value.settled() && value.current() == 1.0, "it ends at its target");
}

void a_delay_holds_the_origin() {
    // Frames of 16 ms, the clock's cadence: a 48 ms delay, then 96 ms.
    const MotionCurve curve = MotionCurve::timed(milliseconds(96), MotionEasing::linear).delayed(milliseconds(48));
    std::unique_ptr<ClockFixture> fixture = make_clock_fixture(curve);
    MotionValue& value = (*(*fixture).mover).value;
    require(value.animate_to(1.0) == MotionStart::started, "the delayed move starts");
    for (int frame = 0; frame < 3; ++frame) {
        advance(*fixture, milliseconds(16));
        require(value.current() == 0.0 && !value.settled(), "during its delay the value stands at its origin");
    }
    advance(*fixture, milliseconds(16));
    require(near(value.current(), 1.0 / 6.0, 1e-12), "then it moves over its duration");
    for (int frame = 0; frame < 4; ++frame) advance(*fixture, milliseconds(16));
    require(!value.settled(), "not yet settled 128 ms in");
    advance(*fixture, milliseconds(16));
    require(value.settled() && value.current() == 1.0, "settled at 144 ms, its delay and duration");
}

void moves_through_stops() {
    std::unique_ptr<ClockFixture> fixture =
        make_clock_fixture(MotionCurve::timed(milliseconds(100), MotionEasing::linear));
    MotionValue& value = (*(*fixture).mover).value;
    const std::array<MotionStop, 1> stops{MotionStop{0.5, 2.0}};
    require(value.animate_through(stops, 1.0) == MotionStart::started, "a move through a stop starts");
    advance(*fixture, milliseconds(25));
    require(near(value.current(), 1.0, 1e-12), "a quarter in, halfway to the stop");
    require(value.animate_to(1.0) == MotionStart::unchanged, "animate_to the same target leaves it running");
    advance(*fixture, milliseconds(25));
    require(near(value.current(), 2.0, 1e-12), "at the stop's offset, its value");
    advance(*fixture, milliseconds(25));
    require(near(value.current(), 1.5, 1e-12), "then on toward the target");
    advance(*fixture, milliseconds(25));
    require(value.settled() && value.current() == 1.0, "ending at the target");

    // Refusals leave the value as it stands.
    const std::array<MotionStop, 2> backward{MotionStop{0.6, 0.0}, MotionStop{0.4, 0.0}};
    const std::array<MotionStop, 1> at_end{MotionStop{1.0, 0.0}};
    const std::array<MotionStop, 1> at_start{MotionStop{0.0, 0.0}};
    const std::array<MotionStop, 1> unbounded{MotionStop{0.5, std::nan("")}};
    const std::array<MotionStop, 5> many{MotionStop{0.1, 0.0}, MotionStop{0.2, 0.0}, MotionStop{0.3, 0.0},
                                         MotionStop{0.4, 0.0}, MotionStop{0.5, 0.0}};
    require(value.animate_through(backward, 0.0) == MotionStart::refused &&
                value.animate_through(at_end, 0.0) == MotionStart::refused &&
                value.animate_through(at_start, 0.0) == MotionStart::refused &&
                value.animate_through(unbounded, 0.0) == MotionStart::refused &&
                value.animate_through(many, 0.0) == MotionStart::refused,
            "stops out of order, at either end, not finite or too many are refused");
    value.set_curve(MotionCurve::follow());
    require(value.animate_through(stops, 0.0) == MotionStart::refused, "stops need a timed curve");
    require(value.settled() && value.current() == 1.0, "a refusal leaves the value as it stood");
}

// The bits of a fixed table of samples of the new curves, the same on every
// host. The expected value was produced on macOS arm64 when the motion clock
// began keeping its phase (2026-11-01; Windows x64 produced the one before)
// and is checked by every CI platform.
constexpr std::uint64_t expected_curve_hash = 0xf650b68d91b837f6ULL;

struct BitHash final {
    std::uint64_t value{14695981039346656037ULL};

    void add(double number) {
        std::uint64_t bits = 0U;
        std::memcpy(&bits, &number, sizeof bits);
        for (int shift = 0; shift < 64; shift += 8) {
            const std::uint64_t byte = (bits >> shift) & 0xFFU;
            value = value ^ byte;
            value = value * 1099511628211ULL;
        }
    }
};

void identical_samples() {
    BitHash hash{};
    const std::array<MotionCurve, 5> curves{
        MotionCurve::cubic_bezier(milliseconds(200), 0.25, 0.1, 0.25, 1.0),
        MotionCurve::cubic_bezier(milliseconds(300), 0.3, -0.05, 0.7, 1.05),
        MotionCurve::stepped(milliseconds(160), 5U, MotionStepPosition::start),
        MotionCurve::timed(milliseconds(150), MotionEasing::ease).delayed(milliseconds(40)),
        MotionCurve::timed(milliseconds(240), MotionEasing::smoothstep),
    };
    const std::array<MotionStop, 2> stops{MotionStop{0.3, 1.4}, MotionStop{0.7, 0.6}};
    for (std::size_t index = 0; index < curves.size(); ++index) {
        std::unique_ptr<ClockFixture> fixture = make_clock_fixture(curves[index]);
        MotionValue& value = (*(*fixture).mover).value;
        // The last curve runs through stops.
        if (index + 1U == curves.size()) {
            static_cast<void>(value.animate_through(stops, 1.0));
        } else {
            static_cast<void>(value.animate_to(1.0));
        }
        for (int frame = 0; frame < 60 && !value.settled(); ++frame) {
            advance(*fixture, milliseconds(7));
            hash.add(value.current());
            hash.add(value.velocity());
        }
    }
    if (print_mode()) {
        std::cout << "motion curve sample hash 0x" << std::hex << hash.value << std::dec << '\n';
        return;
    }
    require(hash.value == expected_curve_hash, "the curve samples have the same bits on every host");
}

// -- the rules ---------------------------------------------------------------------

PropertyTransition machine(std::string_view property, double hot_target, double rest_target) {
    PropertyTransition transition{};
    transition.property = std::string(property);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = hot_target;
    PresentationState rest{};
    rest.target = rest_target;
    transition.states.push_back(hot);
    transition.states.push_back(rest);
    transition.curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    return transition;
}

ThemeRule pad_rule(std::string id, std::string_view primitive) {
    ThemeRule rule{};
    rule.label = id;
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::move(id);
    rule.selector.part = "face";
    if (!primitive.empty()) rule.declarations.push_back(RuleDeclaration::primitive(primitive));
    return rule;
}

std::size_t invalid_count(const CompiledThemeRules& rules, std::string_view property) {
    std::size_t count = 0U;
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == RuleDiagnosticKind::transition_invalid && diagnostic.property == property) {
            count = count + 1U;
        }
    }
    return count;
}

void rules_check_translations_and_stops() {
    ThemeRuleSheet sheet{};
    ThemeRule far = pad_rule("far", "pill_plate");
    far.transitions.push_back(machine(translate_x_property, 33.0, 0.0));
    sheet.rules.push_back(far);
    ThemeRule backward = pad_rule("backward", "pill_plate");
    PropertyTransition reversed = machine(translate_y_property, 8.0, 0.0);
    reversed.states[0].stops = {MotionStop{0.6, 1.0}, MotionStop{0.4, 2.0}};
    backward.transitions.push_back(reversed);
    sheet.rules.push_back(backward);
    ThemeRule followed = pad_rule("followed", "pill_plate");
    PropertyTransition following = machine(translate_y_property, 8.0, 0.0);
    following.curve = MotionCurve::follow();
    following.states[0].stops = {MotionStop{0.5, 1.0}};
    followed.transitions.push_back(following);
    sheet.rules.push_back(followed);
    ThemeRule beyond = pad_rule("beyond", "pill_plate");
    PropertyTransition outside = machine(opacity_property, 1.0, 0.0);
    outside.states[0].stops = {MotionStop{0.5, 1.5}};
    beyond.transitions.push_back(outside);
    sheet.rules.push_back(beyond);
    ThemeRule crowded = pad_rule("crowded", "pill_plate");
    PropertyTransition many = machine(translate_x_property, 8.0, 0.0);
    many.states[0].stops = {MotionStop{0.1, 1.0}, MotionStop{0.2, 1.0}, MotionStop{0.3, 1.0}, MotionStop{0.4, 1.0},
                            MotionStop{0.5, 1.0}};
    crowded.transitions.push_back(many);
    sheet.rules.push_back(crowded);
    // Every engine property and a parameter on one part: six transitions.
    ThemeRule full = pad_rule("full", "bulge");
    PropertyTransition bounced = machine(translate_x_property, 8.0, 0.0);
    bounced.states[0].stops = {MotionStop{0.5, -4.0}};
    full.transitions.push_back(machine("amplitude", 1.0, 0.0));
    full.transitions.push_back(machine(translate_y_property, -4.0, 0.0));
    full.transitions.push_back(bounced);
    PropertyTransition fade{};
    fade.property = std::string(face_property);
    fade.curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    full.transitions.push_back(fade);
    full.transitions.push_back(machine(scale_property, 1.25, 1.0));
    full.transitions.push_back(machine(opacity_property, 1.0, 0.5));
    sheet.rules.push_back(full);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(invalid_count(rules, translate_x_property) == 2U && invalid_count(rules, translate_y_property) == 2U &&
                invalid_count(rules, opacity_property) == 1U && rules.diagnostics().size() == 5U,
            "a translation beyond 32 units and stops out of order, on a follow, out of range or too many are "
            "refused at load");
    CompiledThemeRules::PartFacts facts{};
    facts.stable_id = "full";
    facts.part = "face";
    facts.role = PartRole::face;
    const CompiledThemeRules::Resolution resolution = rules.resolve(facts);
    require(resolution.transition_count == maximum_part_transitions && maximum_part_transitions == 6U,
            "a part runs six transitions");
    const std::array<PresentationProperty, 6> order{
        PresentationProperty::opacity,     PresentationProperty::scale,       PresentationProperty::face,
        PresentationProperty::translate_x, PresentationProperty::translate_y, PresentationProperty::parameter};
    for (std::size_t index = 0; index < order.size(); ++index) {
        require((*resolution.transitions[index]).property == order[index],
                "opacity, scale, face, the translations, then parameters");
    }
    const CompiledThemeRules::CompiledTransition& across = *resolution.transitions[3];
    require(across.states[0].stops.size() == 1U && across.states[0].stops[0] == MotionStop{0.5, -4.0},
            "a state's stops compile with it");
}

// -- the translation drawn ----------------------------------------------------------

// A painter that keeps where the last part raster was composited and how
// large it was, and draws nothing.
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
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        last_origin = origin;
        last_extent = (*raster).extent();
        stretched = false;
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double) override {
        last_destination = destination;
        last_extent = (*raster).extent();
        stretched = true;
    }
    Point last_origin{};
    Rect last_destination{};
    DeviceExtent last_extent{};
    bool stretched{};
};

// A control showing one keyed face, hot while the pointer is over it.
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
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, 40.0, 24.0};
        part.key = PartKey{"face", {}, {}};
        part.granted_overflow = overflow;
        if (hot) part.states.insert(AnatomyState::hot);
        return part;
    }
    bool hot{};
    Insets overflow{};
};

struct PadFixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
    RasterWatch painter{};
};

// A pad drawn by `rules` added to the professional theme.
std::unique_ptr<PadFixture> make_pad_with_rules(Insets overflow, std::vector<ThemeRule> rules) {
    std::unique_ptr<PadFixture> fixture = std::make_unique<PadFixture>();
    PadFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.pad = make_control<Pad>(StableId("pad"));
    (*made.pad).set_requested_bounds(Rect{100.0, 100.0, 100.0, 80.0});
    (*made.pad).overflow = overflow;
    (*made.root).add_child(made.pad);
    made.window = std::make_unique<Window>(made.root, Size{400.0, 300.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "engine-motion-curve-test";
    for (ThemeRule& rule : rules) (*definition).rules.rules.push_back(std::move(rule));
    window.set_theme(Theme::create(*definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the pad's theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

std::unique_ptr<PadFixture> make_pad(Insets overflow, std::vector<PropertyTransition> transitions) {
    ThemeRule rule = pad_rule("pad", "pill_plate");
    rule.transitions = std::move(transitions);
    std::vector<ThemeRule> rules{};
    rules.push_back(std::move(rule));
    return make_pad_with_rules(overflow, std::move(rules));
}

void repaint(PadFixture& fixture) {
    Window& window = *fixture.window;
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

void hover(PadFixture& fixture) {
    static_cast<void>((*fixture.window).dispatch_pointer({PointerAction::move, PointerButton::none, Point{150.0, 140.0}}));
    repaint(fixture);
}

void frame(PadFixture& fixture, milliseconds step) {
    fixture.time.time += step;
    static_cast<void>((*fixture.window).poll_frame_schedule(fixture.time.time));
    repaint(fixture);
}

void settle(PadFixture& fixture) {
    Window& window = *fixture.window;
    for (int step = 0; step < 40 && window.motion_clock().armed(); ++step) frame(fixture, milliseconds(16));
    require(!window.motion_clock().armed(), "the motion settles");
}

void translation_moves_the_drawn_part() {
    std::vector<PropertyTransition> moves{};
    moves.push_back(machine(translate_x_property, 8.0, 0.0));
    moves.push_back(machine(translate_y_property, -4.0, 0.0));
    std::unique_ptr<PadFixture> fixture = make_pad(Insets{10.0, 6.0, 10.0, 6.0}, std::move(moves));
    PadFixture& made = *fixture;
    const Point rest = made.painter.last_origin;
    const DeviceExtent rest_extent = made.painter.last_extent;
    const ThemeEngine& engine = (*made.window).theme().engine();
    const PartHitRegion rest_region = engine.hit_region(*made.pad, (*made.pad).face());
    const Rect layout = (*made.pad).arranged_bounds();
    hover(made);
    settle(made);
    require(made.painter.last_origin.x == rest.x + 8.0 && made.painter.last_origin.y == rest.y - 4.0,
            "hot, the face is drawn 8 across and 4 up");
    require(made.painter.last_extent == rest_extent && !made.painter.stretched, "at its own size, unstretched");
    const PartHitRegion hot_region = engine.hit_region(*made.pad, (*made.pad).face());
    require(hot_region.bounds == rest_region.bounds && hot_region.shape == rest_region.shape,
            "the hit region does not move");
    require((*made.pad).arranged_bounds() == layout, "nor does the layout");
    static_cast<void>((*made.window).dispatch_pointer({PointerAction::move, PointerButton::none, Point{390.0, 290.0}}));
    repaint(made);
    settle(made);
    require(made.painter.last_origin.x == rest.x && made.painter.last_origin.y == rest.y, "left, it returns");
}

void translation_is_clamped_to_the_grant() {
    std::vector<PropertyTransition> moves{};
    moves.push_back(machine(translate_x_property, 8.0, 0.0));
    moves.push_back(machine(translate_y_property, -4.0, 0.0));
    std::unique_ptr<PadFixture> some = make_pad(Insets{3.0, 0.0, 3.0, 2.0}, moves);
    const Point rest = (*some).painter.last_origin;
    hover(*some);
    settle(*some);
    require((*some).painter.last_origin.x == rest.x + 3.0 && (*some).painter.last_origin.y == rest.y,
            "a part moves only as far as its grant, and not at all toward a side granted none");

    // The scale takes its share of the grant first: 1.25 grows the 40 wide
    // face by 5 a side, leaving 5 of the 10 granted for the move.
    std::vector<PropertyTransition> both{};
    both.push_back(machine(scale_property, 1.25, 1.0));
    both.push_back(machine(translate_x_property, 8.0, 0.0));
    std::unique_ptr<PadFixture> scaled = make_pad(Insets{10.0, 6.0, 10.0, 6.0}, both);
    const Point scaled_rest = (*scaled).painter.last_origin;
    hover(*scaled);
    settle(*scaled);
    require((*scaled).painter.last_extent.width == 50U && (*scaled).painter.last_origin.x == scaled_rest.x &&
                (*scaled).painter.last_origin.y == scaled_rest.y - 3.0,
            "scaled about its centre, then moved by what the scale leaves");

    std::unique_ptr<PadFixture> none = make_pad(Insets{}, moves);
    const Point fixed = (*none).painter.last_origin;
    hover(*none);
    settle(*none);
    require((*none).painter.last_origin.x == fixed.x && (*none).painter.last_origin.y == fixed.y,
            "a part granted no overflow never moves");
}

void stops_play_on_entering() {
    // Hot: a dip to -6 at half its time, then on to 8; rest has no stops.
    PropertyTransition bounce = machine(translate_x_property, 8.0, 0.0);
    bounce.states[0].stops = {MotionStop{0.5, -6.0}};
    std::vector<PropertyTransition> moves{};
    moves.push_back(bounce);
    std::unique_ptr<PadFixture> fixture = make_pad(Insets{10.0, 6.0, 10.0, 6.0}, std::move(moves));
    PadFixture& made = *fixture;
    const Point rest = made.painter.last_origin;
    hover(made);
    const std::array<double, 4> expected{-3.0, -6.0, 1.0, 8.0};
    for (const double offset : expected) {
        frame(made, milliseconds(25));
        require(near(made.painter.last_origin.x - rest.x, offset, 1e-9), "entering hot passes the stop");
    }
    settle(made);
    require(made.painter.last_origin.x == rest.x + 8.0, "and settles at the target");
    static_cast<void>((*made.window).dispatch_pointer({PointerAction::move, PointerButton::none, Point{390.0, 290.0}}));
    repaint(made);
    frame(made, milliseconds(50));
    require(near(made.painter.last_origin.x - rest.x, 4.0, 1e-9), "leaving, a state without stops moves straight");
    settle(made);
}

void stops_play_entering_from_another_rule() {
    // The pad's own rule rests at 0 on an instant machine; a rule of its
    // own for hot (as T3 lowers an animation without a transition to join)
    // runs a machine of one state, a dip to -6 and on to 8. Arriving in it
    // from the other rule's machine enters it, so its stop plays.
    ThemeRule resting = pad_rule("pad", "pill_plate");
    PropertyTransition still{};
    still.property = std::string(translate_x_property);
    PresentationState zero{};
    still.states.push_back(zero);
    still.curve = MotionCurve::timed(FrameInterval{}, MotionEasing::linear);
    resting.transitions.push_back(still);
    ThemeRule animated = pad_rule("pad", "pill_plate");
    animated.selector.states.push_back(AnatomyState::hot);
    PropertyTransition bounce{};
    bounce.property = std::string(translate_x_property);
    PresentationState hot{};
    hot.target = 8.0;
    hot.stops = {MotionStop{0.5, -6.0}};
    bounce.states.push_back(hot);
    bounce.curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    animated.transitions.push_back(bounce);
    std::vector<ThemeRule> rules{};
    rules.push_back(std::move(resting));
    rules.push_back(std::move(animated));
    std::unique_ptr<PadFixture> fixture = make_pad_with_rules(Insets{10.0, 6.0, 10.0, 6.0}, std::move(rules));
    PadFixture& made = *fixture;
    const Point rest = made.painter.last_origin;
    for (int pass = 0; pass < 2; ++pass) {
        hover(made);
        const std::array<double, 4> expected{-3.0, -6.0, 1.0, 8.0};
        for (const double offset : expected) {
            frame(made, milliseconds(25));
            require(near(made.painter.last_origin.x - rest.x, offset, 1e-9),
                    "arriving from another rule's machine passes the stop, each time");
        }
        settle(made);
        static_cast<void>(
            (*made.window).dispatch_pointer({PointerAction::move, PointerButton::none, Point{390.0, 290.0}}));
        repaint(made);
        require(made.painter.last_origin.x == rest.x, "leaving, the instant machine returns at once");
    }
}

} // namespace

int main() {
    cubic_bezier_easing();
    steps_easing();
    delays_are_clamped();
    steps_in_motion();
    a_delay_holds_the_origin();
    moves_through_stops();
    identical_samples();
    rules_check_translations_and_stops();
    translation_moves_the_drawn_part();
    translation_is_clamped_to_the_grant();
    stops_play_on_entering();
    stops_play_entering_from_another_rule();
    std::cout << "motion curves: cubic-bezier, steps, delays, stops, identical samples, translation rules, the "
                 "translation within its grant and stops on entering passed\n";
    return 0;
}
