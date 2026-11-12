// The theme engine's inputs and damage by dependency (T2 plan, phase E6;
// GIVEN B1-1, B2-1 and B4-2): the engine inputs are vocabulary; a part's
// expressions read the pointer in part-local logical units only while the
// part is hot, the time in its states, the two scales and the values the
// application publishes; and a change to an input damages only the parts
// that read it, within their extents, on the control that recorded them.
// A parameter the pointer drives steps through the engine's keyframes (E7r).
// The window's metrics count it: a pointer moving over a part that reads no
// pointer input damages nothing, a reading part is damaged only within its
// extent, and a published value reaches only its readers.

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"
#include "support/routed_input_probe.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_input_tests: " << message << '\n';
    std::exit(1);
}

using Instruction = ExpressionInstruction;
using Operation = ExpressionOperation;

// The test's time for the window's motion clock, from which time_in_state
// is counted.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A painter that draws nothing: the window records each control's commands
// itself and replays them here.
class DiscardingPainter final : public Painter {
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
};

// A control showing one keyed face, hot while the pointer is over the
// control. Moving the pointer within it changes nothing it draws itself:
// only what the theme's expressions read can need a repaint. It may also
// paint a face of another control, as a docking site's drag layer paints
// the site's tiles, after translating the painter.
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
        part.bounds = face;
        part.key = PartKey{"face", {}, {}};
        if (hot) part.states.insert(AnatomyState::hot);
        if (on_behalf_of == nullptr) {
            paint_part(painter, *this, part);
            return;
        }
        painter.save();
        painter.translate(offset);
        paint_part(painter, *on_behalf_of, part);
        painter.restore();
    }

    bool hot{};
    Rect face{20.0, 20.0, 40.0, 30.0};
    // When set, the face painted is this control's, offset by `offset`.
    const Control* on_behalf_of{};
    Point offset{};
};

ThemeRule bulge_rule(std::string stable_id, std::vector<RuleDeclaration> declarations) {
    ThemeRule rule{};
    rule.label = stable_id;
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::move(stable_id);
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("bulge"));
    for (RuleDeclaration& declaration : declarations) rule.declarations.push_back(std::move(declaration));
    return rule;
}

// amplitude = clamp(input × scale + offset, 0, 1), falling back to
// `fallback`. The fallbacks and offsets keep every amplitude above 0, so
// that each face is drawn and its raster can be inspected.
std::vector<RuleDeclaration> amplitude_from(ExpressionInputIndex input, double scale, double fallback,
                                            double offset = 0.0) {
    const std::array<Instruction, 8> program{Instruction::input(input),      Instruction::number(scale),
                                             Instruction::apply(Operation::multiply), Instruction::number(offset),
                                             Instruction::apply(Operation::add),      Instruction::number(0.0),
                                             Instruction::number(1.0),       Instruction::apply(Operation::clamp)};
    std::vector<RuleDeclaration> declarations{};
    declarations.push_back(RuleDeclaration::expression_value("amplitude", program, fallback));
    return declarations;
}

std::vector<RuleDeclaration> fixed_amplitude(double amplitude) {
    std::vector<RuleDeclaration> declarations{};
    declarations.push_back(RuleDeclaration::number_value("amplitude", amplitude));
    return declarations;
}

constexpr ExpressionInputIndex pointer_x = engine_input_position(EngineInput::pointer_x);

// The pads: `still` reads no input, `reader` and `idle_reader` read
// pointer_x, `level_reader` and `other_reader` the published values `level`
// and `other`, `clock` the time in its states, `scaled` the text scale and
// `layer` paints the face of `host`, which reads pointer_x, from 5 units
// right and 5 down.
std::unique_ptr<ThemeDefinition> input_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "engine-input-test";
    ThemeRuleSheet& sheet = definition.rules;
    sheet.published_values = {"level", "other"};
    sheet.rules.push_back(bulge_rule("still", fixed_amplitude(0.5)));
    sheet.rules.push_back(bulge_rule("reader", amplitude_from(pointer_x, 1.0 / 40.0, 0.0625)));
    sheet.rules.push_back(bulge_rule("idle-reader", amplitude_from(pointer_x, 1.0 / 40.0, 0.0625)));
    sheet.rules.push_back(bulge_rule("level-reader", amplitude_from(published_value_position(0U), 1.0, 0.125)));
    sheet.rules.push_back(bulge_rule("other-reader", amplitude_from(published_value_position(1U), 1.0, 0.125)));
    sheet.rules.push_back(
        bulge_rule("clock", amplitude_from(engine_input_position(EngineInput::time_in_state), 1.0, 0.0625, 0.125)));
    sheet.rules.push_back(
        bulge_rule("scaled", amplitude_from(engine_input_position(EngineInput::text_scale), 0.25, 0.0625)));
    sheet.rules.push_back(bulge_rule("host", amplitude_from(pointer_x, 1.0 / 40.0, 0.0625)));
    return held;
}

struct Fixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> still;
    std::shared_ptr<Pad> reader;
    std::shared_ptr<Pad> idle_reader;
    std::shared_ptr<Pad> level_reader;
    std::shared_ptr<Pad> other_reader;
    std::shared_ptr<Pad> clock;
    std::shared_ptr<Pad> scaled;
    std::shared_ptr<Pad> host;
    std::shared_ptr<Pad> layer;
    std::unique_ptr<Window> window;
    DiscardingPainter painter{};
};

std::shared_ptr<Pad> add_pad(Panel& root, std::string id, Rect bounds) {
    std::shared_ptr<Pad> pad = make_control<Pad>(StableId(std::move(id)));
    (*pad).set_requested_bounds(bounds);
    root.add_child(pad);
    return pad;
}

// Pads of 100 by 80 in a row of 1000 by 400: the n-th at x = 100 n.
std::unique_ptr<Fixture> make_fixture() {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    Panel& root = *made.root;
    made.still = add_pad(root, "still", {0.0, 0.0, 100.0, 80.0});
    made.reader = add_pad(root, "reader", {100.0, 0.0, 100.0, 80.0});
    made.idle_reader = add_pad(root, "idle-reader", {200.0, 0.0, 100.0, 80.0});
    made.level_reader = add_pad(root, "level-reader", {300.0, 0.0, 100.0, 80.0});
    made.other_reader = add_pad(root, "other-reader", {400.0, 0.0, 100.0, 80.0});
    made.clock = add_pad(root, "clock", {500.0, 0.0, 100.0, 80.0});
    made.scaled = add_pad(root, "scaled", {600.0, 0.0, 100.0, 80.0});
    made.host = add_pad(root, "host", {700.0, 0.0, 100.0, 80.0});
    // The layer lies over the host's place, below it in the row, so that
    // the pointer reaches the host; it paints the host's face.
    made.layer = add_pad(root, "layer", {700.0, 100.0, 100.0, 80.0});
    (*made.layer).on_behalf_of = made.host.get();
    (*made.layer).offset = Point{5.0, 5.0};
    (*made.host).face = Rect{};
    made.window = std::make_unique<Window>(made.root, Size{1000.0, 400.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_theme(Theme::create(*input_theme()));
    require(window.theme().engine().rules().diagnostics().empty(), "the input theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

// Paints what is pending and returns the window damage it painted.
DamageRegion repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
    return damage;
}

// The keyframe level nearest `value` of an amplitude over [0, 1] (E7r;
// owner D, 2026-10-22): a parameter the pointer drives steps through the
// engine's animated_keyframe_levels levels, the ends exact.
double keyframe_level(double value) {
    const double steps = static_cast<double>(animated_keyframe_levels - 1U);
    const double level = std::round(std::clamp(value, 0.0, 1.0) * steps);
    if (level >= steps) return 1.0;
    const double shown = level / steps;
    return shown;
}

// The amplitude a keyed face was last drawn with, from its cached raster.
double drawn_amplitude(const Window& window, std::string_view control) {
    const std::optional<PartRecordHandle> handle = window.engine_parts().find(control, PartKey{"face", {}, {}});
    require(handle.has_value(), "the face has a record");
    const EnginePartRecord* const record = window.engine_parts().record(*handle);
    require(record != nullptr && (*record).cached != nullptr && (*(*record).cached).raster != nullptr,
            "the face was drawn by its primitive");
    const PrimitiveDescriptor& descriptor = (*(*(*record).cached).primitive).descriptor();
    const std::optional<std::size_t> amplitude = find_parameter(descriptor, "amplitude");
    require(amplitude.has_value(), "the bulge has an amplitude");
    const double value = (*(*record).cached).parameters.values[*amplitude].number;
    return value;
}

bool covers_only(const DamageRegion& damage, Rect expected) {
    if (damage.rectangles().size() != 1U) return false;
    const bool exact = damage.rectangles()[0] == expected;
    return exact;
}

// -- the pointer -----------------------------------------------------------------

void unread_pointer_damages_nothing() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    move_to(window, {50.0, 40.0});
    static_cast<void>(repaint(*fixture));
    require((*(*fixture).still).hot, "the still pad is hot");
    window.reset_activity_metrics();
    for (int step = 1; step <= 5; ++step) {
        move_to(window, {50.0 + static_cast<double>(step), 40.0});
        const DamageRegion damage = window.take_damage();
        require(damage.empty(), "a pointer moving over a part that reads no pointer input damages nothing");
    }
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.engine_input_changes == 5U && metrics.engine_input_damaged_parts == 0U &&
                metrics.engine_input_damage_area == 0.0 && metrics.dirty_marks == 0U,
            "five pointer changes, no part damaged and nothing marked");
}

void reader_is_damaged_within_its_extent() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    // Before the pointer reaches it, the reader's pointer is not published:
    // its expression gives the fallback.
    require(drawn_amplitude(window, "reader") == keyframe_level(0.0625),
            "a part that is not hot reads no pointer: the fallback, at its keyframe");
    move_to(window, {130.0, 40.0});
    static_cast<void>(repaint(*fixture));
    // Part-local logical units: the face's left edge is at window x 120.
    require(drawn_amplitude(window, "reader") == keyframe_level(10.0 / 40.0),
            "the pointer is read from the part's corner");
    window.reset_activity_metrics();
    const Rect face_in_window{120.0, 20.0, 40.0, 30.0};
    for (int step = 1; step <= 4; ++step) {
        move_to(window, {130.0 + 4.0 * static_cast<double>(step), 40.0});
        const DamageRegion damage = repaint(*fixture);
        require(covers_only(damage, face_in_window), "the reading part is damaged only within its extent");
        const double expected = keyframe_level((10.0 + 4.0 * static_cast<double>(step)) / 40.0);
        require(std::fabs(drawn_amplitude(window, "reader") - expected) < 1e-12, "and drawn at the new position");
    }
    const MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.engine_input_damaged_parts == 4U && metrics.engine_input_damage_area == 4.0 * 40.0 * 30.0,
            "four parts damaged over four extents; the idle reader, not hot, is not among them");
    // Leaving: the pad repaints itself and the pointer is no longer read.
    move_to(window, {50.0, 40.0});
    static_cast<void>(repaint(*fixture));
    require(drawn_amplitude(window, "reader") == keyframe_level(0.0625),
            "away from the part the pointer is unpublished again");
}

void painted_on_behalf_is_damaged_where_drawn() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    move_to(window, {720.0, 40.0});
    static_cast<void>(repaint(*fixture));
    // The layer drew the host's face 5 units in, at window (725, 125).
    const Rect layer_extent{700.0 + 5.0 + 20.0, 100.0 + 5.0 + 20.0, 40.0, 30.0};
    // The host is hot; the layer paints its face with the host's states
    // only if it is told: here the layer's own `hot` decides, so make it hot.
    (*(*fixture).layer).hot = true;
    (*(*fixture).layer).invalidate(Dirty::paint);
    static_cast<void>(repaint(*fixture));
    move_to(window, {722.0, 40.0});
    const DamageRegion damage = window.take_damage();
    require(covers_only(damage, layer_extent),
            "a part painted on another control's behalf is damaged where it was recorded");
}

// -- published values ---------------------------------------------------------

void published_value_reaches_only_its_readers() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    require(drawn_amplitude(window, "level-reader") == 0.125, "an unpublished value gives the fallback");
    window.reset_activity_metrics();
    require(window.publish_value("level", 0.75), "a value is published");
    const DamageRegion damage = repaint(*fixture);
    require(covers_only(damage, Rect{320.0, 20.0, 40.0, 30.0}), "the published value reaches only its reader");
    require(drawn_amplitude(window, "level-reader") == 0.75 && drawn_amplitude(window, "other-reader") == 0.125,
            "its reader shows it; the other value's reader does not change");
    require(window.publish_value("level", 0.75) && window.take_damage().empty(),
            "publishing the same value damages nothing");
    require(window.publish_value("other", 0.5), "the other value is published");
    require(covers_only(repaint(*fixture), Rect{420.0, 20.0, 40.0, 30.0}), "and reaches only its own reader");
    require(window.publish_value("unread", 1.0) && window.take_damage().empty(),
            "a value no theme reads damages nothing");
    MetricsSnapshot metrics = window.metrics_snapshot();
    require(metrics.engine_input_changes == 3U && metrics.engine_input_damaged_parts == 2U,
            "three changes, two parts damaged");
    window.withdraw_value("level");
    static_cast<void>(repaint(*fixture));
    require(drawn_amplitude(window, "level-reader") == 0.125 && !window.published_value("level"),
            "a withdrawn value reads as unpublished");
    require(!window.publish_value("Level", 1.0) && !window.publish_value("level", std::nan("")) &&
                !window.publish_value("", 1.0) && !window.published_value("Level"),
            "an invalid name or a value that is not finite is refused");
    metrics = window.metrics_snapshot();
    require(metrics.engine_input_damaged_parts == 3U, "the withdrawal damaged its reader");
}

// -- time and scales ------------------------------------------------------------

void time_and_scales() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    require(drawn_amplitude(window, "clock") == 0.125, "no time has passed in the part's states");
    (*fixture).time.time += std::chrono::milliseconds(250);
    (*(*fixture).clock).invalidate(Dirty::paint);
    static_cast<void>(repaint(*fixture));
    require(drawn_amplitude(window, "clock") == 0.375, "time_in_state counts from the paint that showed the states");
    require(drawn_amplitude(window, "scaled") == 0.25, "the text scale is read");
    PresentationSettings settings = window.presentation_settings();
    settings.text_scale = 2.0;
    window.set_presentation_settings(settings);
    window.perform_layout();
    static_cast<void>(repaint(*fixture));
    require(drawn_amplitude(window, "scaled") == 0.5, "a new text scale is read");

    // A part painted without a key is drawn statically: it reads only the
    // scales.
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "control";
    facts.stable_id = "clock";
    facts.part = "face";
    facts.role = PartRole::face;
    const CompiledThemeRules::Resolution resolution = window.theme().engine().rules().resolve(facts);
    require(resolution.inputs_read == expression_input_bit(engine_input_position(EngineInput::time_in_state)),
            "the clock's face reads time_in_state, statically known");
}

// -- the vocabulary and the rules ----------------------------------------------

// A primitive that reads the pointer itself and paints a flat colour.
class PointerPrimitive final : public EnginePrimitive {
public:
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override { return descriptor_; }
    PrimitiveDescriptor descriptor_{};

private:
    void derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept override {}
    void fill(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<const double>,
              const PartRasterView& destination) const noexcept override {
        for (std::uint8_t& byte : destination.pixels) byte = 0U;
    }
};

constexpr std::array<PartRole, 1> sheen_roles{PartRole::sheen};
constexpr std::array<EngineInput, 1> pointer_inputs{EngineInput::pointer_x};

void vocabulary_and_rules() {
    require(engine_input_count == 5U && engine_input_name(EngineInput::pointer_x) == "pointer_x" &&
                engine_input_name(EngineInput::text_scale) == "text_scale",
            "the engine inputs are vocabulary, with no density");
    require(published_value_position(0U).value == engine_input_count, "published values follow the engine inputs");

    PointerPrimitive sheen{};
    sheen.descriptor_.name = "pointer_sheen";
    sheen.descriptor_.roles = sheen_roles;
    sheen.descriptor_.cost = PrimitiveCost{4U, 0U, 0U};
    sheen.descriptor_.inputs_read = pointer_inputs;
    PrimitiveRegistry registry{};
    require(registry.register_primitive(sheen) == RegistrationOutcome::registered, "a pointer-reading primitive");
    PointerPrimitive broken{};
    broken.descriptor_ = sheen.descriptor_;
    broken.descriptor_.name = "broken_sheen";
    const std::array<EngineInput, 1> beyond{static_cast<EngineInput>(engine_input_count)};
    broken.descriptor_.inputs_read = beyond;
    require(registry.register_primitive(broken) == RegistrationOutcome::input_out_of_range,
            "an input outside the vocabulary is refused");

    ThemeRuleSheet sheet{};
    ThemeRule rule{};
    rule.label = "sheen";
    rule.selector.role = PartRole::sheen;
    rule.declarations.push_back(RuleDeclaration::primitive("pointer_sheen"));
    sheet.rules.push_back(rule);
    const CompiledThemeRules compiled = CompiledThemeRules::compile(sheet, registry);
    CompiledThemeRules::PartFacts facts{};
    facts.role = PartRole::sheen;
    require(compiled.resolve(facts).inputs_read == expression_input_bit(pointer_x),
            "a part knows the inputs its primitive reads");

    // Expression declarations are checked when the sheet compiles.
    ThemeRuleSheet faulty{};
    faulty.published_values = {"level", "Level", "level"};
    const std::array<Instruction, 2> unbalanced{Instruction::number(1.0), Instruction::number(2.0)};
    const std::array<Instruction, 1> colour{Instruction::parameter(ExpressionParameterIndex{2})};
    const std::array<Instruction, 1> far{Instruction::input(published_value_position(3U))};
    const std::array<Instruction, 1> half{Instruction::number(0.5)};
    ThemeRule faults{};
    faults.label = "faults";
    faults.selector.role = PartRole::face;
    faults.declarations.push_back(RuleDeclaration::primitive("bulge"));
    faults.declarations.push_back(RuleDeclaration::expression_value("amplitude", unbalanced, 0.0));
    faults.declarations.push_back(RuleDeclaration::expression_value("light_peak", colour, 0.0));
    faults.declarations.push_back(RuleDeclaration::expression_value("shade_peak", far, 0.0));
    faults.declarations.push_back(RuleDeclaration::expression_value("light_colour", half, 0.0));
    faults.declarations.push_back(RuleDeclaration::expression_value("crown_peak", half, 2.0));
    faulty.rules.push_back(faults);
    const CompiledThemeRules checked = CompiledThemeRules::compile(faulty, builtin_primitive_registry());
    std::size_t matched = 0U;
    for (const RuleDiagnostic& diagnostic : checked.diagnostics()) {
        if (diagnostic.kind == RuleDiagnosticKind::published_value_invalid && diagnostic.rule == sheet_diagnostic) {
            ++matched;
        }
        if (diagnostic.kind == RuleDiagnosticKind::expression_invalid && diagnostic.property == "amplitude" &&
            diagnostic.expression == ExpressionDefect::unbalanced) {
            ++matched;
        }
        if (diagnostic.kind == RuleDiagnosticKind::expression_reads_colour && diagnostic.property == "light_peak") {
            ++matched;
        }
        if (diagnostic.kind == RuleDiagnosticKind::expression_invalid && diagnostic.property == "shade_peak" &&
            diagnostic.expression == ExpressionDefect::input_out_of_range) {
            ++matched;
        }
        if (diagnostic.kind == RuleDiagnosticKind::parameter_wrong_kind && diagnostic.property == "light_colour") {
            ++matched;
        }
        if (diagnostic.kind == RuleDiagnosticKind::parameter_out_of_range && diagnostic.property == "crown_peak") {
            ++matched;
        }
    }
    require(matched == 7U && checked.diagnostics().size() == 7U,
            "two unusable published names, and each faulty expression dropped with its own diagnostic");
    require(checked.published_values().size() == 3U && checked.published_values()[1].empty() &&
                checked.published_value_input("level") == published_value_position(0U) &&
                !checked.published_value_input("Level"),
            "an unusable name keeps its position, empty");
}

} // namespace

int main() {
    unread_pointer_damages_nothing();
    reader_is_damaged_within_its_extent();
    painted_on_behalf_is_damaged_where_drawn();
    published_value_reaches_only_its_readers();
    time_and_scales();
    vocabulary_and_rules();
    std::cout << "engine inputs: the vocabulary, the pointer, published values, time, scales and damage by "
                 "dependency passed\n";
    return EXIT_SUCCESS;
}
