// Splitter (stage W, owner decision 2 of 2026-10-25): the Windows Forms
// migration facade over the dock layout. The target found by adjacency at
// each edge, dragging with live resizing between min_size and min_extra,
// splitter_moving adjusting a move, splitter_moved, programmatic and
// semantic positions, a splitter without a target, anatomy and painting.
// On the drag engine (roadmap 2.1, step M1): a parity trace of the pointer
// script against the behaviour before the migration, cancels that restore
// (Escape, a lost capture, deactivation) and the keyboard drag through the
// begin_keyboard_drag rule action.

#include "gui_forms/gui_forms.hpp"
#include "../src/core/drag_engine/drag_engine.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 0.01;
    return result;
}

class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void fill_rounded_rect(Rect, double, Color) override { ++fills; }

    int fills{};
};

// Records moves; with a snap, rounds each proposed position to it.
struct Recorder final : public Component {
    std::vector<double> moving;
    std::vector<double> moved;
    double snap{};

    void on_moving(SplitterMoveEvent& move) {
        if (snap > 0.0) move.split_position = std::round(move.split_position / snap) * snap;
        moving.push_back(move.split_position);
    }
    void on_moved(const SplitterMoveEvent& move) { moved.push_back(move.split_position); }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Panel> pane;
    std::shared_ptr<Splitter> splitter;
    std::shared_ptr<Panel> rest;
    std::unique_ptr<Window> window;
};

// A 400 by 200 root: `pane` docked at `edge` with extent 100, the splitter
// (4 across) docked after it, and a fill.
Fixture make_fixture(DockStyle edge) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("split.root"));
    fixture.pane = make_control<Panel>(StableId("split.pane"));
    (*fixture.pane).dock = edge;
    (*fixture.pane).set_requested_bounds({0.0, 0.0, 100.0, 100.0});
    fixture.splitter = make_control<Splitter>(StableId("split.splitter"));
    (*fixture.splitter).dock = edge;
    (*fixture.splitter).set_requested_bounds({0.0, 0.0, 4.0, 4.0});
    fixture.rest = make_control<Panel>(StableId("split.rest"));
    (*fixture.rest).dock = DockStyle::fill;
    (*fixture.root).add_child(fixture.pane);
    (*fixture.root).add_child(fixture.splitter);
    (*fixture.root).add_child(fixture.rest);
    fixture.window = std::make_unique<Window>(fixture.root, Size{400.0, 200.0});
    (*fixture.window).perform_layout();
    return fixture;
}

Point centre(const Control& control) {
    const Rect bounds = control.absolute_bounds();
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

void pointer(Window& window, PointerAction action, Point point) {
    const PointerButton button = action == PointerAction::move ? PointerButton::none : PointerButton::primary;
    static_cast<void>(window.dispatch_pointer({action, button, point}));
    window.perform_layout();
}

template <typename Action>
bool throws_invalid(Action action) {
    try {
        action();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

struct SetMinSize final {
    Splitter* splitter{};
    double value{};
    void operator()() const { (*splitter).min_size = value; }
};

struct SetPosition final {
    Splitter* splitter{};
    double value{};
    void operator()() const { (*splitter).set_split_position(value); }
};

void left_drag() {
    Fixture fixture = make_fixture(DockStyle::left);
    Window& window = *fixture.window;
    Splitter& splitter = *fixture.splitter;
    Recorder recorder;
    on(splitter.splitter_moving(), recorder, &Recorder::on_moving);
    on(splitter.splitter_moved(), recorder, &Recorder::on_moved);

    require(splitter.target() == fixture.pane && splitter.split_position() && near(*splitter.split_position(), 100.0),
            "a left splitter's target is the left-docked sibling before it");
    const ControlAnatomy& anatomy = splitter.anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "splitter",
            "the splitter publishes a valid anatomy");

    const Point start = centre(splitter);
    pointer(window, PointerAction::move, start);
    require(splitter.states().hot, "a splitter with a target is hot under the pointer");
    pointer(window, PointerAction::down, start);
    require(splitter.states().pressed && splitter.states().captured, "a press on the bar starts a drag");
    pointer(window, PointerAction::move, {start.x + 50.0, start.y});
    require(near((*fixture.pane).committed_arranged_bounds().width, 150.0) && recorder.moving.size() == 1U &&
                near(recorder.moving.back(), 150.0),
            "dragging right widens the target live and raises splitter_moving");
    require(near(splitter.committed_arranged_bounds().x, 150.0), "the splitter follows its target's edge");
    pointer(window, PointerAction::move, {start.x + 1000.0, start.y});
    require(near(*splitter.split_position(), 400.0 - 4.0 - 25.0), "min_extra leaves room for the fill");
    pointer(window, PointerAction::move, {start.x - 1000.0, start.y});
    require(near(*splitter.split_position(), 25.0), "min_size bounds the target");
    pointer(window, PointerAction::up, {start.x + 20.0, start.y});
    require(recorder.moved.size() == 1U && near(recorder.moved.back(), 120.0) &&
                near(*splitter.split_position(), 120.0) && !splitter.states().pressed,
            "release ends the drag and raises splitter_moved with the target's extent");

    recorder.snap = 50.0;
    const Point again = centre(splitter);
    pointer(window, PointerAction::down, again);
    pointer(window, PointerAction::move, {again.x + 22.0, again.y});
    require(near(*splitter.split_position(), 150.0), "a splitter_moving handler may change the position");
    pointer(window, PointerAction::up, {again.x + 22.0, again.y});

    splitter.set_split_position(200.0);
    window.perform_layout();
    require(near(*splitter.split_position(), 200.0), "set_split_position resizes the target");
    splitter.set_split_position(5.0);
    window.perform_layout();
    require(near(*splitter.split_position(), 25.0), "set_split_position is clamped");
    require(throws_invalid(SetMinSize{&splitter, -1.0}) && throws_invalid(SetPosition{&splitter, std::nan("")}),
            "invalid limits and positions are refused");

    const SemanticDescriptor descriptor = splitter.semantic_descriptor();
    require(descriptor.role == SemanticRole::separator && descriptor.numeric_value && near(*descriptor.numeric_value, 25.0) &&
                descriptor.maximum_value && near(*descriptor.maximum_value, 371.0),
            "the splitter publishes a separator with its position and limits");
    require(splitter.on_semantic_action(SemanticAction::set_value, "90"), "set_value is taken");
    window.perform_layout();
    require(near(*splitter.split_position(), 90.0), "set_value moves the splitter");
    require(splitter.on_semantic_action(SemanticAction::increment, {}), "increment is taken");
    window.perform_layout();
    require(near(*splitter.split_position(), 100.0), "increment moves by its step");
    require(!splitter.on_semantic_action(SemanticAction::set_value, "wide"), "a value that is not a number is refused");

    CountingPainter painter;
    window.paint(painter, {0.0, 0.0, 400.0, 200.0});
    require(painter.fills > 0, "the bar paints its face");
}

void other_edges() {
    Fixture right = make_fixture(DockStyle::right);
    require((*right.splitter).target() == right.pane, "a right splitter's target is the right-docked sibling before it");
    const Point start = centre(*right.splitter);
    pointer(*right.window, PointerAction::down, start);
    pointer(*right.window, PointerAction::move, {start.x - 30.0, start.y});
    pointer(*right.window, PointerAction::up, {start.x - 30.0, start.y});
    require(near((*right.pane).committed_arranged_bounds().width, 130.0), "dragging a right splitter left widens its target");

    Fixture top = make_fixture(DockStyle::top);
    require((*top.splitter).target() == top.pane, "a top splitter's target is the top-docked sibling before it");
    const Point top_start = centre(*top.splitter);
    pointer(*top.window, PointerAction::down, top_start);
    pointer(*top.window, PointerAction::move, {top_start.x, top_start.y + 500.0});
    pointer(*top.window, PointerAction::up, {top_start.x, top_start.y + 500.0});
    require(near((*top.pane).committed_arranged_bounds().height, 200.0 - 4.0 - 25.0),
            "a top splitter resizes its target's height within min_extra");

    Fixture bottom = make_fixture(DockStyle::bottom);
    require((*bottom.splitter).target() == bottom.pane, "a bottom splitter's target is the bottom-docked sibling");
}

void without_a_target() {
    Fixture fixture = make_fixture(DockStyle::left);
    Splitter& splitter = *fixture.splitter;
    splitter.dock = DockStyle::none;
    (*fixture.window).perform_layout();
    require(splitter.target() == nullptr && !splitter.split_position(), "an undocked splitter has no target");
    splitter.set_split_position(150.0);
    (*fixture.window).perform_layout();
    require(near((*fixture.pane).committed_arranged_bounds().width, 100.0), "a splitter without a target moves nothing");
    pointer(*fixture.window, PointerAction::move, centre(splitter));
    require(!splitter.states().hot, "a splitter without a target is not hot under the pointer");
    pointer(*fixture.window, PointerAction::down, centre(splitter));
    require(!splitter.states().pressed, "a press without a target starts no drag");
    pointer(*fixture.window, PointerAction::up, centre(splitter));

    Fixture hidden = make_fixture(DockStyle::left);
    (*hidden.pane).visible = false;
    (*hidden.window).perform_layout();
    require((*hidden.splitter).target() == nullptr, "a hidden sibling is not a target");
}


// -- the drag engine (study step M1) --------------------------------------------

KeyEvent key_down(std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical_key;
    event.modifiers = modifiers;
    return event;
}

// One line of the parity trace: the step, the target's extent, the counts of
// splitter_moving and splitter_moved, and whether the bar shows pressed.
void trace_step(std::string& trace, const char* step, double x, const Fixture& fixture, const Recorder& recorder) {
    std::array<char, 128> line{};
    const long position = std::lround(x);
    const long extent = std::lround((*fixture.pane).committed_arranged_bounds().width);
    const char* const pressed = (*fixture.splitter).states().pressed ? " pressed" : "";
    const int written = std::snprintf(line.data(), line.size(), "%s %ld extent=%ld moving=%zu moved=%zu%s\n", step,
                                      position, extent, recorder.moving.size(), recorder.moved.size(), pressed);
    if (written > 0) trace.append(line.data(), static_cast<std::size_t>(written));
}

// The pointer script of left_drag, traced on the engine. The golden text is
// the behaviour before the migration, as left_drag's assertions record it
// (a press starts the drag at once, moves resize live within the limits,
// the release raises splitter_moved with the projected extent).
void parity_trace() {
    Fixture fixture = make_fixture(DockStyle::left);
    Window& window = *fixture.window;
    Recorder recorder;
    on((*fixture.splitter).splitter_moving(), recorder, &Recorder::on_moving);
    on((*fixture.splitter).splitter_moved(), recorder, &Recorder::on_moved);
    const Point start = centre(*fixture.splitter);
    std::string trace;
    pointer(window, PointerAction::move, start);
    trace_step(trace, "move", start.x, fixture, recorder);
    pointer(window, PointerAction::down, start);
    trace_step(trace, "down", start.x, fixture, recorder);
    const std::array<double, 3> script{start.x + 50.0, start.x + 1000.0, start.x - 1000.0};
    for (const double x : script) {
        pointer(window, PointerAction::move, {x, start.y});
        trace_step(trace, "move", x, fixture, recorder);
    }
    pointer(window, PointerAction::up, {start.x + 20.0, start.y});
    trace_step(trace, "up", start.x + 20.0, fixture, recorder);
    const std::string expected =
        "move 102 extent=100 moving=0 moved=0\n"
        "down 102 extent=100 moving=0 moved=0 pressed\n"
        "move 152 extent=150 moving=1 moved=0 pressed\n"
        "move 1102 extent=371 moving=2 moved=0 pressed\n"
        "move -898 extent=25 moving=3 moved=0 pressed\n"
        "up 122 extent=120 moving=3 moved=1\n";
    if (trace != expected) std::cerr << trace;
    require(trace == expected, "the engine-driven drag matches the behaviour before the migration");
    const DragStatistics& statistics = window.drag_engine().statistics();
    require(statistics.ends[static_cast<std::size_t>(DragEnd::free)] == 1U && statistics.storage_growths == 0U,
            "the drag ended free on the engine, with no storage growth");
}

void cancels_restore() {
    Fixture fixture = make_fixture(DockStyle::left);
    Window& window = *fixture.window;
    Splitter& splitter = *fixture.splitter;
    Recorder recorder;
    on(splitter.splitter_moved(), recorder, &Recorder::on_moved);
    const Point start = centre(splitter);
    pointer(window, PointerAction::down, start);
    pointer(window, PointerAction::move, {start.x + 50.0, start.y});
    require(near(*splitter.split_position(), 150.0), "the drag resizes live");
    require(window.dispatch_key(key_down(PhysicalKey::escape)), "the window gives the drag Escape");
    window.perform_layout();
    require(near(*splitter.split_position(), 100.0) && recorder.moved.empty() && !splitter.states().pressed,
            "Escape puts the target back at its extent before the drag, as Windows Forms does");

    const Point again = centre(splitter);
    pointer(window, PointerAction::down, again);
    pointer(window, PointerAction::move, {again.x + 30.0, again.y});
    require(near(*splitter.split_position(), 130.0), "a second drag resizes live");
    splitter.set_pointer_capture(false);
    window.perform_layout();
    require(near(*splitter.split_position(), 100.0) && recorder.moved.empty(), "a lost capture restores as well");

    window.set_active(true);
    pointer(window, PointerAction::down, again);
    pointer(window, PointerAction::move, {again.x + 40.0, again.y});
    window.set_active(false);
    window.perform_layout();
    require(near(*splitter.split_position(), 100.0) && window.drag_engine().phase() == DragPhase::idle,
            "the window's deactivation cancels and restores");

    // Escape right after the press changed nothing, so nothing is restored.
    pointer(window, PointerAction::down, again);
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::escape)));
    window.perform_layout();
    require(near(*splitter.split_position(), 100.0), "Escape before any move leaves the extent as it was");
}

void keyboard_drag() {
    Fixture fixture = make_fixture(DockStyle::left);
    Window& window = *fixture.window;
    Splitter& splitter = *fixture.splitter;
    Recorder recorder;
    on(splitter.splitter_moving(), recorder, &Recorder::on_moving);
    on(splitter.splitter_moved(), recorder, &Recorder::on_moved);
    Rule move_key{};
    move_key.trigger = trigger::key_pressed(PhysicalKey::f3);
    move_key.actions.push_back(builtin::begin_keyboard_drag(splitter));
    require(window.rules().add(move_key).refusal == RuleRefusal::none, "a move-key rule is armed");

    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::f3)));
    DragEngine& engine = window.drag_engine();
    require(engine.mode() == DragInputMode::keyboard && splitter.states().pressed,
            "the rule action starts the keyboard drag on a splitter that takes no focus");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::right)));
    window.perform_layout();
    require(near(*splitter.split_position(), 110.0) && recorder.moving.size() == 1U, "Right steps 10 and previews");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::right, Modifier::control)));
    window.perform_layout();
    require(near(*splitter.split_position(), 210.0), "Ctrl takes a coarse step");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::left, Modifier::shift)));
    window.perform_layout();
    require(near(*splitter.split_position(), 209.0), "Shift takes a fine step");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::up)));
    window.perform_layout();
    require(near(*splitter.split_position(), 209.0), "an arrow across the axis moves nothing");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::enter)));
    window.perform_layout();
    require(recorder.moved.size() == 1U && near(recorder.moved.back(), 209.0) && !splitter.states().pressed,
            "Enter commits and raises splitter_moved");
    require(engine.announcement() == "Split position 209, done", "the end is announced");

    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::f3)));
    for (int step = 0; step < 30; ++step) static_cast<void>(window.dispatch_key(key_down(PhysicalKey::right)));
    window.perform_layout();
    require(near(*splitter.split_position(), 371.0), "the keyboard reaches the limit the pointer reaches");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::escape)));
    window.perform_layout();
    require(near(*splitter.split_position(), 209.0) && engine.announcement() == "Split position 209, restored",
            "Escape restores and says so");

    Fixture right = make_fixture(DockStyle::right);
    Rule right_key{};
    right_key.trigger = trigger::key_pressed(PhysicalKey::f3);
    right_key.actions.push_back(builtin::begin_keyboard_drag(*right.splitter));
    require((*right.window).rules().add(right_key).refusal == RuleRefusal::none, "a right splitter's rule is armed");
    static_cast<void>((*right.window).dispatch_key(key_down(PhysicalKey::f3)));
    static_cast<void>((*right.window).dispatch_key(key_down(PhysicalKey::right)));
    static_cast<void>((*right.window).dispatch_key(key_down(PhysicalKey::enter)));
    (*right.window).perform_layout();
    require(near((*right.pane).committed_arranged_bounds().width, 90.0),
            "Right moves a right splitter's bar right, narrowing its target");
}

} // namespace

int main() {
    left_drag();
    other_edges();
    without_a_target();
    parity_trace();
    cancels_restore();
    keyboard_drag();
    std::cout << "Splitter: targets at every edge, live drags within the limits, splitter_moving adjustment, "
                 "splitter_moved, programmatic and semantic positions, no target, anatomy and paint, the engine parity "
                 "trace, restoring cancels and the keyboard drag passed\n";
    return 0;
}
