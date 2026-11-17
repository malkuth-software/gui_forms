#pragma once

// The normative trace (release-1.0 condition 2; ADR-003, ADR-014): one
// scripted scene whose input order, focus, activation, text, paint receipts
// and display commands every host must reproduce. The headless run
// (normative_headless_trace) dispatches the script straight into the
// Window and is the reference; a host driver (macOS:
// macos_normative_trace.mm; X11: linux_normative_trace.cpp) injects the same
// steps as native events into a real window and records the same lines.
// tools/normative_trace.py compares a host's trace with the reference.
//
// Trace format, one line each, no timestamps:
//   normative-trace version=1 scale=<s>
//   step <n> <description>               before the step's input
//   pointer id=<id> phase=<p> action=<a>  a routed pointer event (preview,
//                                         target raiser, bubble)
//   key id=<id> phase=<p> code=<HID usage>
//   text id=<id> value=<utf-8>
//   focus id=<id> state=on|off
//   activate id=<id>
//   receipt id=<id>                       a control painted since the step
//                                         began (sorted, once each), written
//                                         when the step ends
//   display lines=<n> digest=<16 hex>     the whole window's display trace
//   display| <command line>               ... and its lines, in the display
//                                         trace tests' format (lengths in
//                                         1/64 logical px, rounded)
//   end
// Pointer moves, enters and leaves are recorded too; a host may add moves
// the script did not make (tools/normative_trace.py ignores pointer moves,
// enters and leaves unless --strict-hover).

#include "gui_forms/timer/timer/timer.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace gui_forms::tests {

enum class NormativeStepKind : std::uint8_t {
    pointer_move,
    // A primary press and release at one point.
    click,
    // One detented wheel line down.
    wheel_down,
    // A key pressed and released; typed carries text for the text step.
    key,
    typed,
};

// One step of the script. `position` is in window coordinates (logical
// units, origin top left); `physical_key` is a HID usage (PhysicalKey);
// `text` is what a typed key inserts.
struct NormativeStep final {
    NormativeStepKind kind{NormativeStepKind::pointer_move};
    Point position{};
    std::uint32_t physical_key{};
    const char* text{""};
    const char* description{""};
};

inline constexpr Size normative_window_size{240.0, 160.0};

// The script, in order: fixed for the trace's version.
[[nodiscard]] const std::vector<NormativeStep>& normative_steps();

// The scene: a root and three focusable recorder controls (left, right and
// a nested inner), all recording into one trace. The scene owns the
// trace; the window made by make_window holds the controls. Root thread
// only.
class NormativeScene final {
public:
    NormativeScene();
    ~NormativeScene() {}
    NormativeScene(const NormativeScene&) = delete;
    NormativeScene& operator=(const NormativeScene&) = delete;

    // The scene's window, once: the caller (or the host it is given to)
    // owns it, and the scene observes it until it is destroyed.
    [[nodiscard]] std::unique_ptr<Window> make_window();

    // Writes the header with the window's backing scale.
    void begin(double scale);
    // Closes the previous step (its receipts) and opens step `index`.
    void begin_step(std::size_t index);
    // Closes the last step and records the whole window's display trace
    // from `window`, painted now with a trace painter (paint receipts are
    // not recorded for it), then the end line.
    void finish(Window& window);

    // The recorder controls' entries.
    void record(std::string line);
    void record_receipt(const std::string& id);
    // Whether receipts are recorded (off while finish paints).
    [[nodiscard]] bool receipts_open() const noexcept { return receipts_open_; }

    [[nodiscard]] std::string text() const;

private:
    void close_step();

    std::vector<std::string> lines_{};
    std::set<std::string> receipts_{};
    bool receipts_open_{false};
    bool step_open_{false};
    std::shared_ptr<Control> root_{};
};

// The reference: the script dispatched straight into a Window at `scale`.
[[nodiscard]] std::string normative_headless_trace(double scale);

// How a host driver injects one step as native events into its window
// (posted to the window's own event queue, or sent over a second display
// connection): a named target with its context, called on the UI thread.
struct NormativeInjector final {
    void (*inject)(void* context, const NormativeStep& step){};
    void* context{};
};

// A host driver's pacing, portable: a Timer on the scene's window opens each
// step in the trace, injects it, and gives the host one interval to deliver
// and paint it; after the last step and one more interval it finishes the
// trace, writes it to `output_path` and asks the host to close the window.
// It borrows the scene and the window, which outlive it (declare it after
// both; the host's run must still be in progress while it ticks). UI
// thread only.
class NormativeDriver final {
public:
    NormativeDriver(NormativeScene& scene, Window& window, NormativeInjector injector, std::string output_path,
                    std::function<void()> request_close);
    ~NormativeDriver() {}
    NormativeDriver(const NormativeDriver&) = delete;
    NormativeDriver& operator=(const NormativeDriver&) = delete;

    // Starts the timer; the first step is injected one interval later.
    void start();
    // Whether the trace was written.
    [[nodiscard]] bool written() const noexcept { return written_; }

    static constexpr std::chrono::milliseconds interval{250};

private:
    struct Tick final {
        NormativeDriver* driver{};
        void operator()() const;
    };
    void tick();

    NormativeScene& scene_;
    Window& window_;
    NormativeInjector injector_{};
    std::string output_path_{};
    std::function<void()> request_close_{};
    std::unique_ptr<Timer> timer_{};
    SubscriptionToken token_{};
    std::size_t next_step_{0};
    bool finished_{false};
    bool written_{false};
};

} // namespace gui_forms::tests
