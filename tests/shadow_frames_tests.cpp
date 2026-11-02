// Shadow frames (stage C5): each thread's logical stack, its capacity and
// truncation, balance on exceptions, and the frames a window opens for input
// dispatch, layout and paint.

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/gui_forms.hpp"
#include "support/clip_tracking_painter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The calling thread's stack, copied.
struct StackCopy final {
    std::array<ShadowFrame, shadow_stack_capacity> frames{};
    ShadowStackState state;
    std::size_t recorded = 0U;
};

StackCopy copy_stack() {
    StackCopy copy;
    copy.recorded = copy_shadow_frames(std::span<ShadowFrame>(copy.frames), copy.state);
    return copy;
}

std::string_view subject_of(const ShadowFrame& frame) {
    return std::string_view(frame.subject.data(), frame.subject_size);
}

std::string_view name_of(const ShadowFrame& frame) {
    return std::string_view(frame.name.data(), frame.name_size);
}

void test_nesting_and_overflow() {
    const StackCopy before = copy_stack();
    require(before.state.depth == 0U && before.recorded == 0U, "a thread starts with no frames");
    {
        const ShadowFrameScope call(2U, 7U);
        const ShadowFrameScope event(ShadowFrameKind::event_callback, "button.ok", "click", 0U);
        const StackCopy inside = copy_stack();
        require(inside.recorded == 2U && inside.state.depth == 2U, "two nested scopes are two frames");
        require(inside.frames[0].kind == ShadowFrameKind::library_call && inside.frames[0].interface_id == 2U &&
                    inside.frames[0].function_index == 7U,
                "the outermost frame is the library call");
        require(subject_of(inside.frames[1]) == "button.ok" && name_of(inside.frames[1]) == "click",
                "the inner frame keeps its subject and name");
    }
    // Scopes nest only through calls, so a recursive helper opens frames
    // past the capacity.
    struct Nest final {
        static std::size_t open(std::size_t remaining) {
            const ShadowFrameScope frame(ShadowFrameKind::layout_pass, "nest", std::string_view{}, remaining);
            if (remaining == 0U) {
                const StackCopy deepest = copy_stack();
                require(deepest.recorded == shadow_stack_capacity, "the stack records its capacity");
                require(deepest.state.depth == shadow_stack_capacity + 6U, "depth counts every open frame");
                require(deepest.frames[0].sequence == shadow_stack_capacity + 5U,
                        "the recorded frames are the outermost");
                return deepest.state.dropped;
            }
            return open(remaining - 1U);
        }
    };
    const std::uint64_t dropped_before = copy_stack().state.dropped;
    const std::size_t dropped = Nest::open(shadow_stack_capacity + 5U);
    require(dropped == dropped_before + 6U, "frames past the capacity are counted as dropped");
    const StackCopy after = copy_stack();
    require(after.state.depth == 0U && after.recorded == 0U, "every frame closes");
}

void test_truncation() {
    // 47 ASCII bytes and a two-byte sequence: the sequence does not fit.
    const std::string subject = std::string(47U, 's') + "\xC3\xA9";
    const std::string name(40U, 'n');
    const ShadowFrameScope frame(ShadowFrameKind::event_callback, subject, name, 0U);
    const StackCopy copy = copy_stack();
    const ShadowFrame& top = copy.frames[copy.recorded - 1U];
    require(top.subject_size == 47U, "a subject is cut before a sequence that does not fit");
    require(top.name_size == shadow_frame_name_capacity, "a name is cut at its capacity");
    require(top.flags == (static_cast<std::uint32_t>(ShadowFrameFlags::subject_truncated) |
                          static_cast<std::uint32_t>(ShadowFrameFlags::name_truncated)),
            "a cut text is flagged");
}

void throw_inside_frame() {
    const ShadowFrameScope frame(ShadowFrameKind::event_callback, "thrower", "click", 0U);
    throw std::runtime_error("a handler failed");
}

void test_exception_balance() {
    bool caught = false;
    try {
        throw_inside_frame();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    require(caught && copy_stack().state.depth == 0U, "an exception closes the frames it unwinds");
}

// Runs on another thread: that thread's depth.
void copy_depth_into(std::uint32_t& depth) {
    depth = copy_stack().state.depth;
}

void test_threads_are_separate() {
    const ShadowFrameScope frame(ShadowFrameKind::posted_callback, "main", std::string_view{}, 0U);
    std::uint32_t other_depth = 99U;
    std::thread other(copy_depth_into, std::ref(other_depth));
    other.join();
    require(other_depth == 0U, "another thread's stack is its own");
}

// What the probe control saw, and whether its pointer handler throws.
struct Seen final {
    StackCopy pointer;
    StackCopy measure;
    StackCopy paint;
    bool throw_on_pointer = false;
};

class FrameProbe final : public Control {
public:
    FrameProbe(StableId stable_id, Seen& seen) : Control(std::move(stable_id)), seen_(seen) {}

    Size measure(Size available) override {
        seen_.measure = copy_stack();
        return Control::measure(available);
    }
    void on_paint(Painter& painter, Rect local_damage) override {
        seen_.paint = copy_stack();
        Control::on_paint(painter, local_damage);
    }
    void on_pointer(PointerEvent& event) override {
        seen_.pointer = copy_stack();
        if (seen_.throw_on_pointer) {
            throw std::runtime_error("the probe's pointer handler failed");
        }
        Control::on_pointer(event);
    }

private:
    Seen& seen_;
};

const ShadowFrame* find_kind(const StackCopy& copy, ShadowFrameKind kind) {
    for (std::size_t index = 0U; index < copy.recorded; ++index) {
        if (copy.frames[index].kind == kind) {
            return &copy.frames[index];
        }
    }
    return nullptr;
}

void test_window_frames() {
    Seen seen;
    const Control::Ptr root = make_control<Control>(StableId("frames.root"));
    (*root).set_requested_bounds({0.0, 0.0, 120.0, 80.0});
    const std::shared_ptr<FrameProbe> probe = make_control<FrameProbe>(StableId("frames.probe"), seen);
    (*probe).set_requested_bounds({10.0, 10.0, 60.0, 40.0});
    (*root).add_child(probe);
    Window window(root, {120.0, 80.0});
    window.perform_layout();
    const ShadowFrame* layout = find_kind(seen.measure, ShadowFrameKind::layout_pass);
    require(layout != nullptr && subject_of(*layout) == "frames.root" && (*layout).sequence != 0U,
            "measure runs inside the window's layout frame");

    test_support::ClipTrackingPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 120.0, 80.0}));
    const ShadowFrame* paint = find_kind(seen.paint, ShadowFrameKind::paint_pass);
    require(paint != nullptr && subject_of(*paint) == "frames.root" && (*paint).sequence != 0U,
            "on_paint runs inside the window's paint frame");

    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, Point{20.0, 20.0}}));
    const ShadowFrame* input = find_kind(seen.pointer, ShadowFrameKind::input_dispatch);
    require(input != nullptr && subject_of(*input) == "frames.root" && name_of(*input) == "pointer",
            "on_pointer runs inside the window's input frame");

    seen.throw_on_pointer = true;
    bool caught = false;
    try {
        static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, Point{20.0, 20.0}}));
    } catch (const std::runtime_error&) {
        caught = true;
    }
    require(caught, "the probe's failure reaches the caller");
    require(copy_stack().state.depth == 0U, "a failing handler leaves no frame open");
}

} // namespace

int main() {
    try {
        test_nesting_and_overflow();
        test_truncation();
        test_exception_balance();
        test_threads_are_separate();
        test_window_frames();
    } catch (const std::exception& error) {
        std::cerr << "shadow frames test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "shadow frames tests passed\n";
    return 0;
}
