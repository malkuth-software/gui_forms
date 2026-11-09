// The scrub gesture (the node engine and value bar study, sections 3.2,
// 3.4 and 4; the declarative dragging study's step M2), on the window's
// drag engine: a click is not a scrub; the scrub previews start + travel *
// rate and commits once; the soft range, widened to the start; Shift fine
// and Ctrl snapping; Escape and a right press restore; the keyboard mode
// and its announcement; both pointer-lock settings; no allocation from the
// first scrubbing frame; and a pointer script's trace. Headless: a window
// that is never shown.

#include "gui_forms/gui_forms.hpp"
#include "../src/core/drag_engine/scrub_gesture.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

// Counts allocation attempts on this thread while enabled. Replacement
// allocation is confined to this executable.
namespace scrub_allocation_probe {
thread_local bool enabled{};
thread_local std::size_t attempts{};

void* allocate(const std::size_t bytes) {
    if (enabled) ++attempts;
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = std::malloc(extent);
    if (!memory) throw std::bad_alloc();
    return memory;
}

void* allocate_aligned(const std::size_t bytes, const std::size_t alignment) {
    if (enabled) ++attempts;
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = nullptr;
#ifdef _WIN32
    memory = _aligned_malloc(extent, alignment);
#else
    const int status = posix_memalign(&memory, alignment, extent);
    if (status != 0) throw std::bad_alloc();
#endif
    if (!memory) throw std::bad_alloc();
    return memory;
}

void release_aligned(void* const memory) noexcept {
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

class CountScope final {
public:
    CountScope() {
        attempts = 0U;
        enabled = true;
    }
    ~CountScope() { enabled = false; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;
};
} // namespace scrub_allocation_probe

void* operator new(const std::size_t bytes) {
    void* const memory = scrub_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = scrub_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { std::free(memory); }
void operator delete(void* const memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = scrub_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = scrub_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept {
    scrub_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t) noexcept {
    scrub_allocation_probe::release_aligned(memory);
}
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept {
    scrub_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept {
    scrub_allocation_probe::release_aligned(memory);
}
// The nothrow forms too: the standard library uses them (std::stable_sort's
// temporary buffer), and every form must come from the same allocator as its
// matching delete, or a sanitizer reports an allocation/deallocation mismatch.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = scrub_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = scrub_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = scrub_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = scrub_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    scrub_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    scrub_allocation_probe::release_aligned(memory);
}

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-9;
    return result;
}

// The owner's terms and a record of what the gesture asked of it. The
// records are reserved, so the target allocates nothing while scrubbing.
class RecordingTarget final : public ScrubTarget {
public:
    RecordingTarget() { previews.reserve(256U); }

    bool begin_scrub(ScrubRange& terms) override {
        ++begins;
        if (refuse) return false;
        terms = range;
        return true;
    }
    void preview_scrub(double value) override { previews.push_back(value); }
    void end_scrub(ScrubEnd end, double value) override {
        ++ends;
        last_end = end;
        last_value = value;
    }

    ScrubRange range{.start = 0.5, .minimum = 0.0, .maximum = 1.0, .rate = 0.01, .snap_step = 0.1,
                     .keyboard_step = 0.05};
    bool refuse{};
    int begins{};
    int ends{};
    ScrubEnd last_end{ScrubEnd::restored};
    double last_value{};
    std::vector<double> previews;
};

// A control owning a gesture, as ValueBar does: a primary press arms it,
// the rest of the pointer stream goes to it, a click is counted.
class ScrubPad final : public Control {
public:
    explicit ScrubPad(StableId stable_id) : Control(std::move(stable_id)), gesture(*this, target) {}

    void on_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::down && event.button == PointerButton::primary) {
            if (gesture.press(event, lock)) event.handled = true;
            return;
        }
        const ScrubPointerResult result = gesture.pointer(event);
        if (result == ScrubPointerResult::clicked) ++clicks;
        if (result != ScrubPointerResult::ignored) event.handled = true;
    }
    bool begin_keyboard_drag() override {
        const bool started = gesture.begin_keyboard();
        return started;
    }

    RecordingTarget target;
    ScrubGesture gesture;
    ScrubPointerLock lock{ScrubPointerLock::clamped};
    int clicks{};
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ScrubPad> pad;
    std::unique_ptr<Window> window;
};

Fixture make_fixture() {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId("scrub.root"));
    fixture.pad = make_control<ScrubPad>(StableId("scrub.pad"));
    (*fixture.pad).set_requested_bounds({20.0, 20.0, 200.0, 24.0});
    (*fixture.root).add_child(fixture.pad);
    fixture.window = std::make_unique<Window>(fixture.root, Size{400.0, 100.0});
    (*fixture.window).perform_layout();
    return fixture;
}

PointerEvent pointer_event(PointerAction action, Point point, Modifier modifiers = Modifier::none,
                           PointerButton button = PointerButton::primary) {
    PointerEvent event{};
    event.action = action;
    event.button = action == PointerAction::move ? PointerButton::none : button;
    event.position = point;
    event.modifiers = modifiers;
    return event;
}

void pointer(Window& window, PointerAction action, Point point, Modifier modifiers = Modifier::none) {
    static_cast<void>(window.dispatch_pointer(pointer_event(action, point, modifiers)));
}

KeyEvent key_down(std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical_key;
    event.modifiers = modifiers;
    return event;
}

constexpr double row = 30.0;

// A release before the threshold is a click: the target is never asked.
void click_is_not_a_scrub() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {102.0, row});
    pointer(window, PointerAction::up, {102.0, row});
    require(pad.clicks == 1 && pad.target.begins == 0 && pad.target.ends == 0, "a click asks nothing of the target");
    require(pad.gesture.statistics().clicks == 1U && pad.gesture.statistics().scrubs == 0U, "a click is counted");
    require(!pad.gesture.running(), "the session is over");
}

// Press, pass the threshold, scrub, release: the value follows the travel
// at the rate, live, and one commit carries the last value.
void scrub_commits_once() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {103.0, row});
    require(pad.target.begins == 0, "within the threshold nothing is planned");
    pointer(window, PointerAction::move, {110.0, row});
    require(pad.target.begins == 1 && pad.gesture.state().scrubbing, "past the threshold the scrub begins");
    pointer(window, PointerAction::move, {130.0, row});
    pointer(window, PointerAction::up, {135.0, row});
    const std::vector<double>& seen = pad.target.previews;
    require(seen.size() == 2U && near(seen[0], 0.6) && near(seen[1], 0.8), "each frame previews start + travel * rate");
    require(pad.target.ends == 1 && pad.target.last_end == ScrubEnd::committed && near(pad.target.last_value, 0.85),
            "the release commits once, with its own travel");
    require(!pad.gesture.state().scrubbing && pad.clicks == 0, "the scrub is over and was no click");
}

// The soft range: the scrub stops at its ends; a start outside it widens it
// so the value does not jump.
void soft_range() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {390.0, row});
    pointer(window, PointerAction::move, {380.0, row});
    pointer(window, PointerAction::up, {380.0, row});
    require(near(pad.target.previews[0], 1.0), "the scrub stops at the soft maximum");
    require(near(pad.target.previews[1], 0.9), "travel back moves at once from the end");
    pad.target.previews.clear();
    pad.target.range.start = 1.5;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {90.0, row});
    pointer(window, PointerAction::up, {90.0, row});
    require(near(pad.target.previews[0], 1.4) && near(pad.target.last_value, 1.4),
            "a start above the soft range scrubs down from itself");
}

// Shift slows the scrub from where it is pressed; Ctrl snaps the shown
// value to the step, and releasing Ctrl resumes the unsnapped value.
void modifiers() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {110.0, row});
    pointer(window, PointerAction::move, {130.0, row}, Modifier::shift);
    pointer(window, PointerAction::move, {140.0, row});
    pointer(window, PointerAction::up, {140.0, row});
    const std::vector<double>& fine = pad.target.previews;
    require(near(fine[0], 0.6) && near(fine[1], 0.62) && near(fine[2], 0.72), "Shift scrubs at a tenth, no jump");
    pad.target.previews.clear();
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {113.0, row}, Modifier::control);
    pointer(window, PointerAction::move, {118.0, row}, Modifier::control);
    pointer(window, PointerAction::move, {119.0, row});
    pointer(window, PointerAction::up, {119.0, row});
    const std::vector<double>& snapped = pad.target.previews;
    require(near(snapped[0], 0.6) && near(snapped[1], 0.7), "Ctrl shows the value on the snap step");
    require(near(snapped[2], 0.69) && near(pad.target.last_value, 0.69), "without Ctrl the scrub resumes unsnapped");
}

// Escape and a right press restore the start value; nothing commits.
void cancels_restore() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {120.0, row});
    require(window.dispatch_key(key_down(PhysicalKey::escape)), "the window gives the scrub Escape");
    require(pad.target.ends == 1 && pad.target.last_end == ScrubEnd::restored && near(pad.target.last_value, 0.5),
            "Escape restores the start value");
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {120.0, row});
    static_cast<void>(window.dispatch_pointer(
        pointer_event(PointerAction::down, {120.0, row}, Modifier::none, PointerButton::secondary)));
    require(pad.target.ends == 2 && pad.target.last_end == ScrubEnd::restored && near(pad.target.last_value, 0.5),
            "a right press restores the start value");
    require(pad.gesture.statistics().commits == 0U && pad.gesture.statistics().restores == 2U, "no commit");
    pad.target.refuse = true;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {120.0, row});
    pointer(window, PointerAction::up, {120.0, row});
    require(pad.target.ends == 2 && !pad.gesture.running(), "a refused scrub ends without an end call");
}

// The keyboard mode: arrows by the keyboard step, Ctrl coarse, Enter
// commits; the announcement names the value and the end.
void keyboard_mode() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    require(pad.begin_keyboard_drag(), "the keyboard scrub starts");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::right)));
    require(!pad.target.previews.empty() && near(pad.target.previews.back(), 0.55), "an arrow steps once");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::right, Modifier::control)));
    require(near(pad.target.previews.back(), 1.0), "Ctrl steps coarsely, within the range");
    static_cast<void>(window.dispatch_key(key_down(PhysicalKey::enter)));
    require(pad.target.last_end == ScrubEnd::committed && near(pad.target.last_value, 1.0), "Enter commits");
    const std::string_view announced = (*window.drag_engine_if_made()).announcement();
    require(announced == "1, done", "the end is announced with the value");
}

// Pointer lock, both capability settings (study section 4): relative
// hides the pointer while scrubbing; clamped marks the window's edge.
void pointer_lock() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pad.lock = ScrubPointerLock::relative;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {110.0, row});
    require(pad.gesture.state().pointer_hidden && !pad.gesture.state().at_edge, "relative: the pointer is hidden");
    pointer(window, PointerAction::move, {0.0, row});
    require(!pad.gesture.state().at_edge, "relative: no edge");
    pointer(window, PointerAction::up, {0.0, row});
    require(!pad.gesture.state().pointer_hidden, "the pointer shows again after the scrub");
    pad.lock = ScrubPointerLock::clamped;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {110.0, row});
    require(!pad.gesture.state().pointer_hidden, "clamped: the pointer stays");
    pointer(window, PointerAction::move, {0.0, row});
    require(pad.gesture.state().at_edge && pad.gesture.statistics().edge_frames >= 1U, "clamped: the edge is marked");
    pointer(window, PointerAction::move, {50.0, row});
    require(!pad.gesture.state().at_edge, "off the edge again");
    pointer(window, PointerAction::up, {50.0, row});
    // The rate is unchanged at the edge: 0.5 + (110 - 100) * 0.01, down to
    // 0 at the edge (clamped by the soft minimum), then + 50 * 0.01.
    require(near(pad.target.last_value, 0.5), "the clamped scrub keeps its rate");
}

// From the first scrubbing frame to the end nothing is allocated, the
// target's reserved record aside (study section 4: zero in the interior
// of a scrub).
void no_allocation_while_scrubbing() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    pointer(window, PointerAction::down, {100.0, row});
    pointer(window, PointerAction::move, {110.0, row});
    require(pad.gesture.state().scrubbing, "an allocation-counted scrub runs");
    std::size_t attempts = 0U;
    {
        const scrub_allocation_probe::CountScope scope;
        for (int step = 0; step < 100; ++step) {
            const double x = 110.0 + static_cast<double>(step % 40);
            const Modifier held = step % 3 == 0 ? Modifier::shift : Modifier::none;
            static_cast<void>(pad.gesture.pointer(pointer_event(PointerAction::move, {x, row}, held)));
        }
        attempts = scrub_allocation_probe::attempts;
    }
    require(attempts == 0U, "scrubbing allocates nothing");
    pointer(window, PointerAction::up, {120.0, row});
    require(pad.target.ends == 1, "the counted scrub committed");
}

// The trace: a pointer script in, the previewed values and the end out,
// against the expected text.
void scrub_trace() {
    Fixture fixture = make_fixture();
    Window& window = *fixture.window;
    ScrubPad& pad = *fixture.pad;
    struct Step final {
        PointerAction action{PointerAction::move};
        double x{};
        Modifier modifiers{Modifier::none};
    };
    const Step script[] = {
        {.action = PointerAction::down, .x = 100.0, .modifiers = Modifier::none},
        {.action = PointerAction::move, .x = 104.0, .modifiers = Modifier::none},
        {.action = PointerAction::move, .x = 120.0, .modifiers = Modifier::none},
        {.action = PointerAction::move, .x = 140.0, .modifiers = Modifier::shift},
        {.action = PointerAction::move, .x = 141.0, .modifiers = Modifier::control},
        {.action = PointerAction::up, .x = 141.0, .modifiers = Modifier::control},
    };
    std::string trace{};
    std::array<char, 64> line{};
    std::size_t seen = 0U;
    for (const Step& step : script) {
        pointer(window, step.action, {step.x, row}, step.modifiers);
        for (; seen < pad.target.previews.size(); ++seen) {
            const int written = std::snprintf(line.data(), line.size(), "preview %.4f\n", pad.target.previews[seen]);
            if (written > 0) trace.append(line.data(), static_cast<std::size_t>(written));
        }
    }
    const int written = std::snprintf(line.data(), line.size(), "end %s %.4f\n",
                                      pad.target.last_end == ScrubEnd::committed ? "committed" : "restored",
                                      pad.target.last_value);
    if (written > 0) trace.append(line.data(), static_cast<std::size_t>(written));
    const std::string_view expected = "preview 0.5400\n"
                                      "preview 0.7000\n"
                                      "preview 0.7200\n"
                                      "preview 0.7000\n"
                                      "end committed 0.7000\n";
    if (trace != expected) std::cerr << trace;
    require(trace == expected, "the scrub trace matches");
}

} // namespace

int main() {
    click_is_not_a_scrub();
    scrub_commits_once();
    soft_range();
    modifiers();
    cancels_restore();
    keyboard_mode();
    pointer_lock();
    no_allocation_while_scrubbing();
    scrub_trace();
    std::cout << "scrub gesture: click, scrub, soft range, modifiers, cancels, keyboard, pointer lock, allocation "
                 "and trace passed\n";
    return 0;
}
