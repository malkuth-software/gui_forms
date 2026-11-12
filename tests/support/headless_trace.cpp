#include "support/headless_trace.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <sstream>
#include <utility>

namespace gui_forms::tests {
namespace {

std::string_view pointer_action_name(PointerAction action) {
    switch (action) {
    case PointerAction::move: return "move";
    case PointerAction::down: return "down";
    case PointerAction::up: return "up";
    case PointerAction::wheel: return "wheel";
    case PointerAction::enter: return "enter";
    case PointerAction::leave: return "leave";
    }
    return "unknown";
}

std::string_view phase_name(EventPhase phase) {
    switch (phase) {
    case EventPhase::preview: return "preview";
    case EventPhase::target: return "target";
    case EventPhase::bubble: return "bubble";
    }
    return "unknown";
}

class TracePainter final : public Painter {
public:
    explicit TracePainter(TraceRecorder& trace) : trace_(trace) {}

    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++draws_; }
    void stroke_rect(Rect, Color, double) override { ++draws_; }
    void draw_line(Point, Point, Color, double) override { ++draws_; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++draws_; }
    void draw_image(ImageId, Rect, double) override { ++draws_; }

    void finish() { trace_.record("paint draws=" + std::to_string(draws_)); }

private:
    TraceRecorder& trace_;
    std::uint64_t draws_{};
};

class TraceControl final : public Control {
public:
    TraceControl(StableId id, TraceRecorder& trace, FakeMonotonicClock& clock)
        : Control(std::move(id)), trace_(trace), clock_(clock) {}

    void on_paint(Painter& painter, Rect damage) override {
        painter.fill_rect(damage, Color::rgba(1, 2, 3));
    }
    void on_pointer_preview(PointerEvent& event) override { record_pointer(event); }
    void on_pointer_bubble(PointerEvent& event) override { record_pointer(event); }
    void on_key_preview(KeyEvent& event) override { record_key(event); }
    void on_key_bubble(KeyEvent& event) override { record_key(event); }
    void on_text_input(TextInputEvent& event) override {
        trace_.record("t=" + std::to_string(clock_.now_nanoseconds()) + " text id=" +
                      std::string(stable_id().value()) + " value=" + event.text_utf8);
    }
    void on_focus_changed(bool focused) override {
        trace_.record("t=" + std::to_string(clock_.now_nanoseconds()) + " focus id=" +
                      std::string(stable_id().value()) + " state=" +
                      (focused ? "on" : "off"));
    }
    void on_activate() override {
        trace_.record("t=" + std::to_string(clock_.now_nanoseconds()) + " activate id=" +
                      std::string(stable_id().value()));
    }

protected:
    // The target phase: the mouse and key raisers, recorded in the routed
    // form's line before each raises its event.
    void on_mouse_down(MouseEventArgs& arguments) override {
        record_pointer(pointer_event_of(arguments));
        Control::on_mouse_down(arguments);
    }
    void on_mouse_up(MouseEventArgs& arguments) override {
        record_pointer(pointer_event_of(arguments));
        Control::on_mouse_up(arguments);
    }
    void on_mouse_move(MouseEventArgs& arguments) override {
        record_pointer(pointer_event_of(arguments));
        Control::on_mouse_move(arguments);
    }
    void on_mouse_wheel(MouseEventArgs& arguments) override {
        record_pointer(pointer_event_of(arguments));
        Control::on_mouse_wheel(arguments);
    }
    void on_key_down(KeyEventArgs& arguments) override {
        record_key(key_event_of(arguments, KeyAction::down));
        Control::on_key_down(arguments);
    }
    void on_key_up(KeyEventArgs& arguments) override {
        record_key(key_event_of(arguments, KeyAction::up));
        Control::on_key_up(arguments);
    }

private:
    void record_pointer(const PointerEvent& event) {
        trace_.record("t=" + std::to_string(clock_.now_nanoseconds()) + " pointer id=" +
                      std::string(stable_id().value()) + " phase=" +
                      std::string(phase_name(event.phase)) + " action=" +
                      std::string(pointer_action_name(event.action)));
    }
    void record_key(const KeyEvent& event) {
        trace_.record("t=" + std::to_string(clock_.now_nanoseconds()) + " key id=" +
                      std::string(stable_id().value()) + " phase=" +
                      std::string(phase_name(event.phase)) + " code=" +
                      std::to_string(event.physical_key));
    }

    TraceRecorder& trace_;
    FakeMonotonicClock& clock_;
};

std::shared_ptr<TraceControl> make_trace_control(
    std::string id, TraceRecorder& trace, FakeMonotonicClock& clock) {
    return make_control<TraceControl>(
        StableId(std::move(id)), trace, clock);
}

} // namespace

void TraceRecorder::record(std::string line) {
    lines_.push_back(std::move(line));
}

std::string TraceRecorder::text() const {
    std::ostringstream output;
    for (const std::string& line : lines_) {
        output << line << '\n';
    }
    return output.str();
}

std::string canonical_lifecycle_trace() {
    FakeMonotonicClock clock;
    TraceRecorder trace;
    std::shared_ptr<TraceControl> root =
        make_trace_control("trace.root", trace, clock);
    std::shared_ptr<TraceControl> left =
        make_trace_control("trace.left", trace, clock);
    std::shared_ptr<TraceControl> right =
        make_trace_control("trace.right", trace, clock);
    std::shared_ptr<TraceControl> target =
        make_trace_control("trace.target", trace, clock);
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    (*left).set_requested_bounds({0.0, 0.0, 55.0, 80.0});
    (*right).set_requested_bounds({80.0, 0.0, 55.0, 80.0});
    (*target).set_requested_bounds({5.0, 5.0, 30.0, 20.0});
    (*target).focusable = true;
    (*left).add_child(target);
    (*root).add_child(left);
    (*root).add_child(right);

    Window window(root, {120.0, 80.0});
    trace.record("create controls=" + std::to_string(window.metrics_snapshot().control_count));
    clock.advance(10);
    window.resize({160.0, 90.0});
    window.set_scale(2.0);
    window.perform_layout();
    trace.record("t=10 resize=160x90 scale=2");

    TracePainter painter(trace);
    window.paint(painter);
    painter.finish();
    window.reset_activity_metrics();

    clock.advance(10);
    window.dispatch_pointer({PointerAction::down, PointerButton::primary, {10.0, 10.0}});
    clock.advance(10);
    window.dispatch_pointer({PointerAction::up, PointerButton::primary, {10.0, 10.0}});
    clock.advance(10);
    window.dispatch_key({KeyAction::down, 40});
    window.dispatch_text({"A"});

    clock.advance(10);
    (*right).add_child(target);
    (*target).set_requested_bounds({5.0, 5.0, 30.0, 20.0});
    window.perform_layout();
    trace.record("t=50 reparent id=trace.target parent=trace.right");
    window.request_focus(target);

    clock.advance(10);
    window.dispatch_pointer({PointerAction::down, PointerButton::primary, {90.0, 10.0}});
    (*target).dispose();
    trace.record("t=60 dispose id=trace.target state=disposed");
    window.dispatch_pointer({PointerAction::up, PointerButton::primary, {90.0, 10.0}});

    TracePainter cleanup_painter(trace);
    window.paint(cleanup_painter);
    cleanup_painter.finish();
    const MetricsSnapshot lifecycle = window.metrics_snapshot();
    trace.record("metrics focus=" + std::to_string(lifecycle.focus_transitions) +
                 " activation=" + std::to_string(lifecycle.activations) +
                 " disposal=" + std::to_string(lifecycle.disposals) +
                 " focus_revoke=" + std::to_string(lifecycle.focus_revocations) +
                 " capture_revoke=" + std::to_string(lifecycle.capture_revocations));

    window.reset_activity_metrics();
    clock.advance(1'000);
    const MetricsSnapshot idle = window.metrics_snapshot();
    trace.record("t=1060 idle frame=" + std::to_string(window.needs_frame() ? 1 : 0) +
                 " wake=" + std::to_string(window.next_wake().has_value() ? 1 : 0) +
                 " layout=" + std::to_string(idle.arrange_passes) +
                 " paint=" + std::to_string(idle.paint_passes) +
                 " present=" + std::to_string(idle.frames_presented) +
                 " callback=" + std::to_string(idle.callbacks_emitted));
    return trace.text();
}

} // namespace gui_forms::tests
