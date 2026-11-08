#pragma once
#include "gui_forms/application.hpp"
#include <thread>

namespace gui_forms::detail {
struct ApplicationRunState;

struct ApplicationWindowState final {
    std::thread::id thread{std::this_thread::get_id()};
    bool ready{};
    bool closed{};
    bool can_hide{};
    // The window's stable id and, once it is ready, its identity in the run.
    std::string stable_id{};
    HostWindowId id{};
    // The run this window belongs to; empty for a state made outside a run.
    std::weak_ptr<ApplicationRunState> run{};
    std::function<void()> request_close{};
    std::function<void()> show{};
    std::function<void()> hide{};
    std::function<void()> toggle_full_screen{};
    std::function<HostServiceStatus(std::string_view)> set_title{};
    std::function<HostServiceStatus(Size)> request_client_size{};
    // Invoked by the run's frame router for this window's frame reports.
    std::function<void(const HostWindowFrameEvent&)> frame_changed{};
};

// A window of the run that has reached ready, by identity.
struct ApplicationRunWindow final {
    HostWindowId id{};
    std::weak_ptr<ApplicationWindowState> state{};
};

// The windowing of one run, shared by the run's handles (weakly) and owned
// by the code that runs the adapter (Application::run or a headless
// application). Only the UI thread reads or changes it.
struct ApplicationRunState final {
    std::thread::id thread{std::this_thread::get_id()};
    bool closed{};
    // Borrowed from the adapter while the run lives; cleared before the
    // adapter destroys it.
    HostWindowing* windowing{};
    // Opens a window during the run; installed with the windowing.
    std::function<ApplicationWindowOpenResult(ApplicationWindow&&)> open_window{};
    // Ready windows in readiness order. Bounded by the windowing table
    // (HostWindowing::maximum_windows); entries are removed when a window
    // closes.
    std::vector<ApplicationRunWindow> windows{};
    // The router's subscription to HostWindowing::frame_changed.
    SubscriptionToken frame_subscription{};
};

struct ApplicationHandleAccess final {
    [[nodiscard]] static ApplicationWindowHandle make(
        const std::shared_ptr<ApplicationWindowState>& state) noexcept {
        const ApplicationWindowHandle handle{state};
        return handle;
    }
    [[nodiscard]] static ApplicationWindows make_windows(
        const std::shared_ptr<ApplicationRunState>& run) noexcept {
        const ApplicationWindows windows{run};
        return windows;
    }
    [[nodiscard]] static std::shared_ptr<ApplicationWindowState> state(
        const ApplicationWindowHandle& handle) noexcept {
        std::shared_ptr<ApplicationWindowState> result = handle.state_.lock();
        return result;
    }
};

// Run bookkeeping shared by every adapter, defined in application.cpp.

// Gives a ready window's model its state in the run, so the model can open
// native popups owned by it (Window::open_native_popup). The model
// keeps only a weak reference. Called by every adapter when the window
// becomes ready, before its ready callback.
void attach_application_window(SurfaceRoot& model, const std::shared_ptr<ApplicationWindowState>& state) noexcept;

// ApplicationWindowHandle::show and hide for a floating window: the request
// goes to the run's floating policy (HostWindowing::set_visible), which
// decides when the window is on screen.
struct PolicyVisibility final {
    std::weak_ptr<ApplicationWindowState> state{};
    bool visible{};
    void operator()() const;
};

// Connects `run` to `windowing`: the frame router subscribes to its
// frame_changed, and `open_window` becomes the run's open operation.
void attach_run_windowing(
    const std::shared_ptr<ApplicationRunState>& run, HostWindowing& windowing,
    std::function<ApplicationWindowOpenResult(ApplicationWindow&&)> open_window);
// The adapter is about to destroy the windowing, or the run has ended:
// every handle of the run stops reaching it.
void detach_run_windowing(ApplicationRunState& run) noexcept;
// Called as a window becomes ready: resolves its identity through the
// run's windowing (by stable id) and records it in the run.
void register_run_window(const std::shared_ptr<ApplicationWindowState>& state);
// Called as a window closes: removes it from the run.
void unregister_run_window(ApplicationWindowState& state) noexcept;
// Validates one entry as Application::validate validates each entry,
// without the run-wide rules (one primary, ownership order).
[[nodiscard]] bool valid_window_entry(const ApplicationWindow& entry);
// Whether an entry asks for the run's one primary window
// (HostWindowKind::primary): no owner and not a tool window.
[[nodiscard]] bool primary_window_entry(const ApplicationWindow& entry) noexcept;
// Ends a window state's part in the run: it reads closed and not ready, and
// every native operation it held is released, so its handles report
// after_shutdown. Its stable id, identity and run stay, so
// unregister_run_window still finds it.
void revoke_window_state(ApplicationWindowState& state) noexcept;
} // namespace gui_forms::detail
