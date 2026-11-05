#include "gui_forms/application.hpp"
#include "../../core/application/application_state.hpp"

#include <atomic>
#include <utility>

#if defined(GUI_FORMS_APPLICATION_MACOS)
#include "gui_forms/platform/macos_host.hpp"
#include <pthread.h>
#elif defined(GUI_FORMS_APPLICATION_WINDOWS)
#include "gui_forms/platform/windows_host.hpp"
#elif defined(GUI_FORMS_APPLICATION_LINUX)
#include "gui_forms/platform/linux_host.hpp"
#endif

#if defined(GUI_FORMS_APPLICATION_SKIA)
#include "skia_raster.hpp"
#endif

namespace gui_forms {
namespace {
std::atomic_flag running = ATOMIC_FLAG_INIT;
// Set once by Application::shutdown. Read and written only while `running`
// is held, so the flag orders it; atomic so a refused caller on another
// thread never races the store.
std::atomic<bool> shut_down{false};

class RunningScope final {
public:
    RunningScope() noexcept : acquired_(!running.test_and_set()) {}
    RunningScope(const RunningScope&) = delete;
    RunningScope& operator=(const RunningScope&) = delete;
    ~RunningScope() { if (acquired_) running.clear(); }
    [[nodiscard]] bool acquired() const noexcept { return acquired_; }
private:
    bool acquired_;
};
struct CallbackFailure final {
    std::exception_ptr exception{};
    std::weak_ptr<detail::ApplicationWindowState> primary{};

    void stop() noexcept {
        const std::shared_ptr<detail::ApplicationWindowState> state = primary.lock();
        if (!state || (*state).closed || !(*state).request_close) return;
        // Failure shutdown consumes this request. Swap into an active owner
        // without allocating; a synchronous close may clear the stored slot.
        std::function<void()> request_close{};
        request_close.swap((*state).request_close);
        try {
            request_close();
        } catch (...) {
            // Native delivery may fail. Preserve a retry only while the same
            // open state has no replacement request; never revive closed state.
            if (!(*state).closed && !(*state).request_close) {
                request_close.swap((*state).request_close);
            }
        }
    }
    void capture() noexcept {
        if (!exception) exception = std::current_exception();
        stop();
    }
};
struct WindowBridge final {
    std::shared_ptr<detail::ApplicationWindowState> state{};
    std::shared_ptr<CallbackFailure> failure{};
    Window* model{};
    ApplicationWindowOptions options{};
    // A floating window's show and hide go to the run's floating policy.
    bool floating{};
    bool host_ready{};
    bool visibility_ready{};
    bool notified{};

    void notify() noexcept {
        if (notified || !host_ready || !visibility_ready) return;
        notified = true;
        (*state).ready = true;
        try {
            // The window's identity in the run, for frames and ordering.
            detail::register_run_window(state);
        } catch (...) { (*failure).capture(); }
        detail::attach_application_window(*model, state);
        if ((*failure).exception) { (*failure).stop(); return; }
        try {
            if (options.ready) {
                const ApplicationWindowHandle handle = detail::ApplicationHandleAccess::make(state);
                options.ready(*model, handle);
            }
        } catch (...) { (*failure).capture(); }
    }
};
struct HostReady final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(std::function<void()> wake, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)>,
                    std::function<HostServiceStatus(const HostTooltipRequest&)>,
                    std::function<void()>, std::function<HostClipboardTextResult()>,
                    std::function<HostServiceStatus(std::string_view)>) const noexcept {
        (*(*bridge).state).request_close = std::move(request_close);
        (*bridge).host_ready = true;
        try {
            if ((*bridge).options.wake_ready) {
                (*bridge).options.wake_ready(std::move(wake));
            }
        } catch (...) { (*(*bridge).failure).capture(); }
        (*bridge).notify();
    }
};
struct DispatchPending final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()() const noexcept {
        if ((*(*bridge).state).closed || (*(*bridge).failure).exception) return;
        try {
            if ((*bridge).options.dispatch_pending) (*bridge).options.dispatch_pending();
        } catch (...) { (*(*bridge).failure).capture(); }
    }
};
struct FullScreenReady final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(std::function<void()> toggle) const noexcept {
        (*(*bridge).state).toggle_full_screen = std::move(toggle);
    }
};
struct VisibilityReady final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(std::function<void()> show, std::function<void()> hide) const noexcept {
        if ((*bridge).floating) {
            (*(*bridge).state).show = detail::PolicyVisibility{(*bridge).state, true};
            (*(*bridge).state).hide = detail::PolicyVisibility{(*bridge).state, false};
        } else {
            (*(*bridge).state).show = std::move(show);
            (*(*bridge).state).hide = std::move(hide);
        }
        (*bridge).visibility_ready = true;
        (*bridge).notify();
    }
};
struct TitleReady final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(std::function<HostServiceStatus(std::string_view)> update) const noexcept {
        (*(*bridge).state).set_title = std::move(update);
    }
};
struct ClientSizeReady final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(std::function<HostServiceStatus(Size)> request) const noexcept {
        (*(*bridge).state).request_client_size = std::move(request);
    }
};
// Reached through the run's frame router (detail::attach_run_windowing).
struct FrameChanged final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(const HostWindowFrameEvent& event) const noexcept {
        if ((*(*bridge).state).closed || (*(*bridge).failure).exception) return;
        try {
            if ((*bridge).options.frame_changed) (*bridge).options.frame_changed(event);
        } catch (...) { (*(*bridge).failure).capture(); }
    }
};
struct Closing final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()(HostCloseRequest& request) const noexcept {
        if ((*(*bridge).failure).exception) { request.cancel = false; return; }
        try {
            if ((*bridge).options.closing) (*bridge).options.closing(request);
        } catch (...) {
            request.cancel = false;
            (*(*bridge).failure).capture();
        }
    }
};
struct Closed final {
    std::shared_ptr<WindowBridge> bridge{};
    void operator()() const noexcept {
        if ((*(*bridge).state).closed) return;
        detail::unregister_run_window(*(*bridge).state);
        detail::revoke_window_state(*(*bridge).state);
        try {
            if ((*bridge).options.closed) (*bridge).options.closed();
        } catch (...) { (*(*bridge).failure).capture(); }
    }
};
class BridgeScope final {
public:
    BridgeScope() = default;
    BridgeScope(const BridgeScope&) = delete;
    BridgeScope& operator=(const BridgeScope&) = delete;
    ~BridgeScope() {
        for (std::size_t index = 0U; index < windows.size(); ++index) {
            detail::revoke_window_state(*(*windows[index]).state);
        }
    }
    // Drops the bridges of closed windows. Each native callback holds its own
    // bridge, so a closed window's late callbacks still find theirs.
    void prune() noexcept {
        for (std::size_t index = windows.size(); index > 0U; --index) {
            if (!(*(*windows[index - 1U]).state).closed) continue;
            windows.erase(windows.begin() + static_cast<std::ptrdiff_t>(index - 1U));
        }
    }
    std::vector<std::shared_ptr<WindowBridge>> windows{};
};

// Builds the native entry for one window and records its bridge. The model
// and options are consumed from `source`.
template <typename NativeWindow>
NativeWindow prepare_one(ApplicationWindow& source, BridgeScope& scope,
                         const std::shared_ptr<CallbackFailure>& failure,
                         const std::shared_ptr<detail::ApplicationRunState>& run) {
    const bool primary = detail::primary_window_entry(source);
    const std::shared_ptr<WindowBridge> bridge = std::make_shared<WindowBridge>();
    (*bridge).state = std::make_shared<detail::ApplicationWindowState>();
    (*bridge).failure = failure;
    (*bridge).model = source.model.get();
    (*bridge).options = std::move(source.options);
    (*bridge).floating = source.floating != HostWindowFloating::none;
    detail::ApplicationWindowState& state = *(*bridge).state;
    state.can_hide = !primary;
    state.stable_id = source.stable_id;
    state.run = run;
    if ((*bridge).options.frame_changed) state.frame_changed = FrameChanged{bridge};
    if (primary) (*failure).primary = (*bridge).state;
    scope.windows.push_back(bridge);
    NativeWindow entry{};
    entry.stable_id = std::move(source.stable_id);
    entry.owner_id = std::move(source.owner_id);
    entry.tool_window = source.tool_window;
    entry.floating = source.floating;
    entry.model = std::move(source.model);
    // One key map for the application's windows, floating ones included
    // (owner decision DK-44).
    (*entry.model).set_key_bindings(Application::key_bindings());
    entry.options.title = (*bridge).options.title;
    entry.options.initial_size = (*bridge).options.initial_size;
    entry.options.minimum_size = (*bridge).options.minimum_size;
    entry.options.initially_visible = (*bridge).options.initially_visible;
    entry.options.hide_on_close = (*bridge).options.hide_on_close;
    entry.options.minimizable = (*bridge).options.minimizable;
    entry.options.print_metrics_on_close = (*bridge).options.print_metrics_on_close;
    entry.options.host_ready = HostReady{bridge};
    entry.options.dispatch_pending = DispatchPending{bridge};
    entry.options.visibility_ready = VisibilityReady{bridge};
    entry.options.full_screen_ready = FullScreenReady{bridge};
    entry.options.title_ready = TitleReady{bridge};
    entry.options.client_size_ready = ClientSizeReady{bridge};
    entry.options.close_request = Closing{bridge};
    entry.options.closed = Closed{bridge};
    return entry;
}

template <typename NativeWindow>
void prepare(std::vector<ApplicationWindow>& windows, BridgeScope& scope,
             const std::shared_ptr<CallbackFailure>& failure,
             const std::shared_ptr<detail::ApplicationRunState>& run,
             std::vector<NativeWindow>& native) {
    // Both destinations are fresh storage owned by this run. The native host
    // owns model lifetimes during dispatch; bridge callbacks borrow those
    // models only while the host is active. BridgeScope revokes handles on exit.
    native.reserve(windows.size());
    scope.windows.reserve(HostWindowing::maximum_windows);
    for (std::size_t index = 0U; index < windows.size(); ++index) {
        native.push_back(prepare_one<NativeWindow>(windows[index], scope, failure, run));
    }
}

// Why the run refuses to open `entry` during the run, before any native
// request: invalid_argument for an entry Application::validate would refuse,
// a primary entry, an owner that is not live, a floating entry not owned by
// the primary window itself, or a stable id already live; too_large beyond
// maximum_windows live windows or maximum_floating_windows floating ones.
// HostServiceError::none when the entry may be opened. Reads `entry` only.
HostServiceError runtime_entry_refusal(const ApplicationWindow& entry, const HostWindowing& windowing) {
    if (!detail::valid_window_entry(entry) || !(*entry.model).check_access()) {
        return HostServiceError::invalid_argument;
    }
    if (detail::primary_window_entry(entry)) return HostServiceError::invalid_argument;
    // A host that opens no popup windows refuses a popup before any native
    // request (HostWindowing::supports_popup_windows).
    if (entry.popup.has_value() && !windowing.supports_popup_windows()) return HostServiceError::unsupported;
    std::optional<HostWindowId> owner{};
    if (!entry.owner_id.empty()) {
        owner = windowing.find(entry.owner_id);
        if (!owner.has_value()) return HostServiceError::invalid_argument;
    }
    const bool floating = entry.floating != HostWindowFloating::none;
    // A floating window is owned by the primary window itself.
    if (floating && !(owner.has_value() && *owner == windowing.primary())) {
        return HostServiceError::invalid_argument;
    }
    if (windowing.find(entry.stable_id).has_value()) return HostServiceError::invalid_argument;
    if (windowing.window_count() >= HostWindowing::maximum_windows) return HostServiceError::too_large;
    const bool floating_full =
        floating && windowing.floating_window_count() >= HostWindowing::maximum_floating_windows;
    if (floating_full) return HostServiceError::too_large;
    return HostServiceError::none;
}

// The run's open operation (ApplicationWindows::open_window). Refusals found
// before the native request leave the entry unchanged; HostWindowing::admit
// applies the same rules again in the adapter.
template <typename NativeWindow>
struct RuntimeOpen final {
    // Borrowed: BridgeScope lives in Application::run's frame, which outlives
    // the native run and therefore every call of this operation.
    BridgeScope* scope{};
    std::shared_ptr<CallbackFailure> failure{};
    std::weak_ptr<detail::ApplicationRunState> run{};
    std::function<HostWindowAdmission(NativeWindow)> native_open{};

    ApplicationWindowOpenResult operator()(ApplicationWindow&& entry) const {
        ApplicationWindowOpenResult result;
        const std::shared_ptr<detail::ApplicationRunState> state = run.lock();
        if (!state || (*state).closed || (*state).windowing == nullptr || !native_open) {
            result.status.error = HostServiceError::after_shutdown;
            return result;
        }
        const HostServiceError refusal = runtime_entry_refusal(entry, *(*state).windowing);
        if (refusal != HostServiceError::none) {
            result.status.error = refusal;
            return result;
        }
        (*scope).prune();
        NativeWindow native = prepare_one<NativeWindow>(entry, *scope, failure, state);
        const std::shared_ptr<detail::ApplicationWindowState> window = (*(*scope).windows.back()).state;
        const HostWindowAdmission admission = native_open(std::move(native));
        if (!admission.status.accepted()) {
            detail::revoke_window_state(*window);
            result.status = admission.status;
            return result;
        }
        result.window = detail::ApplicationHandleAccess::make(window);
        return result;
    }
};

// Published by the native host once its windowing exists.
template <typename NativeWindow>
struct WindowingReady final {
    BridgeScope* scope{};
    std::shared_ptr<CallbackFailure> failure{};
    std::shared_ptr<detail::ApplicationRunState> run{};
    void operator()(HostWindowing& windowing,
                    std::function<HostWindowAdmission(NativeWindow)> open) const {
        RuntimeOpen<NativeWindow> operation{scope, failure, run, std::move(open)};
        detail::attach_run_windowing(run, windowing, std::move(operation));
    }
};
struct WindowingEnded final {
    std::shared_ptr<detail::ApplicationRunState> run{};
    void operator()() const noexcept { detail::detach_run_windowing(*run); }
};
}

HostCapabilities Application::capabilities() {
#if defined(GUI_FORMS_APPLICATION_MACOS)
    const HostCapabilities result = host::macos_capabilities();
#elif defined(GUI_FORMS_APPLICATION_WINDOWS)
    const HostCapabilities result = host::windows_capabilities();
#elif defined(GUI_FORMS_APPLICATION_LINUX)
    const HostCapabilities result = host::linux_capabilities();
#else
    const HostCapabilities result{HostCapabilities::current_protocol_version, "unsupported", HostCapability::none};
#endif
    return result;
}

ApplicationResult Application::run(std::unique_ptr<Window> window, ApplicationWindowOptions options) {
    std::vector<ApplicationWindow> windows{};
    ApplicationWindow entry{};
    entry.stable_id = "main";
    entry.model = std::move(window);
    entry.options = std::move(options);
    windows.push_back(std::move(entry));
    const ApplicationResult result = run(std::move(windows));
    return result;
}

ApplicationResult Application::run(std::vector<ApplicationWindow> windows) {
#if defined(GUI_FORMS_APPLICATION_MACOS)
    if (pthread_main_np() == 0) return {ApplicationError::wrong_thread};
#endif
    const RunningScope running_scope{};
    if (!running_scope.acquired()) return {ApplicationError::already_running};
    if (shut_down.load()) return {ApplicationError::after_shutdown};
    ApplicationResult result = validate(windows);
    if (!result.accepted()) return result;
    // Before any native window exists (stage T5).
    for (ApplicationWindow& entry : windows) {
        if (entry.options.non_opaque) static_cast<void>((*entry.model).declare_non_opaque());
    }
    BridgeScope bridges{};
    const std::shared_ptr<CallbackFailure> failure = std::make_shared<CallbackFailure>();
    const std::shared_ptr<detail::ApplicationRunState> run_state =
        std::make_shared<detail::ApplicationRunState>();
#if defined(GUI_FORMS_APPLICATION_MACOS)
    std::vector<host::MacApplicationWindow> native{};
    prepare(windows, bridges, failure, run_state, native);
    host::MacApplicationOptions application{};
    application.windowing_ready =
        WindowingReady<host::MacApplicationWindow>{&bridges, failure, run_state};
    application.windowing_ended = WindowingEnded{run_state};
    result.native_exit_code = host::run_macos_application(std::move(native), std::move(application));
#elif defined(GUI_FORMS_APPLICATION_WINDOWS)
    std::vector<host::WindowsApplicationWindow> native{};
    prepare(windows, bridges, failure, run_state, native);
    host::WindowsApplicationOptions application{};
    application.windowing_ready =
        WindowingReady<host::WindowsApplicationWindow>{&bridges, failure, run_state};
    application.windowing_ended = WindowingEnded{run_state};
    result.native_exit_code = host::run_windows_application(std::move(native), std::move(application));
#elif defined(GUI_FORMS_APPLICATION_LINUX)
    std::vector<host::LinuxApplicationWindow> native{};
    prepare(windows, bridges, failure, run_state, native);
    host::LinuxApplicationOptions application{};
    application.windowing_ready =
        WindowingReady<host::LinuxApplicationWindow>{&bridges, failure, run_state};
    application.windowing_ended = WindowingEnded{run_state};
    result.native_exit_code = host::run_linux_application(std::move(native), std::move(application));
#else
    result.error = ApplicationError::unsupported;
#endif
    // The native host normally ends the windowing itself; this covers a
    // host that returned early.
    detail::detach_run_windowing(*run_state);
    if ((*failure).exception) {
        result.error = ApplicationError::callback_failure;
        result.callback_exception = (*failure).exception;
    } else if (result.native_exit_code != 0) {
        result.error = ApplicationError::backend_failure;
    }
    return result;
}

ApplicationResult Application::shutdown() {
#if defined(GUI_FORMS_APPLICATION_MACOS)
    if (pthread_main_np() == 0) return {ApplicationError::wrong_thread};
#endif
    // Holding the run flag keeps a run from starting while the library ends.
    const RunningScope running_scope{};
    if (!running_scope.acquired()) return {ApplicationError::already_running};
    if (shut_down.load()) return {ApplicationError::after_shutdown};
    shut_down.store(true);
    // Every run has returned, so its windows, rasters and fonts are destroyed
    // (the window models are consumed by run, and handles are weak).
#if defined(GUI_FORMS_APPLICATION_SKIA)
    render::shutdown_skia_library();
#endif
    const ApplicationResult result{};
    return result;
}
} // namespace gui_forms
