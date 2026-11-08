#pragma once

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {
namespace detail {
struct ApplicationWindowState;
struct ApplicationRunState;
struct ApplicationHandleAccess;
}

class ApplicationWindows;

enum class ApplicationError : std::uint8_t {
    none,
    invalid_argument,
    wrong_thread,
    already_running,
    unsupported,
    backend_failure,
    callback_failure,
    // Application::shutdown has already run in this process.
    after_shutdown,
};

struct ApplicationResult final {
    ApplicationError error{ApplicationError::none};
    int native_exit_code{};
    std::size_t window_index{std::numeric_limits<std::size_t>::max()};
    std::exception_ptr callback_exception{};

    [[nodiscard]] bool accepted() const noexcept {
        const bool accepted = error == ApplicationError::none;
        return accepted;
    }
};

/// A weak, thread-affine handle to one native application window.
/// Requests after close or after Application::run return after_shutdown.
class ApplicationWindowHandle final {
public:
    ApplicationWindowHandle() noexcept = default;
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] HostServiceStatus request_close() const;
    [[nodiscard]] HostServiceStatus show() const;
    [[nodiscard]] HostServiceStatus hide() const;
    [[nodiscard]] HostServiceStatus toggle_full_screen() const;
    // UI-thread request; title is borrowed only for this call. Empty is allowed;
    // malformed UTF-8, embedded NUL and more than 65536 bytes are refused.
    // Acceptance is a native update request, not a compositor presentation receipt.
    [[nodiscard]] HostServiceStatus set_title(const std::string_view title) const;
    // UI-thread request that the native window's client area become
    // `client_size` logical units (1 to 32768 each way, at least the window's
    // minimum_size). Acceptance means the native request was submitted. The
    // new size reaches the model only through the host's resize event, as any
    // user resize does: never assume it has arrived when this returns (inside
    // a host callback the event waits until the callback returns, and X11
    // answers on a later event). The platform or window manager may constrain
    // it. Refused as unsupported while the window is full screen, maximized or
    // minimized.
    [[nodiscard]] HostServiceStatus request_client_size(Size client_size) const;
    // The window's identity in this run; invalid before ready, after close
    // and for a host without application windowing.
    [[nodiscard]] HostWindowId id() const noexcept;
    // The run's windowing (opening, destroying and finding windows). An
    // inactive handle when this handle is inactive.
    [[nodiscard]] ApplicationWindows windows() const noexcept;
    // The window's frame in desktop logical units (HostWindowing::frame;
    // capability window_frames).
    [[nodiscard]] HostWindowFrameResult frame() const;
    // Asks for a client rectangle in desktop logical units
    // (HostWindowing::request_frame). As with request_client_size, the
    // model and frame_changed follow only when the host reports the change.
    [[nodiscard]] HostServiceStatus request_frame(Rect content) const;
    // Orders the window above the run's other windows, never above other
    // applications' windows, without activating it (window_ordering).
    [[nodiscard]] HostServiceStatus bring_to_front() const;

private:
    friend struct detail::ApplicationHandleAccess;
    explicit ApplicationWindowHandle(
        std::weak_ptr<detail::ApplicationWindowState> state) noexcept;
    std::weak_ptr<detail::ApplicationWindowState> state_{};
};

struct ApplicationWindowOptions final {
    std::string title{"GUI.Forms Application"};
    Size initial_size{1120.0, 680.0};
    Size minimum_size{150.0, 150.0};
    bool initially_visible{true};
    bool hide_on_close{};
    bool minimizable{true};
    bool print_metrics_on_close{};
    // The window may be presented non-opaque (stage T5, owner decisions
    // T5-Q3 and T5-Q4): declared before the host creates the native window
    // (Window::declare_non_opaque). The backdrop itself is asked for through
    // Window::request_backdrop, and the window is opaque without it.
    bool non_opaque{};
    // Runs after this model has an attached host and services. Other windows
    // may not be ready yet. Keep a weak handle for native window requests.
    std::function<void(Window&, ApplicationWindowHandle)> ready{};
    // Published on the UI thread before ready. The supplied wake may be called
    // by workers while this window is alive; workers must stop before closed.
    // Wake schedules work only: it posts a message, writes a pipe or queues a
    // main-thread task, and returns. It takes no lock a caller could hold and
    // never calls dispatch_pending or anything else of the caller's, so a
    // worker may call it while holding its own lock. dispatch_pending executes
    // on the UI thread.
    std::function<void(std::function<void()>)> wake_ready{};
    std::function<void()> dispatch_pending{};
    std::function<void(HostCloseRequest&)> closing{};
    std::function<void()> closed{};
    // Published on the UI thread for every native move or resize of this
    // window once it is ready, in desktop logical units (capability
    // window_frames). The client size also reaches the model through the
    // host's resize event, as before.
    std::function<void(const HostWindowFrameEvent&)> frame_changed{};
};

struct ApplicationWindow final {
    std::string stable_id{};
    std::string owner_id{};
    std::unique_ptr<Window> model{};
    ApplicationWindowOptions options{};
    bool tool_window{};
    // A floating window (stage DK decisions 1, 10 and 11): owned by the
    // primary window (owner_id names it; tool_window stays false), no
    // taskbar button, minimized with it, above it only, moved with it and
    // clamped to the work area of the screen it lands on. A floating tool
    // also hides while the application is inactive; a floating document
    // does not. The policy decides when the window is on screen; show and
    // hide on its handle say whether it is wanted. See HostWindowKindTraits.
    HostWindowFloating floating{HostWindowFloating::none};
    // A native popup window (HostPopupPlacement): opened only during the
    // run, owned by `owner_id`, never a tool or floating window; none for
    // every other kind.
    std::optional<HostPopupPlacement> popup{};
};

struct ApplicationWindowOpenResult final {
    HostServiceStatus status{};
    // Active once the window is ready; inactive when the open was refused.
    ApplicationWindowHandle window{};
};

struct ApplicationWindowAtResult final {
    HostServiceStatus status{};
    // False when no window of the run is topmost at the point.
    bool found{};
    ApplicationWindowHandle window{};
};

/// A weak, thread-affine handle to the windowing of one running
/// application (stage DK decisions 6 and 7). Obtained from
/// ApplicationWindowHandle::windows(). Requests after the run ends return
/// after_shutdown; requests from another thread return wrong_thread.
class ApplicationWindows final {
public:
    ApplicationWindows() noexcept = default;
    [[nodiscard]] bool active() const noexcept;
    // The host's capabilities for this run; empty when inactive.
    [[nodiscard]] HostCapabilities capabilities() const;
    // Opens a window during the run (capability runtime_windows). The entry
    // is validated as Application::validate validates one entry, and it must
    // be owned by a live window of this run or be a tool window; at most
    // Application::maximum_windows windows are live at once. A refusal found
    // before the native request leaves `window` unchanged; once the native
    // request starts the entry is consumed, as Application::run consumes its
    // entries. The window's ready callback runs before this returns.
    [[nodiscard]] ApplicationWindowOpenResult open_window(ApplicationWindow&& window) const;
    // Closes and destroys a window this run opened, or an owned initial
    // window, without asking its closing handler (HostWindowing::
    // destroy_window). Its closed callback runs before this returns.
    [[nodiscard]] HostServiceStatus destroy_window(const ApplicationWindowHandle& window) const;
    // The pointer in desktop logical units (capability screen_pointer).
    [[nodiscard]] HostScreenPointResult pointer_position() const;
    // The run's topmost window at a desktop point (window_at_point).
    [[nodiscard]] ApplicationWindowAtResult window_at(DesktopLogicalPoint point) const;
    // Monitors in desktop logical units (monitor_geometry).
    [[nodiscard]] HostMonitorResult monitors() const;
    // Live windows of the run, including any not yet ready.
    [[nodiscard]] std::size_t window_count() const noexcept;

private:
    friend struct detail::ApplicationHandleAccess;
    explicit ApplicationWindows(std::weak_ptr<detail::ApplicationRunState> run) noexcept;
    std::weak_ptr<detail::ApplicationRunState> run_{};
};

/// Runs the selected native host through one portable C++ entry point.
/// Link GUIForms::Application for run/capabilities; validation and handles
/// are part of GUIForms::Core. Window models are consumed even on failure.
class Application final {
public:
    Application() = delete;
    static constexpr std::size_t maximum_windows = 64U;
    [[nodiscard]] static ApplicationResult validate(
        const std::vector<ApplicationWindow>& windows);
    [[nodiscard]] static HostCapabilities capabilities();
    // The application's one set of keyboard-command keys (owner decision
    // DK-44): `run` gives it to every window it runs, before the host
    // attaches, so the host seeds it once and a rebinding in any window
    // holds in all. Made on first use; lives as long as the process. UI
    // thread only.
    [[nodiscard]] static const std::shared_ptr<KeyBindings>& key_bindings();
    // The program's tooltip switch (owner, 2026-10-28): whether tooltips
    // show in every window whose own switch is unset
    // (Window::set_tool_tips_shown). On by default. A window reads it when
    // a tooltip would show, so a tooltip that already shows stays until it
    // closes. Any thread.
    static void set_tool_tips_shown(bool shown) noexcept;
    [[nodiscard]] static bool tool_tips_shown() noexcept;
    [[nodiscard]] static ApplicationResult run(
        std::unique_ptr<Window> window, ApplicationWindowOptions options = {});
    [[nodiscard]] static ApplicationResult run(
        std::vector<ApplicationWindow> windows);
    // Ends the rendering library once per process, after the last `run`: it
    // frees stx_skia's caches and singletons. Call it when the last `run` has
    // returned and every window, surface and font the program made is gone.
    // Refused with already_running during a run, and with wrong_thread on
    // macOS off the main thread; after it, `run` and `shutdown` return
    // after_shutdown. A process that ends without it, a crash included,
    // leaks nothing: the library holds only process memory.
    [[nodiscard]] static ApplicationResult shutdown();
};

} // namespace gui_forms
