// Splitter (stage W, owner decision 2 of 2026-10-25): the Windows Forms
// migration facade over the dock layout. The target found by adjacency at
// each edge, dragging with live resizing between min_size and min_extra,
// splitter_moving adjusting a move, splitter_moved, programmatic and
// semantic positions, a splitter without a target, anatomy and painting.

#include "gui_forms/gui_forms.hpp"

#include <cmath>
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
    void operator()() const { (*splitter).set_min_size(value); }
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

} // namespace

int main() {
    left_drag();
    other_edges();
    without_a_target();
    std::cout << "Splitter: targets at every edge, live drags within the limits, splitter_moving adjustment, "
                 "splitter_moved, programmatic and semantic positions, no target, anatomy and paint passed\n";
    return 0;
}
