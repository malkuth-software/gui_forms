#pragma once

#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window.hpp"

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
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
    // Application::close while windows of the application are alive.
    windows_alive,
};

// Why Application::validate (and so a run) refused its window list or an
// entry of it; none for a result that is not invalid_argument.
enum class ApplicationWindowRefusal : std::uint8_t {
    none,
    // The list is empty or longer than Application::maximum_windows.
    window_count,
    // The entry has no model.
    no_model,
    // The stable id, owner id or title is not UTF-8 without NUL within its
    // limit, the stable id is empty, or the entry owns itself.
    invalid_text,
    // initial_size is not finite or not within [1, 32768] logical units.
    invalid_initial_size,
    // minimum_size is not finite or not within [0, 32768] logical units.
    invalid_minimum_size,
    // initial_size is below minimum_size on a side (ApplicationResult's
    // minimum_size and initial_size give both).
    below_minimum_size,
    // A popup entry, which opens only during a run
    // (Window::open_native_popup), or one with an invalid owner, kind,
    // focus or origin.
    invalid_popup,
    // A floating entry without an owner, or one marked a tool window.
    invalid_floating,
    // The primary window starts hidden or hides on close.
    hidden_primary,
    // An earlier entry has the same stable id.
    duplicate_stable_id,
    // An owner is not listed, the owners form a cycle, or a floating
    // entry's owner is not the primary window.
    invalid_owner,
    // The list has no primary window, or more than one.
    primary_count,
};

// A short English sentence for `refusal`, for logs and the C ABI's failure
// message. Static storage; empty for none.
[[nodiscard]] std::string_view application_window_refusal_text(ApplicationWindowRefusal refusal) noexcept;

struct ApplicationResult final {
    ApplicationError error{ApplicationError::none};
    int native_exit_code{};
    std::size_t window_index{std::numeric_limits<std::size_t>::max()};
    std::exception_ptr callback_exception{};
    // With invalid_argument from validation: why; for below_minimum_size,
    // the refused entry's minimum and initial client sizes.
    ApplicationWindowRefusal refusal{ApplicationWindowRefusal::none};
    Size minimum_size{};
    Size initial_size{};

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
    // The smallest client size the user may resize the window to, in
    // logical units; a side of 0 sets no minimum on that side beyond the
    // host's own, and the default {0, 0} sets none (Windows Forms'
    // MinimumSize, empty by default). initial_size must not be below it
    // (ApplicationWindowRefusal::below_minimum_size).
    Size minimum_size{};
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

// What ApplicationWindows::show_modal gives back.
struct ApplicationModalResult final {
    // The open's or the nested loop's refusal; accepted once the window
    // has closed (or the run quit during the loop).
    HostServiceStatus status{};
    // The window's dialog result when it closed: the last value its model
    // (SurfaceRoot::dialog_result) had, or cancel when it closed without
    // one (its close box, a destroy_window), as Form.ShowDialog reports.
    // none when the window never opened.
    DialogResult dialog_result{DialogResult::none};
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
    // Opens `window` as open_window does and runs a nested event loop until
    // it closes (HostWindowing::run_modal), as Form.ShowDialog does: the
    // run's other windows refuse user input meanwhile, while posted work,
    // timers and repaint continue everywhere. The entry must name an owner
    // (owner_id) and must not be a popup or floating window
    // (invalid_argument). A dialog result other than none set on the
    // window's model while it is open asks the window to close (its
    // closing handler may cancel; the window then stays and the result
    // stays set). Returns after the window's closed callback. Unsupported
    // where the host has no nested loop (supports_modal_windows), refused
    // with modal_limit past HostWindowing::maximum_modal_depth.
    [[nodiscard]] ApplicationModalResult show_modal(ApplicationWindow&& window) const;
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

// The application's theme (APP_WINDOW_RULES_DESIGN A.2): the base sheet,
// then the program's packages in order, compiled once by
// Application::create (compile_theme_packages).
struct ApplicationThemeConfig final {
    // The process default's base (default_theme_base) unless chosen.
    ThemeBase base{default_theme_base};
    // The program's packages, in compile order: a later one overrides an
    // earlier one. At most maximum_theme_packages, with distinct names.
    std::vector<ThemePackage> packages{};
    // The installed packages a package's `@import` may name.
    ThemeLibrary library{};
    // The application's own control types, beside the stock ones, that a
    // selector may name. Borrowed for the call to create only.
    std::span<const ControlAnatomy> controls{};
    // The effects the packages' `effect` may name (EP-3), usually
    // built_in_effect_registry(); a name it does not hold refuses the theme
    // (E417). Borrowed for the call to create only. Null, only the names'
    // form is checked.
    const EffectRegistry* effects{};
};

// Everything an application is made with. Move-only (it holds a library).
struct ApplicationConfig final {
    ApplicationThemeConfig theme{};
    // The presentation defaults every window of the application starts
    // with, until the host reports its own; GUI.Forms' defaults when unset.
    std::optional<PresentationSettings> presentation{};
    // The quotas of the application's shared image registry.
    ImageRegistryLimits image_limits{};
};

enum class ApplicationCreateError : std::uint8_t {
    none,
    // An application is current already; there is one at a time.
    already_exists,
    // The theme was refused; ApplicationCreateResult::findings say why.
    theme_refused,
};

class Application;

// What Application::create gave: an application owned by the caller, with
// the theme compile's warnings in `findings`, or a refusal with nothing
// made and, for theme_refused, the compile's diagnostics.
struct ApplicationCreateResult final {
    ApplicationCreateError error{ApplicationCreateError::none};
    std::unique_ptr<Application> application{};
    std::vector<ThemeCompileDiagnostic> findings{};

    [[nodiscard]] explicit operator bool() const noexcept {
        const bool created = application != nullptr;
        return created;
    }
};

/// The application (APP_WINDOW_RULES_DESIGN Part A): made once by
/// `create`, owned by the program, and the current application while it
/// lives. It owns what every window shares: the resources (the image
/// registry and the theme, compiled once at create), the presentation
/// defaults, the keyboard-command keys (DK-44) and the tooltip switch.
///
/// A top-level Window made on the application's thread while it is current
/// joins it: it shares its resources (unless given its own) and starts from
/// its presentation. Popups, dialogs, menus and tooltips open from a
/// window and take its configuration. With no current application (tests,
/// headless use) a window makes its own resources with the built-in theme.
///
/// Link GUIForms::Application for `run` and `capabilities`; the rest is
/// part of GUIForms::Core. Window models given to `run` are consumed even
/// on failure. UI thread: every member but the tooltip switch and current().
class Application final {
public:
    static constexpr std::size_t maximum_windows = 64U;
    // Makes the application and registers it as current: compiles its
    // theme (the base, then the packages) into fresh resources. Refused,
    // making nothing, while another application is current
    // (already_exists) or when the compile refuses the theme
    // (theme_refused: also for duplicate names or too many packages).
    [[nodiscard]] static ApplicationCreateResult create(ApplicationConfig config);
    // The current application, or null. Read on any thread; use it only on
    // the application's own thread.
    [[nodiscard]] static Application* current() noexcept;
    // Unregisters the application. Windows still joined to it are reported
    // on stderr (a destructor cannot refuse) and left: they keep the theme
    // and resources they share, and read nothing more from it.
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Ends the application's time as current, before its destruction:
    // refused with windows_alive while a window joined to it lives, then
    // changing nothing. After it, `create` may make another application and
    // windows made from then on do not join this one. Calling it again
    // accepts and does nothing.
    [[nodiscard]] ApplicationResult close();
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    // The windows joined to the application and alive.
    [[nodiscard]] std::size_t window_count() const noexcept { return window_count_; }
    // The resources every joined window shares unless made with its own.
    [[nodiscard]] const std::shared_ptr<ApplicationResources>& resources() const noexcept { return resources_; }
    // The presentation every joined window takes as its own: the
    // configured defaults, then the host's reports.
    [[nodiscard]] const PresentationSettings& presentation_settings() const noexcept { return presentation_; }
    // Changes it and gives it to every joined window, in the order they
    // joined; each keeps its overrides (Window::set_presentation_overrides)
    // and raises presentation_changed when its settings in force change. A
    // window a handler destroys meanwhile is skipped. std::invalid_argument
    // as validate_presentation_settings, changing nothing. UI thread.
    void set_presentation_settings(PresentationSettings settings);
    // Replaces the program's theme of every joined window: the shared
    // resources' (ApplicationResources::set_theme, which each window sharing
    // them follows), then each joined window's own resources, in joining
    // order. Null and the refusals of ApplicationResources::set_theme are
    // std::invalid_argument: a refusal by the shared resources changes
    // nothing; one by a window's own resources leaves those before it
    // changed. UI thread.
    void set_theme(std::shared_ptr<const Theme> theme);

    [[nodiscard]] static ApplicationResult validate(
        const std::vector<ApplicationWindow>& windows);
    [[nodiscard]] static HostCapabilities capabilities();
    // The application's one set of keyboard-command keys (owner decision
    // DK-44): `run` gives it to every window it runs, before the host
    // attaches, so the host seeds it once and a rebinding in any window
    // holds in all.
    [[nodiscard]] const std::shared_ptr<KeyBindings>& key_bindings() const noexcept { return key_bindings_; }
    // The program's tooltip switch (owner, 2026-10-28): whether tooltips
    // show in every joined window whose own switch is unset
    // (Window::set_tool_tips_shown). On by default. A window reads it when
    // a tooltip would show, so a tooltip that already shows stays until it
    // closes. Any thread.
    void set_tool_tips_shown(bool shown) noexcept;
    [[nodiscard]] bool tool_tips_shown() const noexcept;
    [[nodiscard]] ApplicationResult run(
        std::unique_ptr<Window> window, ApplicationWindowOptions options = {});
    [[nodiscard]] ApplicationResult run(
        std::vector<ApplicationWindow> windows);
    // Ends the rendering library once per process, after the last `run`: it
    // frees stx_skia's caches and singletons. Call it when the last `run` has
    // returned and every window, surface and font the program made is gone.
    // Refused with already_running during a run, and with wrong_thread on
    // macOS off the main thread; after it, `run` and `shutdown` return
    // after_shutdown. A process that ends without it, a crash included,
    // leaks nothing: the library holds only process memory.
    [[nodiscard]] static ApplicationResult shutdown();

private:
    friend class Window;
    Application(std::shared_ptr<ApplicationResources> resources, PresentationSettings presentation);
    // Called by Window: joins and leaves the application's window list
    // (intrusive, O(1), allocating nothing). UI thread.
    void join(Window& window) noexcept;
    void leave(Window& window) noexcept;

    std::shared_ptr<ApplicationResources> resources_{};
    PresentationSettings presentation_{};
    std::shared_ptr<KeyBindings> key_bindings_{};
    // Shared with joined windows, which read it from their threads.
    std::shared_ptr<std::atomic<bool>> tool_tips_shown_{};
    // The joined windows, oldest first.
    Window* first_window_{};
    Window* last_window_{};
    std::size_t window_count_{};
    bool closed_{};
};

} // namespace gui_forms
