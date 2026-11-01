// Present profile workload (SK2). Measurement only: it selects nothing and
// claims nothing about speed by itself.
//
// One window of the native host, through Application::run, repaints its whole
// client area every frame; the window's own instrumentation reports what the
// host spent presenting each frame. It exists to compare the present path
// before and after a change to the framebuffer's pixel format (SK2: BGRA8
// premultiplied everywhere), so it draws no text and no images: the paint is
// one fill whose colour changes every frame, and every frame copies every
// pixel of the window.
//
// After warm_up_frames painted frames it takes the window's metrics, paints
// measured_frames more and takes them again. Its result is one JSON line, on
// standard output and in <output> when given, with the deltas per presented
// frame:
//
// - present_ns: Window::notify_presented's duration (painting, composing and
//   the native copy);
// - screen_copy_ns: the host's conversion and native copy alone
//   (MetricsSnapshot::screen_copy_nanoseconds);
// - wall_ns: the time between frames, which a host's frame pacing bounds.
//
// It exits 0 with a result, 2 when the host presented fewer frames than it
// painted or no frames at all within time_limit, and 1 when the application
// could not run.
//
// Usage: gui_forms_present_profile_workload [output]

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using gui_forms::AnimationTickResult;
using gui_forms::AnimationTimer;
using gui_forms::Application;
using gui_forms::ApplicationResult;
using gui_forms::ApplicationWindowHandle;
using gui_forms::ApplicationWindowOptions;
using gui_forms::Color;
using gui_forms::Control;
using gui_forms::Dirty;
using gui_forms::FrameTime;
using gui_forms::MetricsSnapshot;
using gui_forms::Painter;
using gui_forms::Rect;
using gui_forms::Size;
using gui_forms::StableId;
using gui_forms::Window;
using Clock = std::chrono::steady_clock;

// Small enough for the CI runners' screens (Windows 1024 x 768, the Linux
// jobs' Xvfb 1280 x 1024).
constexpr Size client_size{800.0, 560.0};
constexpr std::uint64_t warm_up_frames = 30U;
constexpr std::uint64_t measured_frames = 300U;
constexpr std::chrono::seconds time_limit{60};
constexpr std::chrono::milliseconds frame_interval{16};

#if defined(_WIN32)
constexpr std::string_view host_name = "windows";
#elif defined(__APPLE__)
constexpr std::string_view host_name = "macos";
#else
constexpr std::string_view host_name = "linux";
#endif

// The run's state, shared by the probe control and the window's callbacks.
struct PresentRun final {
    ApplicationWindowHandle handle{};
    std::uint64_t painted{};
    std::optional<MetricsSnapshot> first{};
    std::optional<MetricsSnapshot> last{};
    Clock::time_point started{};
    Clock::time_point first_time{};
    Clock::time_point last_time{};
    bool close_requested{};
    bool timed_out{};
};

// Fills its whole area every frame, in a colour that changes every frame so
// that no layer can skip the copy.
class PresentProbe final : public Control {
public:
    PresentProbe(StableId id, PresentRun& run) : Control(std::move(id)), run_(&run) {}

    void start() {
        Window* const owner = window();
        if (owner == nullptr) return;
        (*run_).started = Clock::now();
        // ADR-016: a paint-gated animation timer, as an animated control
        // would use; each tick damages the whole probe.
        const std::shared_ptr<PresentProbe> self =
            std::static_pointer_cast<PresentProbe>(shared_from_this());
        frames_.start(*this, frame_interval, FrameTick{self});
    }

    AnimationTickResult advance() {
        PresentRun& run = *run_;
        const bool finished = run.last.has_value();
        const bool late = Clock::now() - run.started > time_limit;
        if (finished || late) {
            if (!run.close_requested) {
                run.close_requested = true;
                run.timed_out = !finished;
                frames_.stop();
                static_cast<void>(run.handle.request_close());
            }
            return AnimationTickResult::unchanged;
        }
        invalidate(Dirty::paint);
        return AnimationTickResult::damaged;
    }

    void on_paint(Painter& painter, Rect damage) override {
        frames_.note_painted();
        PresentRun& run = *run_;
        Window* const owner = window();
        // Presents up to the previous frame are in the metrics now.
        if (owner != nullptr && run.painted == warm_up_frames) {
            run.first = (*owner).metrics().snapshot();
            run.first_time = Clock::now();
        }
        if (owner != nullptr && run.painted == warm_up_frames + measured_frames && !run.last) {
            run.last = (*owner).metrics().snapshot();
            run.last_time = Clock::now();
        }
        const std::uint8_t shade = static_cast<std::uint8_t>(run.painted % 200U + 40U);
        painter.fill_rect(damage, Color::rgba(shade, static_cast<std::uint8_t>(255U - shade), 96U));
        ++run.painted;
    }

private:
    PresentRun* run_{};
    // The timer's target. Observes the probe.
    struct FrameTick final {
        std::weak_ptr<PresentProbe> probe;
        AnimationTickResult operator()(FrameTime now) const {
            static_cast<void>(now);
            const std::shared_ptr<PresentProbe> live = probe.lock();
            const AnimationTickResult result =
                live ? (*live).advance() : AnimationTickResult::unchanged;
            return result;
        }
    };

    AnimationTimer frames_;
};

// The window's ready callback: keeps its handle and scale, then starts the
// frames.
struct ProbeReady final {
    PresentRun* run{};
    double* scale{};
    PresentProbe* probe{};
    void operator()(Window& ready, const ApplicationWindowHandle handle) const {
        (*run).handle = handle;
        *scale = ready.scale();
        (*probe).start();
    }
};

[[nodiscard]] std::string result_line(const PresentRun& run, const double scale) {
    const MetricsSnapshot& first = *run.first;
    const MetricsSnapshot& last = *run.last;
    const std::uint64_t frames = last.frames_presented - first.frames_presented;
    const std::uint64_t pixels = last.retained_pixels_presented - first.retained_pixels_presented;
    const std::uint64_t present = last.present_duration_nanoseconds - first.present_duration_nanoseconds;
    const std::uint64_t copy = last.screen_copy_nanoseconds - first.screen_copy_nanoseconds;
    const std::uint64_t wall = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(run.last_time - run.first_time).count());
    std::ostringstream line;
    line << "{\"workload\":\"present\",\"host\":\"" << host_name << "\",\"client_width\":" << client_size.width
         << ",\"client_height\":" << client_size.height << ",\"scale\":" << scale
         << ",\"frames_painted\":" << measured_frames << ",\"frames_presented\":" << frames;
    if (frames != 0U) {
        line << ",\"pixels_per_frame\":" << pixels / frames << ",\"present_ns\":" << present / frames
             << ",\"screen_copy_ns\":" << copy / frames << ",\"wall_ns\":" << wall / frames;
    }
    line << '}';
    return line.str();
}

} // namespace

int main(const int argc, char** const argv) {
    PresentRun run{};
    const std::shared_ptr<PresentProbe> probe =
        gui_forms::make_control<PresentProbe>(StableId("present.profile.root"), run);
    std::unique_ptr<Window> window = std::make_unique<Window>(probe, client_size);
    double scale = 0.0;
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms present profile";
    options.initial_size = client_size;
    options.minimizable = false;
    options.ready = ProbeReady{&run, &scale, probe.get()};
    const ApplicationResult result = Application::run(std::move(window), std::move(options));
    static_cast<void>(Application::shutdown());
    if (!result.accepted()) {
        std::cerr << "present profile: the application did not run\n";
        return 1;
    }
    if (run.timed_out || !run.first || !run.last) {
        std::cerr << "present profile: " << run.painted << " frames painted within the time limit\n";
        return 2;
    }
    const std::string line = result_line(run, scale);
    std::cout << line << '\n';
    if (argc > 1) {
        std::ofstream output(argv[1]);
        output << line << '\n';
        if (!output) {
            std::cerr << "present profile: cannot write " << argv[1] << '\n';
            return 1;
        }
    }
    if ((*run.last).frames_presented - (*run.first).frames_presented < measured_frames) return 2;
    return 0;
}
