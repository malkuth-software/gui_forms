#pragma once
#include "gui_forms/application.hpp"

#include <stxlib/threads.hpp>

namespace gui_forms::detail {
struct ApplicationRunState;

struct ApplicationWindowState final {
    stxlib::ThreadIdentity thread{stxlib::current_thread()};
    bool ready{};
    bool closed{};
    bool can_hide{};
    // A passive popup window (a tooltip, a Splash, a menu panel that never
    // takes focus): never a dialog, so the automation does not take one for
    // the window a modal call left free (find_open_dialog).
    bool passive_popup{};
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
    // Gives the window's model its last focused control back
    // (SurfaceRoot::restore_focus), as a modal window over it ends.
    std::function<void()> restore_focus{};
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
    stxlib::ThreadIdentity thread{stxlib::current_thread()};
    bool closed{};
    // The running application's keyboard-command keys (DK-44), given to
    // every window the run opens.
    std::shared_ptr<KeyBindings> key_bindings{};
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
    // Whether the run's last popup or Splash window that the host had
    // reported to was granted a clear backdrop (note_popup_backdrop); none
    // before one was. A host can promise translucent popups and still
    // refuse one its alpha (Wine's DWM stubs refuse blur behind), and the
    // content's shadow follows what the window really shows.
    std::optional<bool> popup_alpha{};
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
GUI_FORMS_API void attach_application_window(SurfaceRoot& model, const std::shared_ptr<ApplicationWindowState>& state) noexcept;

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
// Why one entry is refused, as Application::validate checks each entry,
// without the run-wide rules (one primary, ownership order); none when it
// is valid.
[[nodiscard]] ApplicationWindowRefusal window_entry_refusal(const ApplicationWindow& entry);
// window_entry_refusal(entry) == none.
[[nodiscard]] bool valid_window_entry(const ApplicationWindow& entry);
// Whether an entry asks for the run's one primary window
// (HostWindowKind::primary): no owner and not a tool window.
[[nodiscard]] bool primary_window_entry(const ApplicationWindow& entry) noexcept;
// Ends a window state's part in the run: it reads closed and not ready, and
// every native operation it held is released, so its handles report
// after_shutdown. Its stable id, identity and run stay, so
// unregister_run_window still finds it.
void revoke_window_state(ApplicationWindowState& state) noexcept;

// The window of a host's single-window entry (host::run_macos, run_windows,
// run_linux), which runs no Application::run: it joins the automation as an
// application window does (session_directory.hpp, the window-ready hook), so
// a program that calls its host directly serves --mcp and --mcp-script too.
// Its state is outside any run and not attached to the model, which opens
// its popups as it did before; its handle offers request_close (the
// automation's close at the end of its input) and reports the rest
// unsupported. Made, used and destroyed on the host's UI thread; destroyed
// after the run loop ends, which revokes the state.
// Gives `window` the title its host opens it with as its text when the
// program wrote none, so the program and the automation read the title the
// user sees (an empty title is left alone). The host's UI thread, before the
// window attaches to its host.
void adopt_opening_title(Window& window, std::string_view title);

class DirectRunWindow final {
public:
    using HostReady = std::function<void(std::function<void()> wake, std::function<void()> request_close,
                                         std::function<HostDialogResult(const HostDialogRequest&)> show_dialog,
                                         std::function<HostClipboardTextResult()> read_clipboard_text,
                                         std::function<HostServiceStatus(std::string_view)> write_clipboard_text)>;
    DirectRunWindow();
    ~DirectRunWindow();
    DirectRunWindow(const DirectRunWindow&) = delete;
    DirectRunWindow& operator=(const DirectRunWindow&) = delete;
    // The host's host_ready option with the join after it: `inner` (empty
    // for none) runs first, then the window joins under `stable_id` with
    // the host's request_close.
    [[nodiscard]] HostReady wrap(SurfaceRoot& model, std::string stable_id, HostReady inner);

private:
    std::shared_ptr<ApplicationWindowState> state_{};
};
} // namespace gui_forms::detail
