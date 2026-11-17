#include "support/normative_trace.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <cmath>
#include <fstream>
#include <cstdio>
#include <sstream>
#include <string_view>
#include <utility>

namespace gui_forms::tests {
namespace {

std::string_view pointer_action_name(const PointerAction action) {
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

std::string_view phase_name(const EventPhase phase) {
    switch (phase) {
    case EventPhase::preview: return "preview";
    case EventPhase::target: return "target";
    case EventPhase::bubble: return "bubble";
    }
    return "unknown";
}

// A length in 1/64 logical px, rounded, as the display trace tests write
// lengths, so a last-bit difference in layout arithmetic between hosts does
// not change the trace.
std::string length_text(const double value) {
    const long long units = std::llround(value * 64.0);
    return std::to_string(units);
}

std::string colour_text(const Color colour) {
    char text[16]{};
    std::snprintf(text, sizeof text, "#%02x%02x%02x%02x", static_cast<unsigned>(colour.red),
                  static_cast<unsigned>(colour.green), static_cast<unsigned>(colour.blue),
                  static_cast<unsigned>(colour.alpha));
    return std::string(text);
}

// FNV-1a over the display trace's bytes.
std::uint64_t trace_digest(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const char character : text) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
        hash *= 1099511628211ULL;
    }
    return hash;
}

// Writes each drawing command as one line, in the display trace tests'
// vocabulary for the commands it records.
class DisplayTracePainter final : public Painter {
public:
    void save() override { lines.push_back("save"); }
    void restore() override { lines.push_back("restore"); }
    void translate(Point offset) override {
        lines.push_back("translate " + length_text(offset.x) + " " + length_text(offset.y));
    }
    void clip_rect(Rect rect) override { lines.push_back("clip_rect " + rectangle_text(rect)); }
    void fill_rect(Rect rect, Color color) override {
        lines.push_back("fill_rect " + rectangle_text(rect) + " " + colour_text(color));
    }
    void stroke_rect(Rect rect, Color color, double width) override {
        lines.push_back("stroke_rect " + rectangle_text(rect) + " " + colour_text(color) + " " + length_text(width));
    }
    void draw_line(Point from, Point to, Color color, double width) override {
        lines.push_back("draw_line " + length_text(from.x) + " " + length_text(from.y) + " " + length_text(to.x) +
                        " " + length_text(to.y) + " " + colour_text(color) + " " + length_text(width));
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color color) override {
        static_cast<void>(font);
        lines.push_back("draw_text " + length_text(origin.x) + " " + length_text(origin.y) + " " +
                        colour_text(color) + " " + std::string(text));
    }
    void draw_image(ImageId image, Rect destination, double opacity) override {
        static_cast<void>(image);
        lines.push_back("draw_image " + rectangle_text(destination) + " " + length_text(opacity));
    }

    std::vector<std::string> lines{};

private:
    static std::string rectangle_text(const Rect rect) {
        const std::string text = length_text(rect.x) + " " + length_text(rect.y) + " " + length_text(rect.width) +
                                 " " + length_text(rect.height);
        return text;
    }
};

// Draws nothing: the headless run's paints after each step, which only
// produce receipts.
class NullPainter final : public Painter {
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

// A focusable control that records its routed input, focus, activation,
// text and paints into the scene, and paints a fill (and a frame while
// focused).
class NormativeControl final : public Control {
public:
    NormativeControl(StableId id, NormativeScene& scene, Color fill)
        : Control(std::move(id)), scene_(scene), fill_(fill) {
        focusable = true;
    }

    void on_paint(Painter& painter, Rect damage) override {
        static_cast<void>(damage);
        if (scene_.receipts_open()) scene_.record_receipt(std::string(stable_id().value()));
        const Rect bounds = absolute_bounds();
        const Rect local{0.0, 0.0, bounds.width, bounds.height};
        painter.fill_rect(local, fill_);
        if (focused_) painter.stroke_rect(local, Color::rgba(0, 0, 0), 2.0);
    }
    void on_pointer_preview(PointerEvent& event) override { record_pointer(event); }
    void on_pointer_bubble(PointerEvent& event) override { record_pointer(event); }
    void on_key_preview(KeyEvent& event) override { record_key(event); }
    void on_key_bubble(KeyEvent& event) override { record_key(event); }
    void on_text_input(TextInputEvent& event) override {
        scene_.record("text id=" + std::string(stable_id().value()) + " value=" + event.text_utf8);
    }
    void on_focus_changed(bool focused) override {
        focused_ = focused;
        scene_.record("focus id=" + std::string(stable_id().value()) + " state=" + (focused ? "on" : "off"));
        invalidate(Dirty::paint);
    }
    void on_activate() override { scene_.record("activate id=" + std::string(stable_id().value())); }

protected:
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
        scene_.record("pointer id=" + std::string(stable_id().value()) + " phase=" +
                      std::string(phase_name(event.phase)) + " action=" +
                      std::string(pointer_action_name(event.action)));
    }
    void record_key(const KeyEvent& event) {
        scene_.record("key id=" + std::string(stable_id().value()) + " phase=" + std::string(phase_name(event.phase)) +
                      " action=" + (event.action == KeyAction::down ? "down" : "up") +
                      " code=" + std::to_string(event.physical_key));
    }

    NormativeScene& scene_;
    Color fill_;
    bool focused_{false};
};

// Dispatches one step straight into the window, as a host's native events
// for it would arrive.
void dispatch_step(Window& window, const NormativeStep& step) {
    switch (step.kind) {
    case NormativeStepKind::pointer_move: {
        PointerEvent move{};
        move.action = PointerAction::move;
        move.position = step.position;
        static_cast<void>(window.dispatch_pointer(move));
        break;
    }
    case NormativeStepKind::click: {
        PointerEvent press{};
        press.action = PointerAction::down;
        press.button = PointerButton::primary;
        press.position = step.position;
        press.pointer_id = 1U;
        static_cast<void>(window.dispatch_pointer(press));
        PointerEvent release = press;
        release.action = PointerAction::up;
        static_cast<void>(window.dispatch_pointer(release));
        break;
    }
    case NormativeStepKind::wheel_down: {
        PointerEvent wheel{};
        wheel.action = PointerAction::wheel;
        wheel.position = step.position;
        wheel.wheel_delta = {0.0, -1.0};
        wheel.wheel_unit = WheelDeltaUnit::lines;
        wheel.pointer_id = 1U;
        static_cast<void>(window.dispatch_pointer(wheel));
        break;
    }
    case NormativeStepKind::key:
    case NormativeStepKind::typed: {
        KeyEvent down{};
        down.action = KeyAction::down;
        down.physical_key = step.physical_key;
        static_cast<void>(window.dispatch_key(down));
        if (step.kind == NormativeStepKind::typed) {
            TextInputEvent text{};
            text.text_utf8 = step.text;
            static_cast<void>(window.dispatch_text(text));
        }
        KeyEvent up = down;
        up.action = KeyAction::up;
        static_cast<void>(window.dispatch_key(up));
        break;
    }
    default:
        break;
    }
}

} // namespace

const std::vector<NormativeStep>& normative_steps() {
    // Window layout: left {10,10,100,60} holding inner at {20,20,60,30}
    // (absolute {30,30,60,30}); right {130,10,100,60}; the root around them.
    static const std::vector<NormativeStep> steps{
        {NormativeStepKind::pointer_move, {50.0, 120.0}, 0U, "", "move over the root"},
        {NormativeStepKind::click, {15.0, 15.0}, 0U, "", "click left"},
        {NormativeStepKind::click, {45.0, 40.0}, 0U, "", "click inner"},
        {NormativeStepKind::key, {}, PhysicalKey::tab, "", "tab"},
        {NormativeStepKind::typed, {}, PhysicalKey::a, "a", "type a"},
        {NormativeStepKind::wheel_down, {180.0, 40.0}, 0U, "", "wheel over right"},
        {NormativeStepKind::click, {180.0, 40.0}, 0U, "", "click right"},
        {NormativeStepKind::pointer_move, {200.0, 140.0}, 0U, "", "move off right"},
    };
    return steps;
}

NormativeScene::NormativeScene() {
    root_ = make_control<NormativeControl>(StableId("normative.root"), *this, Color::rgba(240, 240, 240));
    std::shared_ptr<Control> left =
        make_control<NormativeControl>(StableId("normative.left"), *this, Color::rgba(200, 220, 255));
    std::shared_ptr<Control> inner =
        make_control<NormativeControl>(StableId("normative.inner"), *this, Color::rgba(120, 160, 230));
    std::shared_ptr<Control> right =
        make_control<NormativeControl>(StableId("normative.right"), *this, Color::rgba(255, 220, 200));
    (*root_).set_requested_bounds({0.0, 0.0, normative_window_size.width, normative_window_size.height});
    (*left).set_requested_bounds({10.0, 10.0, 100.0, 60.0});
    (*inner).set_requested_bounds({20.0, 20.0, 60.0, 30.0});
    (*right).set_requested_bounds({130.0, 10.0, 100.0, 60.0});
    (*left).add_child(inner);
    (*root_).add_child(left);
    (*root_).add_child(right);
}

std::unique_ptr<Window> NormativeScene::make_window() {
    std::unique_ptr<Window> window = std::make_unique<Window>(root_, normative_window_size);
    return window;
}

void NormativeScene::begin(const double scale) {
    char header[64]{};
    std::snprintf(header, sizeof header, "normative-trace version=1 scale=%.2f", scale);
    lines_.push_back(header);
    // Step 0 is the window's first presentation.
    lines_.push_back("step 0 show");
    step_open_ = true;
    receipts_open_ = true;
}

void NormativeScene::close_step() {
    if (!step_open_) return;
    for (const std::string& id : receipts_) lines_.push_back("receipt id=" + id);
    receipts_.clear();
    step_open_ = false;
}

void NormativeScene::begin_step(const std::size_t index) {
    close_step();
    const std::vector<NormativeStep>& steps = normative_steps();
    const char* const description = index < steps.size() ? steps[index].description : "";
    lines_.push_back("step " + std::to_string(index + 1U) + " " + description);
    step_open_ = true;
}

void NormativeScene::finish(Window& window) {
    close_step();
    receipts_open_ = false;
    window.perform_layout();
    DisplayTracePainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, normative_window_size.width,
                                                 normative_window_size.height}));
    std::string display{};
    for (const std::string& line : painter.lines) {
        display += line;
        display += '\n';
    }
    char summary[96]{};
    std::snprintf(summary, sizeof summary, "display lines=%zu digest=%016llx", painter.lines.size(),
                  static_cast<unsigned long long>(trace_digest(display)));
    lines_.push_back(summary);
    for (const std::string& line : painter.lines) lines_.push_back("display| " + line);
    lines_.push_back("end");
}

void NormativeScene::record(std::string line) {
    lines_.push_back(std::move(line));
}

void NormativeScene::record_receipt(const std::string& id) {
    receipts_.insert(id);
}

std::string NormativeScene::text() const {
    std::ostringstream output;
    for (const std::string& line : lines_) output << line << '\n';
    return output.str();
}

std::string normative_headless_trace(const double scale) {
    NormativeScene scene;
    std::unique_ptr<Window> window = scene.make_window();
    (*window).set_scale(scale);
    (*window).perform_layout();
    scene.begin(scale);
    NullPainter painter;
    static_cast<void>((*window).paint(painter, Rect{0.0, 0.0, normative_window_size.width,
                                                   normative_window_size.height}));
    const std::vector<NormativeStep>& steps = normative_steps();
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        scene.begin_step(index);
        dispatch_step(*window, steps[index]);
        (*window).perform_layout();
        static_cast<void>((*window).paint(painter));
    }
    scene.finish(*window);
    return scene.text();
}

NormativeDriver::NormativeDriver(NormativeScene& scene, Window& window, NormativeInjector injector,
                                 std::string output_path, std::function<void()> request_close)
    : scene_(scene), window_(window), injector_(injector), output_path_(std::move(output_path)),
      request_close_(std::move(request_close)) {}

void NormativeDriver::start() {
    timer_ = std::make_unique<Timer>(window_, interval);
    token_ = (*timer_).tick().subscribe(Tick{this});
    (*timer_).start();
}

void NormativeDriver::Tick::operator()() const {
    (*driver).tick();
}

void NormativeDriver::tick() {
    if (finished_) return;
    const std::vector<NormativeStep>& steps = normative_steps();
    if (next_step_ < steps.size()) {
        scene_.begin_step(next_step_);
        if (injector_.inject != nullptr) injector_.inject(injector_.context, steps[next_step_]);
        ++next_step_;
        return;
    }
    // The last step has had its interval: finish, write and close.
    finished_ = true;
    (*timer_).stop();
    scene_.finish(window_);
    std::ofstream file(output_path_, std::ios::binary);
    file << scene_.text();
    file.close();
    written_ = !file.fail();
    if (request_close_) request_close_();
}

} // namespace gui_forms::tests
