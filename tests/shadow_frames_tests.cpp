// Shadow frames (stage C5): each thread's logical stack, its capacity and
// truncation, balance on exceptions, and the frames a window opens for input
// dispatch, layout and paint. Since C ABI 1.2: the frames an exception was
// thrown under, as a boundary reads them after catching it, and the
// thread's failure snapshot built from them. Since the STXlib swap-in
// (A2): the stack is STXlib's, a gate's frame carries the library's kind
// and texts, and a snapshot is read from the shadow record a gate attached
// to its ErrorRecord.

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "support/clip_tracking_painter.hpp"
#include "../src/core/failure/failure_snapshot.hpp"
#include "support/routed_input_probe.hpp"

#include <stxlib/error.hpp>
#include <stxlib/gate.hpp>
#include <stxlib/result.hpp>
#include <stxlib/shadow.hpp>

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

StackCopy copy_failure_stack() {
    StackCopy copy;
    copy.recorded = copy_failure_shadow_frames(std::span<ShadowFrame>(copy.frames), copy.state);
    return copy;
}

void throw_two_frames_deep() {
    const ShadowFrameScope call(2U, 7U);
    const ShadowFrameScope event(ShadowFrameKind::event_callback, "button.ok", "click", 0U);
    throw std::runtime_error("thrown two frames deep");
}

// A handler that catches an exception reads the frames it was thrown
// under, though unwinding popped them; with no capture pending (the first
// read took it), it reads the frames open now.
void test_failure_frames() {
    const ShadowFrameScope boundary(ShadowFrameKind::posted_callback, "boundary", std::string_view{}, 0U);
    StackCopy caught;
    try {
        throw_two_frames_deep();
    } catch (const std::runtime_error&) {
        caught = copy_failure_stack();
    }
    require(copy_stack().state.depth == 1U, "unwinding closed the two inner frames");
    require(caught.recorded == 3U && caught.state.depth == 3U, "the boundary reads the three frames of the throw");
    require(caught.frames[0].kind == ShadowFrameKind::posted_callback && subject_of(caught.frames[0]) == "boundary",
            "the outermost failure frame is the boundary's own");
    require(caught.frames[1].kind == ShadowFrameKind::library_call && caught.frames[1].interface_id == 2U &&
                caught.frames[1].function_index == 7U,
            "the middle failure frame is the library call the exception unwound");
    require(caught.frames[2].kind == ShadowFrameKind::event_callback && subject_of(caught.frames[2]) == "button.ok" &&
                name_of(caught.frames[2]) == "click",
            "the innermost failure frame is the one the exception was thrown in");
    {
        const ShadowFrameScope later(ShadowFrameKind::layout_pass, "later", std::string_view{}, 1U);
    }
    const StackCopy moved_on = copy_failure_stack();
    require(moved_on.recorded == 1U && moved_on.state.depth == 1U,
            "with no capture pending, the failure frames are the frames open now");
}

// The body of a framed call: it throws, as an application handler in
// another module would; no runtime is asked whether it is unwinding.
int throwing_body(int value) {
    if (value != 0) throw std::runtime_error("a framed body failed");
    return value;
}

// A call frame's entry: open, call the body, mark returned.
int framed_entry(int value) {
    ShadowFrameCall frame(ShadowFrameKind::input_dispatch, "entry", "pointer", 0U);
    const int result = throwing_body(value);
    frame.returned();
    return result;
}

// A call frame closed by its returned() mark closes as a return does; one
// destroyed before the mark is unwinding and marks the depth, whichever
// module threw.
void test_call_frames() {
    const ShadowFrameScope boundary(ShadowFrameKind::posted_callback, "call-boundary", std::string_view{}, 0U);
    require(framed_entry(0) == 0 && copy_stack().state.depth == 1U, "a returned call frame closes");
    const StackCopy after_return = copy_failure_stack();
    require(after_return.state.depth == 1U, "a returned call frame leaves no failure mark");
    StackCopy caught;
    try {
        static_cast<void>(framed_entry(1));
    } catch (const std::runtime_error&) {
        caught = copy_failure_stack();
    }
    require(copy_stack().state.depth == 1U, "an unwound call frame closes");
    require(caught.recorded == 2U && caught.frames[1].kind == ShadowFrameKind::input_dispatch &&
                subject_of(caught.frames[1]) == "entry",
            "an unwound call frame is in the failure frames");
}

// The thread's snapshot: taken in a catch block, it holds the boundary, the
// message cut to its capacity and the frames of the throw; clearing it keeps
// the sequence counting.
void test_failure_snapshot() {
    detail::clear_failure_snapshot();
    const std::uint64_t before = detail::thread_failure_snapshot().sequence;
    require(detail::thread_failure_snapshot().status == 0 && detail::thread_failure_snapshot().frame_count == 0U,
            "a cleared thread has no snapshot");
    try {
        throw_two_frames_deep();
    } catch (const std::runtime_error&) {
        const detail::FailureBoundary boundary{"test.boundary", 8, 3U, 4U};
        detail::record_failure_snapshot(boundary, std::current_exception());
    }
    const detail::FailureSnapshot& snapshot = detail::thread_failure_snapshot();
    require(snapshot.status == 8 && snapshot.interface_id == 3U && snapshot.function_index == 4U,
            "the snapshot records its boundary's status and function");
    require(snapshot.sequence == before + 1U, "each snapshot counts");
    require(std::string_view(snapshot.site.data(), snapshot.site_size) == "test.boundary",
            "the snapshot records its site");
    require(std::string_view(snapshot.message.data(), snapshot.message_size) == "thrown two frames deep",
            "the snapshot records the exception's text");
    require(snapshot.frame_count == 2U && snapshot.depth == 2U &&
                snapshot.frames[1].kind == ShadowFrameKind::event_callback,
            "the snapshot holds the frames the exception was thrown under");

    // A message longer than the capacity is cut at a whole UTF-8 sequence:
    // 511 ASCII bytes, then a two-byte sequence that would end at byte 513.
    std::string long_message(511U, 'x');
    long_message += "\xC3\xA9";
    {
        // The thread moves on, so the next snapshot is taken outside any frame.
        const ShadowFrameScope moved_on(ShadowFrameKind::layout_pass, "moved-on", std::string_view{}, 1U);
    }
    const detail::FailureBoundary site_only{"test.long", 16, 0U, 0U};
    detail::record_failure_snapshot(site_only, std::string_view(long_message));
    const detail::FailureSnapshot& cut = detail::thread_failure_snapshot();
    require(cut.message_size == 511U && cut.frame_count == 0U && cut.sequence == before + 2U,
            "a long message is cut before a split sequence, outside any frame");

    detail::clear_failure_snapshot();
    const detail::FailureSnapshot& cleared = detail::thread_failure_snapshot();
    require(cleared.status == 0 && cleared.message_size == 0U && cleared.site_size == 0U &&
                cleared.sequence == before + 2U,
            "clearing forgets the snapshot and keeps the count");

    require(!detail::exception_reflection(), "exception reflection starts off");
    detail::set_exception_reflection(true);
    require(detail::exception_reflection(), "exception reflection turns on");
    detail::set_exception_reflection(false);
}

// A gate target that copies the stack it runs under.
struct CopyingTarget final {
    StackCopy* seen = nullptr;

    stxlib::OperationStatus operator()() const {
        *seen = copy_stack();
        const stxlib::OperationStatus result = stxlib::OperationStatus::success();
        return result;
    }
};

// A gate target that throws two frames below the gate.
struct ThrowingTarget final {
    stxlib::OperationStatus operator()() const {
        throw_two_frames_deep();
        const stxlib::OperationStatus result = stxlib::OperationStatus::success();
        return result;
    }
};

// A gate's frame is the whole frame (the C ABI's slot gate, call_slot): a
// gate described as a library call is one library_call frame with the
// slot's numbers, not a library call plus a frame of the gate's own.
void test_gate_frames() {
    StackCopy seen;
    CopyingTarget copying{&seen};
    const stxlib::GateFrame slot_frame{
        .operation = "gui_forms.c_abi",
        .first = 9U,
        .second = 3U,
        .kind = static_cast<stxlib::u32>(ShadowFrameKind::library_call),
    };
    const stxlib::OperationStatus returned = stxlib::call_gate(slot_frame, copying);
    require(returned.succeeded(), "a returning target passes through its gate");
    require(seen.recorded == 1U && seen.frames[0].kind == ShadowFrameKind::library_call &&
                seen.frames[0].interface_id == 9U && seen.frames[0].function_index == 3U &&
                seen.frames[0].name_size == 0U,
            "a slot gate is one library_call frame with the slot's numbers");

    // A gate described only by its operation is a gate frame named by it.
    const stxlib::OperationStatus plain = stxlib::call_gate(stxlib::Operation("test.gate"), copying);
    require(plain.succeeded() && seen.recorded == 1U && seen.frames[0].kind == ShadowFrameKind::gate &&
                name_of(seen.frames[0]) == "test.gate",
            "an operation's gate frame is named by its operation");
    // A gate frame's name field holds 32 bytes (STXlib cda6fb1): a longer
    // operation name is cut there and says so; the record keeps it whole.
    const stxlib::OperationStatus long_name =
        stxlib::call_gate(stxlib::Operation("windows.MsaaElement.QueryInterface"), copying);
    require(long_name.succeeded() && seen.recorded == 1U && seen.frames[0].kind == ShadowFrameKind::gate &&
                name_of(seen.frames[0]) == "windows.MsaaElement.QueryInterfa" &&
                seen.frames[0].flags == static_cast<std::uint32_t>(ShadowFrameFlags::name_truncated),
            "a gate frame's name is its operation's first 32 bytes, marked as cut");
    require(copy_stack().state.depth == 0U, "a gate closes its frame");
}

// The snapshot of a gate's failure: taken after the gate returned, from the
// shadow record on its ErrorRecord, it holds the gate's frame and the frames
// the exception was thrown under.
void test_gate_failure_snapshot() {
    ThrowingTarget throwing;
    const stxlib::GateFrame slot_frame{
        .operation = "gui_forms.c_abi",
        .first = 10U,
        .second = 7U,
        .kind = static_cast<stxlib::u32>(ShadowFrameKind::library_call),
    };
    const stxlib::OperationStatus failed = stxlib::call_gate(slot_frame, throwing);
    require(!failed.succeeded(), "a throwing target fails its gate");
    const stxlib::ErrorRecord& record = *failed.error();
    require(copy_stack().state.depth == 0U, "the gate's failure leaves no frame open");

    const std::uint64_t before = detail::thread_failure_snapshot().sequence;
    const detail::FailureBoundary boundary{"c_abi", 13, 10U, 7U};
    detail::record_failure_snapshot(boundary, "thrown two frames deep", record);
    const detail::FailureSnapshot& snapshot = detail::thread_failure_snapshot();
    require(snapshot.sequence == before + 1U && snapshot.status == 13, "the gate's failure is a snapshot");
    require(snapshot.frame_count == 3U && snapshot.depth == 3U, "the snapshot holds the gate's frame and two more");
    require(snapshot.frames[0].kind == ShadowFrameKind::library_call && snapshot.frames[0].interface_id == 10U &&
                snapshot.frames[0].function_index == 7U,
            "the outermost frame is the slot gate's");
    require(snapshot.frames[1].kind == ShadowFrameKind::library_call && snapshot.frames[1].interface_id == 2U,
            "the middle frame is the library call the exception unwound");
    require(snapshot.frames[2].kind == ShadowFrameKind::event_callback &&
                subject_of(snapshot.frames[2]) == "button.ok",
            "the innermost frame is the one the exception was thrown in");

    // The development detail (survey S4): the exception's type and the
    // trace at the throw, when STXlib keeps them.
    if (stxlib::shadowing_enabled()) {
        const std::string_view type(snapshot.type.data(), snapshot.type_size);
        require(type.find("runtime_error") != std::string_view::npos, "the snapshot names the exception's type");
        require(snapshot.trace_count != 0U && snapshot.trace[0] != 0U, "the snapshot keeps the trace at the throw");
    } else {
        require(snapshot.type_size == 0U && snapshot.trace_count == 0U, "a release build keeps no type or trace");
    }

    const stxlib::ErrorRecord unshadowed =
        stxlib::ErrorRecord::make(stxlib::ErrorKind::internal_failure, stxlib::Operation("test.unshadowed"));
    detail::record_failure_snapshot(boundary, "no record", unshadowed);
    require(detail::thread_failure_snapshot().frame_count == 0U && detail::thread_failure_snapshot().depth == 0U &&
                detail::thread_failure_snapshot().type_size == 0U && detail::thread_failure_snapshot().trace_count == 0U,
            "a failure with no shadow record has no frames, type or trace");
    detail::clear_failure_snapshot();
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

class FrameProbe final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    FrameProbe(StableId stable_id, Seen& seen) : gui_forms::tests::RoutedInputProbe<Control>(std::move(stable_id)), seen_(seen) {}

    Size measure(Size available) override {
        seen_.measure = copy_stack();
        return Control::measure(available);
    }
    void on_paint(Painter& painter, Rect local_damage) override {
        seen_.paint = copy_stack();
        Control::on_paint(painter, local_damage);
    }
    void routed_pointer(PointerEvent& event) override {
        seen_.pointer = copy_stack();
        if (seen_.throw_on_pointer) {
            throw std::runtime_error("the probe's pointer handler failed");
        }
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
    StackCopy failure;
    try {
        static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, Point{20.0, 20.0}}));
    } catch (const std::runtime_error&) {
        caught = true;
        // Inside the handler: the read takes the thread's pending capture.
        failure = copy_failure_stack();
    }
    require(caught, "the probe's failure reaches the caller");
    require(copy_stack().state.depth == 0U, "a failing handler leaves no frame open");
    const ShadowFrame* failed_input = find_kind(failure, ShadowFrameKind::input_dispatch);
    require(failed_input != nullptr && subject_of(*failed_input) == "frames.root",
            "the failure frames hold the input dispatch the handler threw under");
}

} // namespace

int main() {
    try {
        test_nesting_and_overflow();
        test_truncation();
        test_exception_balance();
        test_failure_frames();
        test_call_frames();
        test_failure_snapshot();
        test_gate_frames();
        test_gate_failure_snapshot();
        test_threads_are_separate();
        test_window_frames();
    } catch (const std::exception& error) {
        std::cerr << "shadow frames test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "shadow frames tests passed\n";
    return 0;
}
