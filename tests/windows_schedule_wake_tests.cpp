// The Windows host's schedule wake seam (Windows handoff, Group 1 item 1;
// docs/user/painting-and-damage.md, "Waking the host"):
// - a Timer restarted from another window's callback fires on time with no
//   other input reaching its own window: the restart posts
//   schedule_wake_message, whose handler polls the model and arms the
//   window's wake for the new deadline;
// - requests made before the window's next poll share one wake;
// - a request made while the window paints (a paint deadline set from
//   on_paint) is answered after the paint without other input.
// Before the seam, the Windows host re-armed only at the end of its own
// damage collections, so the restarted Timer waited for the previous,
// distant deadline.
// The windows are hidden popups driven message by message; nothing takes
// the foreground.
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/timer/timer/timer.hpp"
#include "gui_forms/window.hpp"
#include "../src/host/windows/application/windows_host.cpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace gui_forms::host {
namespace {

constexpr Size scene_size{160.0, 100.0};
// The restarted Timer's interval, and the distant one it replaces: without
// the seam the window would wake only at the distant deadline.
constexpr std::chrono::milliseconds restarted_interval{40};
constexpr std::chrono::milliseconds distant_interval{10'000};
// How long the window's own loop may take to deliver the tick. Shadow's
// timings are not evidence (Remote Desktop); the bound only separates "on
// time" from "at the distant deadline".
constexpr std::chrono::milliseconds tick_bound{2'000};

// Counts the Timer's ticks.
class CountingTick final {
public:
    explicit CountingTick(std::uint64_t& ticks) : ticks_(&ticks) {}

    void operator()() const { ++(*ticks_); }

private:
    std::uint64_t* ticks_;
};

// Posted work of the second window: restarts the first window's Timer at
// the short interval, outside any frame poll of the first window.
class RestartTimer final {
public:
    explicit RestartTimer(Timer& timer) : timer_(&timer) {}

    void operator()() const {
        (*timer_).stop();
        (*timer_).set_interval(restarted_interval);
        (*timer_).start();
    }

private:
    Timer* timer_;
};

// A control that sets a paint deadline from inside its own paint, once, as
// deferred work of a paint does.
class DeadlineFromPaint final : public Control {
public:
    explicit DeadlineFromPaint(StableId id) : Control(std::move(id)) {}

    void arm(Window& window) noexcept {
        window_ = &window;
        armed_ = true;
    }

    std::uint64_t paints{};

protected:
    void on_paint(Painter& painter, Rect) override {
        painter.fill_rect(client_rectangle(), Color::rgba(240, 240, 240));
        ++paints;
        if (!armed_ || window_ == nullptr) return;
        armed_ = false;
        const FrameTime due = FrameClock::now() + restarted_interval;
        static_cast<void>((*window_).schedule_paint(shared_from_this(), due));
    }

private:
    Window* window_{};
    bool armed_{};
};

struct ScheduleWakeHostFixture final {
    static void require(bool condition, const std::string& message) {
        if (!condition) throw std::runtime_error(message);
    }

    // One hidden host window over its own model.
    struct HostedWindow final {
        std::unique_ptr<WindowsHostState> state;
        Window* model{};
        HWND window{};
    };

    static HostedWindow open(const ATOM native_class, const std::shared_ptr<Control>& root) {
        std::unique_ptr<Window> owned = std::make_unique<Window>(root, scene_size);
        HostedWindow hosted{};
        hosted.model = owned.get();
        WindowsHostOptions options{};
        options.quit_thread_on_close = false;
        options.print_metrics_on_close = false;
        options.scale_override = 1.0;
        hosted.state = std::make_unique<WindowsHostState>(std::move(owned), std::move(options));
        hosted.window = CreateWindowExW(WS_EX_NOACTIVATE,
                                        reinterpret_cast<LPCWSTR>(static_cast<ULONG_PTR>(native_class)),
                                        L"Schedule wake fixture", WS_POPUP, 0, 0,
                                        static_cast<int>(scene_size.width), static_cast<int>(scene_size.height),
                                        nullptr, nullptr, GetModuleHandleW(nullptr), hosted.state.get());
        require(hosted.window != nullptr, "fixture native creation");
        require((*hosted.state).initialize(hosted.window), "fixture host initialization");
        return hosted;
    }

    // Dispatches every message queued for `window` now, without waiting.
    static void drain(HWND window) {
        MSG message{};
        while (PeekMessageW(&message, window, 0U, 0U, PM_REMOVE) != FALSE) {
            DispatchMessageW(&message);
        }
    }

    // The fixture's windows are hidden, so the system sends them no
    // WM_PAINT; a pending update region is painted as it would be for a
    // visible window.
    static void paint_pending(HWND window) {
        if (GetUpdateRect(window, nullptr, FALSE) != FALSE) SendMessageW(window, WM_PAINT, 0, 0);
    }

    // True while a schedule wake is queued for `window`.
    static bool schedule_wake_queued(HWND window) {
        MSG message{};
        return PeekMessageW(&message, window, schedule_wake_message, schedule_wake_message, PM_NOREMOVE) != FALSE;
    }

    // The window's own loop, as WindowsApplicationRun::turn runs it but for
    // this window alone: waits on its scheduler clock and its messages
    // until `done` reaches `target` or the bound passes.
    static void run_until(HostedWindow& hosted, const std::uint64_t& done, const std::uint64_t target) {
        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + tick_bound;
        while (done < target) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if (now >= deadline) return;
            const std::chrono::milliseconds::rep remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            HANDLE clock = (*hosted.state).scheduler_clock();
            const DWORD clock_count = clock != nullptr ? 1U : 0U;
            const DWORD wait = MsgWaitForMultipleObjectsEx(clock_count, clock != nullptr ? &clock : nullptr,
                                                           static_cast<DWORD>(remaining), QS_ALLINPUT,
                                                           MWMO_INPUTAVAILABLE);
            require(wait != WAIT_FAILED, "the window's loop waits");
            if (clock_count == 1U && wait == WAIT_OBJECT_0) (*hosted.state).scheduler_clock_signalled();
            drain(hosted.window);
            paint_pending(hosted.window);
        }
    }

    static void timer_restarted_from_another_window(const ATOM native_class) {
        const std::shared_ptr<Panel> root_a = make_control<Panel>(StableId("schedule-wake.a"));
        const std::shared_ptr<Panel> root_b = make_control<Panel>(StableId("schedule-wake.b"));
        HostedWindow a = open(native_class, root_a);
        HostedWindow b = open(native_class, root_b);
        drain(a.window);
        drain(b.window);

        // A's Timer runs at a distant interval; A's wake is armed for it.
        std::uint64_t ticks = 0U;
        Timer timer(*a.model, distant_interval);
        const SubscriptionToken subscription = timer.tick().subscribe(CountingTick{ticks});
        timer.start();
        require(schedule_wake_queued(a.window), "a Timer started outside a poll posts a schedule wake");
        drain(a.window);
        require(!schedule_wake_queued(a.window) && (*a.state).schedule_wakes_handled_ >= 1U,
                "the window's wake is armed for the distant deadline");

        // B's posted work restarts A's Timer. Only B's messages run.
        const std::uint64_t posted_before = (*a.state).schedule_wakes_posted_;
        static_cast<void>((*b.model).begin_invoke(RestartTimer{timer}));
        drain(b.window);
        require(timer.enabled() && timer.interval() == restarted_interval, "B's work restarted A's Timer");
        require((*a.state).schedule_wakes_posted_ == posted_before + 1U,
                "the restart posts one schedule wake (stop and start share it)");
        require(schedule_wake_queued(a.window), "the schedule wake waits in A's queue");

        // A's own loop, with no other input: the wake re-arms the clock and
        // the Timer ticks at the restarted interval.
        run_until(a, ticks, 1U);
        require(ticks >= 1U, "the restarted Timer ticks on time, not at the distant deadline");
        timer.stop();

        DestroyWindow(b.window);
        DestroyWindow(a.window);
    }

    static void deadline_set_while_painting(const ATOM native_class) {
        const std::shared_ptr<DeadlineFromPaint> root =
            make_control<DeadlineFromPaint>(StableId("schedule-wake.paint"));
        HostedWindow hosted = open(native_class, root);
        InvalidateRect(hosted.window, nullptr, FALSE);
        SendMessageW(hosted.window, WM_PAINT, 0, 0);
        drain(hosted.window);
        const std::uint64_t first_paints = (*root).paints;
        require(first_paints >= 1U, "the window painted");

        // The next paint sets a deadline from on_paint, after the host armed
        // its wake: the seam posts, and the deadline repaints by itself.
        // Only the render wake and the paint run here, so the posted
        // schedule wake is still queued after the paint.
        (*root).arm(*hosted.model);
        (*root).invalidate(Dirty::paint);
        MSG render{};
        while (PeekMessageW(&render, hosted.window, render_dispatch_message, render_dispatch_message, PM_REMOVE) !=
               FALSE) {
            DispatchMessageW(&render);
        }
        paint_pending(hosted.window);
        require((*root).paints == first_paints + 1U, "the armed paint ran");
        require(schedule_wake_queued(hosted.window), "a deadline set while painting posts a schedule wake");
        run_until(hosted, (*root).paints, first_paints + 2U);
        require((*root).paints >= first_paints + 2U, "the paint deadline repaints with no other input");

        DestroyWindow(hosted.window);
    }

    static void run() {
        WNDCLASSEXW native_class{};
        native_class.cbSize = sizeof(native_class);
        native_class.lpfnWndProc = window_procedure;
        native_class.hInstance = GetModuleHandleW(nullptr);
        native_class.lpszClassName = L"GUIForms.ScheduleWakeFixture.v1";
        const ATOM registered_class = RegisterClassExW(&native_class);
        require(registered_class != 0, "fixture class registration");
        timer_restarted_from_another_window(registered_class);
        deadline_set_while_painting(registered_class);
        require(UnregisterClassW(native_class.lpszClassName, native_class.hInstance) != FALSE,
                "fixture class release");
    }
};

} // namespace
} // namespace gui_forms::host

int main() {
    try {
        gui_forms::host::ScheduleWakeHostFixture::run();
    } catch (const std::exception& error) {
        std::cerr << "windows schedule wake failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "windows schedule wake tests passed\n";
    return EXIT_SUCCESS;
}
