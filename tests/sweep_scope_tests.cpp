// SweepScope, the instruments package's level scope: its properties and
// their bounds, its anatomy and layout, its semantics, the scope_sweep
// effect drawn directly (the trace and the sweep, the still pose, the phase
// following the ticks, the same ticks giving the same state bytes), and the
// scope hosting it on its window's motion clock: enlisted while drawn,
// dropped when not painted between frames, stopped by reduced motion and
// the ambient switch, withdrawn when the scope leaves its window. Time comes
// from the test, never the wall clock.

#include "gui_forms/instrument_controls.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/controls/instruments/sweep_scope/sweep_scope_effects.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_sweep_scope_tests: " << message << '\n';
    std::exit(1);
}

// Records the lines drawn (the effect's strokes) and checks saves balance.
class RecordingPainter final : public Painter {
public:
    void save() override { saves = saves + 1; }
    void restore() override { restores = restores + 1; }
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_linear_gradient_spread(Rect, Point, Point, std::span<const GradientStop>, GradientSpreadMode) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_line(Point from, Point to, Color color, double) override {
        const Line line{from, to, color};
        lines.push_back(line);
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    struct Line final {
        Point from{};
        Point to{};
        Color color{};
    };
    int saves{};
    int restores{};
    std::vector<Line> lines{};
};

const FrameTime start{std::chrono::seconds(1000)};

const EffectEntry& scope_sweep() {
    const EffectEntry* const entry = built_in_effect_registry().find("scope_sweep");
    require(entry != nullptr, "scope_sweep is a built-in effect");
    return *entry;
}

EffectView<SweepScope> scope_view(double fraction) {
    EffectView<SweepScope> view{};
    view.fraction = fraction;
    view.plot = Size{200.0, 80.0};
    view.rising = Color::rgba(38, 166, 154);
    view.falling = Color::rgba(241, 196, 15);
    view.sweep = Color::rgba(38, 166, 154, 150);
    view.period_seconds = 1.0;
    view.still_phase = 0.5;
    return view;
}

// The sweep line is the one drawn in the sweep colour.
const RecordingPainter::Line* sweep_line(const RecordingPainter& painter, Color sweep) {
    const RecordingPainter::Line* found = nullptr;
    for (const RecordingPainter::Line& line : painter.lines) {
        if (line.color == sweep) found = &line;
    }
    return found;
}

void properties() {
    std::shared_ptr<SweepScope> scope = make_control<SweepScope>(StableId("scope.properties"));
    require((*scope).caption.read() == "LEVEL", "the caption defaults to LEVEL");
    require((*scope).sweep_period.read() == std::chrono::microseconds(1'234'568), "the sweep crosses in about 1.23 s");
    require(!(*scope).focusable.read(), "the scope takes no focus");
    (*scope).set_property_value("caption", BindingValue{std::string("GAIN")});
    require((*scope).caption.read() == "GAIN", "the caption property sets the caption");
    (*scope).set_property_value("sweep_period", BindingValue{2.5});
    require((*scope).sweep_period.read() == std::chrono::milliseconds(2500), "the sweep_period property is in seconds");
    require(std::get<double>(*(*scope).property_value("sweep_period")) == 2.5, "and reads back in seconds");
    bool refused = false;
    try {
        (*scope).sweep_period = std::chrono::milliseconds(99);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused, "a period under 0.1 s is refused");
    refused = false;
    try {
        (*scope).sweep_period = std::chrono::seconds(61);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*scope).sweep_period.read() == std::chrono::milliseconds(2500),
            "a period over 60 s is refused, leaving the period as it was");
    (*scope).sweep_period = std::chrono::seconds(60);
    require((*scope).sweep_period.read() == std::chrono::seconds(60), "60 s is admitted");
}

void anatomy_and_semantics() {
    std::shared_ptr<SweepScope> scope = make_control<SweepScope>(StableId("scope.anatomy"));
    (*scope).set_requested_bounds({0.0, 0.0, 220.0, 120.0});
    (*scope).value = 42.0;
    Window window(scope, {220.0, 120.0});
    window.perform_layout();
    const Rect plot = (*scope).plot_bounds();
    require(plot.x == 8.0 && plot.y == 22.0 && plot.width == 204.0 && plot.height == 90.0,
            "the plot lies under the caption, inset from the edges");
    const SweepScope::Parts parts = (*scope).parts();
    require(parts.region.present && parts.caption.present && parts.plot.present && parts.graticule.present &&
                parts.indicator.present && parts.activity.present,
            "every part is present");
    require(parts.plot.bounds.x == plot.x && parts.plot.bounds.height == plot.height, "the plot part is the plot");
    require(parts.graticule.bounds.y > plot.y && parts.graticule.bounds.y + parts.graticule.bounds.height <
                                                     plot.y + plot.height,
            "the graticule lies inside the plot");
    const SweepScope::States states = (*scope).states();
    require(states.normal && states.enabled && !states.disabled && !states.reduced_motion, "the ordinary states");
    require(!(*scope).hit_test_local({20.0, 40.0}), "the pointer passes through the scope");
    const SemanticDescriptor descriptor = (*scope).semantic_descriptor();
    require(descriptor.role == SemanticRole::progress_bar, "the scope reads as a meter");
    require(descriptor.name == "LEVEL", "an unnamed scope is named by its caption");
    require(descriptor.value == "LEVEL 42%", "its value is the caption and the percentage");
    require(descriptor.numeric_value == 42.0 && descriptor.minimum_value == 0.0 && descriptor.maximum_value == 100.0,
            "and its range");
    (*scope).enabled = false;
    require((*scope).states().disabled, "a disabled scope reports it");
}

void effect_draws() {
    const EffectView<SweepScope> view = scope_view(0.25);
    const Rect plot{8.0, 22.0, 200.0, 80.0};
    RecordingPainter moving;
    EffectInstance instance;
    instance.attach(scope_sweep(), 1U, start);
    require(scope_sweep().parent == "sweep_scope", "the effect names its parent");
    instance.paint(moving, plot, 1.0, false, view);
    require(moving.lines.size() == 3U, "the effect draws the rise, the fall and the sweep");
    require(moving.saves == moving.restores, "the effect's saves balance");
    require(moving.lines[0].to.x == 2.0 + 0.25 * 196.0 && moving.lines[0].to.y == 2.0,
            "the trace peaks above the level's place");
    require(instance.last_meter().refused == 0U, "the sweep stays within budget");

    RecordingPainter still;
    instance.paint(still, plot, 1.0, true, view);
    const RecordingPainter::Line* still_sweep = sweep_line(still, view.sweep);
    require(still_sweep != nullptr && (*still_sweep).from.x == 100.0, "a still sweep rests in the middle");

    // A quarter of a one-second period, in frames of three ticks (a frame
    // runs at most four): the sweep a quarter across.
    RecordingPainter later;
    for (int frame = 1; frame <= 5; ++frame) {
        static_cast<void>(instance.advance(start + std::chrono::milliseconds(50 * frame)));
        later.lines.clear();
        instance.paint(later, plot, 1.0, false, view);
    }
    const RecordingPainter::Line* moved = sweep_line(later, view.sweep);
    require(moved != nullptr && std::abs((*moved).from.x - 50.0) < 1.0, "the sweep follows its ticks");

    EffectInstance twin;
    twin.attach(scope_sweep(), 1U, start);
    RecordingPainter twin_first;
    twin.paint(twin_first, plot, 1.0, false, view);
    RecordingPainter twin_later;
    for (int frame = 1; frame <= 5; ++frame) {
        static_cast<void>(twin.advance(start + std::chrono::milliseconds(50 * frame)));
        twin.paint(twin_later, plot, 1.0, false, view);
    }
    require(twin.state_checksum() == instance.state_checksum(), "the same ticks give the same state bytes");

    EffectView<SweepScope> tiny = view;
    tiny.plot = Size{3.0, 3.0};
    RecordingPainter nothing;
    instance.paint(nothing, Rect{0.0, 0.0, 3.0, 3.0}, 1.0, false, tiny);
    require(nothing.lines.empty(), "a plot too small for the margins draws nothing");
}

void on_the_motion_clock() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("scope.root"));
    std::shared_ptr<SweepScope> scope = make_control<SweepScope>(StableId("scope.clocked"));
    (*scope).set_requested_bounds({0.0, 0.0, 220.0, 120.0});
    (*scope).value = 60.0;
    (*root).add_child(scope);
    Window window(root, {240.0, 140.0});
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    require(!clock.control_ambient(*scope), "a scope not yet drawn is not on the motion clock");
    RecordingPainter first;
    (*scope).on_paint(first, {0.0, 0.0, 220.0, 120.0});
    require((*scope).effect_instance().entry() == &scope_sweep(), "the drawn scope hosts the sweep");
    require(clock.control_ambient(*scope) && window.next_wake().has_value(), "the drawn scope is enlisted");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    RecordingPainter second;
    (*scope).on_paint(second, {0.0, 0.0, 220.0, 120.0});
    require((*scope).effect_instance().counters().ticks > 0U, "a clock frame makes ticks due");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require(!clock.control_ambient(*scope) && !window.next_wake().has_value(),
            "a scope not painted between frames is dropped, and the window rests");
    RecordingPainter again;
    (*scope).on_paint(again, {0.0, 0.0, 220.0, 120.0});
    require(clock.control_ambient(*scope), "its next paint enlists it again");

    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require(!clock.control_ambient(*scope) && (*scope).states().reduced_motion, "reduced motion stops the sweep");
    const std::uint64_t ticks = (*scope).effect_instance().counters().ticks;
    RecordingPainter still;
    (*scope).on_paint(still, {0.0, 0.0, 220.0, 120.0});
    require(!clock.control_ambient(*scope) && (*scope).effect_instance().counters().ticks == ticks,
            "under reduced motion the scope draws the still pose and stays off the clock");
    settings.reduced_motion = false;
    settings.ambient_motion = false;
    window.set_presentation_settings(settings);
    RecordingPainter switched_off;
    (*scope).on_paint(switched_off, {0.0, 0.0, 220.0, 120.0});
    require(!clock.control_ambient(*scope), "the ambient switch keeps the sweep still");
    settings.ambient_motion = true;
    window.set_presentation_settings(settings);
    RecordingPainter switched_on;
    (*scope).on_paint(switched_on, {0.0, 0.0, 220.0, 120.0});
    require(clock.control_ambient(*scope), "with the switch on the next paint enlists it again");
    const Control::Ptr detached = (*root).remove_child((*scope).runtime_id());
    require(detached != nullptr && clock.ambient_count() == 0U, "a scope leaving its window withdraws from its clock");
}

} // namespace

int main() {
    properties();
    anatomy_and_semantics();
    effect_draws();
    on_the_motion_clock();
    std::cout << "sweep scope: properties, anatomy and semantics, the sweep effect, the motion clock\n";
    return 0;
}
