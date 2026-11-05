#include "gui_forms/container_controls.hpp"
#include "gui_forms/range_controls.hpp"
#include "gui_forms/window.hpp"
#include "support/named_callbacks.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class RecordingPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { ++restores; }
    void translate(Point) override {}
    void clip_rect(Rect) override { ++clips; }
    void fill_rect(Rect, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override { ++strokes; }
    void draw_line(Point, Point, Color, double) override { ++lines; }
    void clip_rounded_rect(Rect, double) override { ++clips; }
    void fill_rounded_rect(Rect, double, Color) override { ++fills; }
    void stroke_rounded_rect(Rect, double, Color, double) override { ++strokes; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    std::uint64_t saves{};
    std::uint64_t restores{};
    std::uint64_t clips{};
    std::uint64_t fills{};
    std::uint64_t strokes{};
    std::uint64_t lines{};
};

class ObserveSmallIncrement final {
public:
    explicit ObserveSmallIncrement(std::string& order) : order_(order) {}

    void operator()(const RangeScrollEvent& event) const {
        require(event.action == RangeAction::small_increment,
                "right key must report small increment");
        order_ += "scroll\n";
    }

private:
    std::string& order_;
};

class RecordRangeAction final {
public:
    explicit RecordRangeAction(std::vector<RangeAction>& actions)
        : actions_(actions) {}

    void operator()(const RangeScrollEvent& event) const {
        actions_.push_back(event.action);
    }

private:
    std::vector<RangeAction>& actions_;
};

class SetProgressOffThread final {
public:
    SetProgressOffThread(ProgressBar& progress, bool& rejected)
        : progress_(progress), rejected_(rejected) {}

    void operator()() const {
        try {
            progress_.set_value(50.0);
        } catch (const std::logic_error&) {
            rejected_ = true;
        }
    }

private:
    ProgressBar& progress_;
    bool& rejected_;
};

void test_container_focus_identity() {
    std::shared_ptr<gui_forms::ContainerControl> root = make_control<ContainerControl>(StableId("container.root"));
    std::shared_ptr<gui_forms::UserControl> nested = make_control<UserControl>(StableId("container.user"));
    std::shared_ptr<gui_forms::Button> first = make_control<Button>(StableId("container.first"), "First");
    std::shared_ptr<gui_forms::Button> second = make_control<Button>(StableId("container.second"), "Second");
    (*first).set_requested_bounds({0.0, 0.0, 80.0, 24.0});
    (*second).set_requested_bounds({0.0, 28.0, 80.0, 24.0});
    (*nested).add_child(second);
    (*root).add_child(first);
    (*root).add_child(nested);
    Window window(root, {120.0, 80.0});

    require((*root).contains_descendant(second) && (*nested).contains_descendant(second) &&
                !(*nested).contains_descendant(first),
            "container descendant identity must follow the logical retained tree");
    require((*root).request_active_control(second) &&
                (*root).active_control() == second &&
                (*nested).active_control() == second,
            "container must request and query a focused nested descendant");
    require((*nested).clear_active_control() && !window.focused_control() &&
                !(*root).active_control(),
            "nested container must clear focus only when it owns the active descendant");

    std::shared_ptr<gui_forms::Button> foreign = make_control<Button>(StableId("container.foreign"), "Foreign");
    require(!(*root).request_active_control(foreign),
            "container must reject detached or foreign controls");
}

void test_range_validation_and_event_order() {
    std::shared_ptr<gui_forms::TrackBar> slider = make_control<TrackBar>(StableId("range.slider"));
    (*slider).set_range(0.0, 100.0);
    (*slider).set_value(50.0);
    (*slider).set_requested_bounds({0.0, 0.0, 200.0, 28.0});
    Window window(slider, {200.0, 28.0});
    require(window.request_focus(slider), "range test slider must accept focus");

    std::string order;
    SubscriptionToken range = (*slider).range_changed().subscribe(
        test_support::AppendLiteral<double, double>(order, "range\n"));
    SubscriptionToken scroll = (*slider).scroll().subscribe(
        ObserveSmallIncrement(order));
    SubscriptionToken value = (*slider).value_changed().subscribe(
        test_support::AppendLiteral<double>(order, "value\n"));

    KeyEvent right;
    right.action = KeyAction::down;
    right.physical_key = PhysicalKey::right;
    require(window.dispatch_key(right) && (*slider).value() == 51.0 &&
                order == "scroll\nvalue\n",
            "input range mutation must publish scroll before value_changed");

    order.clear();
    (*slider).set_value(75.0);
    require(order == "value\n",
            "programmatic range mutation must publish value without a scroll event");

    order.clear();
    (*slider).set_range(80.0, 120.0);
    require((*slider).value() == 80.0 && order == "range\nvalue\n",
            "range boundary mutation must publish range then a clamped value");

    bool invalid_range = false;
    bool invalid_value = false;
    bool invalid_change = false;
    try {
        (*slider).set_range(10.0, 10.0);
    } catch (const std::invalid_argument&) {
        invalid_range = true;
    }
    try {
        (*slider).set_value(200.0);
    } catch (const std::out_of_range&) {
        invalid_value = true;
    }
    try {
        (*slider).set_small_change(0.0);
    } catch (const std::invalid_argument&) {
        invalid_change = true;
    }
    require(invalid_range && invalid_value && invalid_change,
            "range substrate must reject degenerate ranges, overflow, and nonpositive steps");
}

void test_pointer_capture_keyboard_and_disabled_state() {
    std::shared_ptr<gui_forms::TrackBar> slider = make_control<TrackBar>(StableId("capture.slider"));
    (*slider).set_requested_bounds({10.0, 10.0, 200.0, 28.0});
    (*slider).set_value(25.0);
    Window window(slider, {240.0, 60.0});

    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {110.0, 24.0};
    down.pointer_id = 7;
    require(window.dispatch_pointer(down) && window.captured_control() == slider,
            "track pointer down must enter retained capture");
    PointerEvent move;
    move.action = PointerAction::move;
    move.position = {500.0, 24.0};
    move.pointer_id = 7;
    require(window.dispatch_pointer(move) && (*slider).value() == 100.0,
            "captured track drag must clamp outside movement to maximum");
    PointerEvent up = move;
    up.action = PointerAction::up;
    up.button = PointerButton::primary;
    require(window.dispatch_pointer(up) && !window.captured_control(),
            "track pointer up must deterministically release capture");

    KeyEvent home;
    home.action = KeyAction::down;
    home.physical_key = PhysicalKey::home;
    require(window.dispatch_key(home) && (*slider).value() == 0.0,
            "Home must select the first range boundary");
    KeyEvent page_up = home;
    page_up.physical_key = PhysicalKey::page_up;
    require(window.dispatch_key(page_up) && (*slider).value() == 10.0,
            "Page Up must apply the declared large change");

    (*slider).enabled = false;
    const double disabled_value = (*slider).value();
    require(!window.dispatch_key(page_up) && (*slider).value() == disabled_value,
            "disabled range controls must reject keyboard mutation");
}

void test_progress_rendering_and_vertical_geometry() {
    std::shared_ptr<gui_forms::Panel> panel = make_control<Panel>(StableId("range.panel"));
    (*panel).set_requested_bounds({0.0, 0.0, 240.0, 180.0});
    std::shared_ptr<gui_forms::ProgressBar> horizontal = make_control<ProgressBar>(StableId("range.progress.horizontal"));
    (*horizontal).set_requested_bounds({10.0, 10.0, 200.0, 20.0});
    (*horizontal).set_value(50.0);
    std::shared_ptr<gui_forms::ProgressBar> vertical = make_control<ProgressBar>(StableId("range.progress.vertical"));
    (*vertical).set_orientation(Orientation::vertical);
    (*vertical).set_requested_bounds({10.0, 40.0, 20.0, 120.0});
    (*vertical).set_value(75.0);
    std::shared_ptr<gui_forms::TrackBar> track = make_control<TrackBar>(StableId("range.track.vertical"));
    (*track).set_orientation(Orientation::vertical);
    (*track).set_requested_bounds({50.0, 40.0, 28.0, 120.0});
    (*track).set_value(75.0);
    (*panel).add_child(horizontal);
    (*panel).add_child(vertical);
    (*panel).add_child(track);
    Window window(panel, {240.0, 180.0});
    window.perform_layout();

    RecordingPainter painter;
    window.paint(painter, {0.0, 0.0, 240.0, 180.0});
    require(painter.saves == painter.restores && painter.clips >= 4 &&
                painter.fills >= 4 && painter.strokes + painter.lines >= 4,
            "horizontal and vertical ranges must render retained painter commands");
    const Rect progress_bounds = (*horizontal).absolute_bounds();
    const Point progress_center{progress_bounds.x + progress_bounds.width * 0.5,
                                progress_bounds.y + progress_bounds.height * 0.5};
    require(window.hit_test(progress_center) == panel,
            "noninteractive ProgressBar must not intercept its container");
}

void test_scrollbar_geometry_capture_repeat_and_orientation() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("scroll.root"));
    std::shared_ptr<gui_forms::HScrollBar> horizontal = make_control<HScrollBar>(StableId("scroll.horizontal"));
    (*horizontal).accessible_name = "Horizontal viewport";
    (*horizontal).set_requested_bounds({10.0, 10.0, 240.0, 18.0});
    (*horizontal).set_range(0.0, 100.0);
    (*horizontal).set_small_change(2.0);
    (*horizontal).set_large_change(20.0);
    (*horizontal).set_value(50.0);
    std::shared_ptr<gui_forms::VScrollBar> vertical = make_control<VScrollBar>(StableId("scroll.vertical"));
    (*vertical).set_requested_bounds({260.0, 10.0, 18.0, 180.0});
    (*vertical).set_range(0.0, 200.0);
    (*vertical).set_value(40.0);
    (*root).add_child(horizontal);
    (*root).add_child(vertical);
    Window window(root, {300.0, 210.0});
    window.perform_layout();

    const Rect track = (*horizontal).track_bounds();
    const Rect thumb = (*horizontal).thumb_bounds();
    require(track.width > thumb.width && thumb.width >= 18.0 &&
                thumb.x > track.x &&
                (*vertical).thumb_bounds().height >= 18.0,
            "scrollbar thumbs must be proportional, bounded, and orientation-aware");

    std::vector<RangeAction> actions;
    SubscriptionToken scroll = (*horizontal).scroll().subscribe(
        RecordRangeAction(actions));
    const Rect absolute = (*horizontal).absolute_bounds();
    const Rect decrement = (*horizontal).decrement_button_bounds();
    Point decrement_point{absolute.x + decrement.x + decrement.width * 0.5,
                          absolute.y + decrement.y + decrement.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     decrement_point}) &&
                (*horizontal).value() == 48.0 &&
                window.captured_control() == horizontal &&
                window.next_wake().has_value(),
            "scrollbar arrow press must step, capture, and schedule bounded repeat");
    const FrameTime repeat_deadline = *window.next_wake();
    static_cast<void>(window.poll_frame_schedule(repeat_deadline));
    require((*horizontal).value() == 46.0 && actions.size() == 2U &&
                actions[0] == RangeAction::small_decrement &&
                actions[1] == RangeAction::small_decrement,
            "scrollbar hold deadline must repeat the declared small action");
    require(window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                     decrement_point}) &&
                !window.captured_control() && !window.next_wake(),
            "scrollbar release must cancel capture and repeat without residue");

    const Rect current_thumb = (*horizontal).thumb_bounds();
    const double page_x = std::min(track.x + track.width - 1.0,
                                   current_thumb.x + current_thumb.width + 8.0);
    Point page_point{absolute.x + page_x, absolute.y + track.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     page_point}) &&
                (*horizontal).value() == 66.0,
            "scrollbar track press must apply the declared large increment");
    require(window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                     page_point}),
            "scrollbar page action must end cleanly");

    const Rect drag_thumb = (*horizontal).thumb_bounds();
    const Point drag_start{absolute.x + drag_thumb.x + drag_thumb.width * 0.5,
                           absolute.y + drag_thumb.height * 0.5};
    const Point drag_end{absolute.x + track.x + track.width - 2.0,
                         drag_start.y};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     drag_start}) &&
                window.dispatch_pointer({PointerAction::move, PointerButton::none,
                                         drag_end}) &&
                (*horizontal).value() > 95.0 &&
                actions.back() == RangeAction::thumb_track &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                         drag_end}),
            "scrollbar thumb must track continuously under retained capture");

    const Rect vertical_absolute = (*vertical).absolute_bounds();
    const Rect vertical_increment = (*vertical).increment_button_bounds();
    const Point vertical_increment_point{
        vertical_absolute.x + vertical_increment.width * 0.5,
        vertical_absolute.y + vertical_increment.y + vertical_increment.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     vertical_increment_point}) &&
                (*vertical).value() == 41.0 &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                         vertical_increment_point}),
            "vertical scrollbar lower arrow must increment the same retained model");
}

void test_disposal_and_ui_thread_guard() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("range.dispose.root"));
    std::shared_ptr<gui_forms::TrackBar> slider = make_control<TrackBar>(StableId("range.dispose.slider"));
    (*slider).set_requested_bounds({0.0, 0.0, 180.0, 28.0});
    (*root).add_child(slider);
    Window window(root, {180.0, 28.0});
    require(window.request_focus(slider), "disposal slider must receive focus");
    std::uint64_t later_values = 0;
    SubscriptionToken dispose_on_scroll = (*slider).scroll().subscribe(
        test_support::DisposeCapturedControl<TrackBar,
                                             const RangeScrollEvent&>(slider));
    SubscriptionToken value = (*slider).value_changed().subscribe(
        test_support::IncrementCounter<std::uint64_t, double>(later_values));
    KeyEvent right;
    right.action = KeyAction::down;
    right.physical_key = PhysicalKey::right;
    require(window.dispatch_key(right) && !(*slider).is_alive() && later_values == 0,
            "range disposal during scroll must suppress later value callbacks");

    std::shared_ptr<gui_forms::ProgressBar> progress = make_control<ProgressBar>(StableId("range.thread.progress"));
    Window thread_window(progress, {100.0, 20.0});
    bool rejected = false;
    std::thread worker(SetProgressOffThread(*progress, rejected));
    worker.join();
    require(rejected && (*progress).value() == 0.0,
            "range properties must preserve the core UI-thread guard");
}

} // namespace

int main() {
    try {
        test_container_focus_identity();
        test_range_validation_and_event_order();
        test_pointer_capture_keyboard_and_disabled_state();
        test_progress_rendering_and_vertical_geometry();
        test_scrollbar_geometry_capture_repeat_and_orientation();
        test_disposal_and_ui_thread_guard();
        std::cout << "gui_forms_range_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_range_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
