#include "gui_forms/application.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/window/native_popup/native_popup.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "gui_forms/platform/windows_host.hpp"
#include "support/windows_desktop.hpp"
#include "support/windows_own_window.hpp"
#endif

namespace {
using namespace gui_forms;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::unique_ptr<Window> make_window() {
    const std::shared_ptr<Control> root = make_control<Control>(StableId("native.application.root"));
    std::unique_ptr<Window> window = std::make_unique<Window>(root, Size{360.0, 240.0});
    return window;
}
#if defined(_WIN32)
struct DirectTitleThread final {
    const std::function<HostServiceStatus(std::string_view)>* update{};
    HostServiceStatus* result{};
    void operator()() const { *result = (*update)("Wrong thread"); }
};
struct DirectTitleReady final {
    std::function<HostServiceStatus(std::string_view)>* retained{};
    void operator()(std::function<HostServiceStatus(std::string_view)> update) const {
        *retained = std::move(update);
        const std::function<HostServiceStatus(std::string_view)>& title = *retained;
        require(title("Direct Unicode \xe6\x97\xa5").accepted(), "direct native title update failed");
        require(title(std::string_view("a\0b", 3)).error == HostServiceError::invalid_argument,
                "direct title callback accepted embedded NUL");
        require(title("\xff").error == HostServiceError::invalid_argument,
                "direct title callback accepted malformed UTF-8");
        require(title(std::string(65537, 'x')).error == HostServiceError::invalid_argument,
                "direct title callback accepted excess source bytes");
        require(test_support::own_gui_forms_window(L"Direct Unicode \u65e5") != nullptr,
                "rejected direct title updates changed native title");
        HostServiceStatus wrong_thread{};
        std::thread worker(DirectTitleThread{retained, &wrong_thread});
        worker.join();
        require(wrong_thread.error == HostServiceError::wrong_thread,
                "direct title callback did not reject the wrong thread");
        require(title(std::string(65536, 'x')).accepted(), "direct title exact limit rejected");
        require(title("").accepted(), "direct title empty value rejected");
    }
};
void check_direct_title_lifetime() {
    std::function<HostServiceStatus(std::string_view)> retained{};
    host::WindowsHostOptions options{};
    options.title = "Direct title lifetime fixture";
    options.print_metrics_on_close = false;
    options.close_after_launch_for_testing = true;
    options.title_ready = DirectTitleReady{&retained};
    const int result = host::run_windows(make_window(), std::move(options));
    require(result == 0 && static_cast<bool>(retained), "direct title fixture did not run");
    require(retained("Expired").error == HostServiceError::after_shutdown,
            "direct title callback outlived native host authority");
}
#endif
struct Probe final {
    ApplicationWindowHandle handle{};
    unsigned ready{};
    unsigned closing{};
    unsigned closed{};
    bool fail{};
    bool closed_active{};
    bool attached_services{};
    bool primary_hide_rejected{};
    bool recursive_rejected{};
    bool shutdown_rejected{};
    bool system_close{};
    // The model's client size when close was first requested, after the
    // size request made in ready has been delivered.
    Window* window{};
    Size closing_size{};
};
struct Ready final {
    Probe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        (*probe).handle = handle;
        ++(*probe).ready;
        (*probe).attached_services = window.host_services() != nullptr;
        require(handle.set_title("Updated \xe6\x97\xa5 \xf0\x9f\x98\x80").accepted(), "native Unicode title update refused");
#if defined(_WIN32)
        require(test_support::own_gui_forms_window(L"Updated \u65e5 \U0001f600") != nullptr,
            "native Unicode title not applied");
#endif
        require(handle.set_title("GUI.Forms portable application contract").accepted(), "native title restore refused");
        require(handle.request_client_size({100.0, 100.0}).error == HostServiceError::invalid_argument,
            "size below the window minimum accepted");
        require(handle.request_client_size({420.0, 260.0}).accepted(), "native client size request refused");
        (*probe).window = &window;
        const HostServiceStatus hidden = handle.hide();
        (*probe).primary_hide_rejected = hidden.error == HostServiceError::invalid_argument;
        std::unique_ptr<Window> recursive_window = make_window();
        const ApplicationResult recursive = Application::run(std::move(recursive_window));
        (*probe).recursive_rejected = recursive.error == ApplicationError::already_running;
        const ApplicationResult nested_shutdown = Application::shutdown();
        (*probe).shutdown_rejected = nested_shutdown.error == ApplicationError::already_running;
        if ((*probe).fail) throw std::runtime_error("intentional ready failure");
#if defined(_WIN32)
        if ((*probe).system_close) {
            const HWND native = test_support::own_gui_forms_window(L"GUI.Forms portable application contract");
            require(native != nullptr, "native close fixture window missing");
            const BOOL posted = PostMessageW(native, WM_SYSKEYDOWN, VK_F4,
                static_cast<LPARAM>(1ULL << 29U));
            require(posted != 0, "native close gesture was not posted");
            return;
        }
#endif
        require(handle.active(), "ready handle inactive");
        const HostServiceStatus closed = handle.request_close();
        require(closed.accepted(), "ready close request failed");
    }
};
struct Closing final {
    Probe* probe{};
    void operator()(HostCloseRequest& request) const {
        ++(*probe).closing;
        if ((*probe).closing == 1U) (*probe).closing_size = (*(*probe).window).client_size();
        if ((*probe).closing == 1U) {
            request.cancel = true;
            const HostServiceStatus closed = (*probe).handle.request_close();
            require(closed.accepted(), "second close request failed");
        }
    }
};
struct Closed final {
    Probe* probe{};
    void operator()() const {
        ++(*probe).closed;
        (*probe).closed_active = (*probe).handle.active();
    }
};
ApplicationResult run_probe(Probe& probe) {
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms portable application contract";
    options.initial_size = {360, 240};
    options.ready = Ready{&probe};
    options.closing = Closing{&probe};
    options.closed = Closed{&probe};
    std::unique_ptr<Window> window = make_window();
    const ApplicationResult result = Application::run(std::move(window), std::move(options));
    return result;
}
struct DispatchProbe final {
    std::function<void()> wake{};
    ApplicationWindowHandle handle{};
    std::thread::id ui_thread{std::this_thread::get_id()};
    unsigned dispatched{};
    bool fail{};
};
struct WakeReady final {
    DispatchProbe* probe{};
    void operator()(std::function<void()> wake) const { (*probe).wake = std::move(wake); }
};
struct DispatchReady final {
    DispatchProbe* probe{};
    void operator()(Window&, ApplicationWindowHandle handle) const {
        require(static_cast<bool>((*probe).wake), "wake was not published before ready");
        (*probe).handle = handle;
        // Exercise the worker-to-host boundary without retaining a worker after
        // the window closes. The callback itself must execute on the UI thread.
        std::thread worker((*probe).wake);
        worker.join();
    }
};
struct DrainPending final {
    DispatchProbe* probe{};
    void operator()() const {
        if (!(*probe).handle.active()) return;
        require(std::this_thread::get_id() == (*probe).ui_thread, "dispatch ran off the UI thread");
        ++(*probe).dispatched;
        if ((*probe).fail) throw std::runtime_error("intentional dispatch failure");
        const HostServiceStatus closed = (*probe).handle.request_close();
        require(closed.accepted(), "dispatch close failed");
    }
};
ApplicationResult run_dispatch_probe(DispatchProbe& probe) {
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms portable worker dispatch";
    options.initial_size = {360, 240};
    options.wake_ready = WakeReady{&probe};
    options.ready = DispatchReady{&probe};
    options.dispatch_pending = DrainPending{&probe};
    std::unique_ptr<Window> window = make_window();
    const ApplicationResult result = Application::run(std::move(window), std::move(options));
    return result;
}
#if defined(_WIN32)
struct CharacterReady final {
    std::shared_ptr<TextBox> editor{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        const bool focused = window.request_focus(editor);
        require(focused, "character fixture focus failed");
        const HWND native = test_support::own_gui_forms_window(L"GUI.Forms character normalization fixture");
        require(native != nullptr, "character fixture window missing");
        DWORD process_id = 0;
        GetWindowThreadProcessId(native, &process_id);
        require(process_id == GetCurrentProcessId(), "character fixture belongs to another process");
        // Messages target only this fixture. Command-generated WM_CHAR values
        // must not enter the text store; ordinary UTF-16 text still must.
        for (WPARAM character = 0; character < 0x20; ++character) {
            PostMessageW(native, WM_CHAR, character, 0);
        }
        PostMessageW(native, WM_CHAR, 0x7F, 0);
        PostMessageW(native, WM_CHAR, L'A', 0);
        PostMessageW(native, WM_CHAR, 0xD83D, 0);
        PostMessageW(native, WM_CHAR, 0xDE00, 0);
        PostMessageW(native, WM_CHAR, L'Z', 0);
        const HostServiceStatus closed = handle.request_close();
        require(closed.accepted(), "character fixture close failed");
    }
};
void check_character_normalization() {
    const std::shared_ptr<TextBox> editor = make_control<TextBox>(StableId("native.character.editor"));
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms character normalization fixture";
    options.initial_size = {360, 240};
    options.ready = CharacterReady{editor};
    std::unique_ptr<Window> window = std::make_unique<Window>(editor, Size{360.0, 240.0});
    const ApplicationResult result = Application::run(std::move(window), std::move(options));
    if (result.callback_exception) std::rethrow_exception(result.callback_exception);
    require(result.accepted(), "native character normalization fixture failed");
    if ((*editor).text() != "A\xF0\x9F\x98\x80Z") {
        std::cerr << "character fixture byte count=" << (*editor).text().size() << " bytes:";
        for (const unsigned char byte : (*editor).text()) std::cerr << ' ' << static_cast<unsigned>(byte);
        std::cerr << '\n';
        throw std::runtime_error("native text normalization changed text or inserted command bytes");
    }
}
#endif
// Stage DK W0: a window opened and destroyed during the run, its frame
// requested and read, the pointer, the window at a point and ordering. The
// runtime window stays hidden; nothing here needs the desktop's attention.
struct RuntimeWindowsProbe final {
    ApplicationWindowHandle floating{};
    unsigned floating_ready{};
    unsigned floating_closed{};
    unsigned frames{};
    bool supported{};
    bool ready_before_return{};
    HostServiceStatus open{};
    HostServiceStatus request{};
    HostWindowFrameResult frame{};
    HostServiceStatus pointer{};
    HostServiceStatus at_point{};
    HostServiceStatus raise{};
    HostServiceStatus destroy{};
    bool closed_before_return{};
    bool handle_expired{};
    HostServiceStatus floating_open{};
    HostServiceStatus floating_destroy{};
    // Native popups (phase (b)): what each declared focus opened, and on
    // Win32 the popup window's native styles, owner and placement.
    NativePopupSurface passive_surface{NativePopupSurface::none};
    NativePopupRefusal passive_refusal{NativePopupRefusal::none};
    NativePopupSurface focus_surface{NativePopupSurface::none};
    bool passive_closed{};
    bool native_checked{};
    bool native_styles{};
    bool native_owner{};
    bool native_placed{};
};
#if defined(_WIN32)
// The popup windows of this thread that `owner` owns: WS_POPUP windows.
struct PopupWindows final {
    HWND owner{};
    HWND found[4]{};
    std::size_t count{};
};
BOOL CALLBACK collect_popup_window(HWND window, LPARAM context) {
    PopupWindows& popups = *reinterpret_cast<PopupWindows*>(context);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    const bool popup = (style & WS_POPUP) != 0 && GetWindow(window, GW_OWNER) == popups.owner;
    if (popup && popups.count < 4U) {
        popups.found[popups.count] = window;
        popups.count = popups.count + 1U;
    }
    return TRUE;
}
// Reads the one passive popup window's native record into the probe:
// tool and no-activate styles without a caption, owned by `owner`, its
// client placed at the owner's client origin plus `bounds` (device pixels).
void check_native_popup(RuntimeWindowsProbe& state, HWND owner, Rect bounds) {
    PopupWindows popups{};
    popups.owner = owner;
    EnumThreadWindows(GetCurrentThreadId(), &collect_popup_window, reinterpret_cast<LPARAM>(&popups));
    state.native_checked = popups.count == 1U;
    if (!state.native_checked) return;
    const HWND popup = popups.found[0];
    const LONG_PTR style = GetWindowLongPtrW(popup, GWL_STYLE);
    const LONG_PTR extended = GetWindowLongPtrW(popup, GWL_EXSTYLE);
    state.native_styles = (style & WS_CAPTION) == 0 && (extended & WS_EX_TOOLWINDOW) != 0 &&
        (extended & WS_EX_NOACTIVATE) != 0;
    POINT owner_origin{0, 0};
    POINT popup_origin{0, 0};
    const bool mapped = ClientToScreen(owner, &owner_origin) && ClientToScreen(popup, &popup_origin);
    const double scale = static_cast<double>(GetDpiForWindow(owner)) / 96.0;
    const double expected_x = static_cast<double>(owner_origin.x) + bounds.x * scale;
    const double expected_y = static_cast<double>(owner_origin.y) + bounds.y * scale;
    state.native_placed = mapped && std::fabs(static_cast<double>(popup_origin.x) - expected_x) <= 1.0 &&
        std::fabs(static_cast<double>(popup_origin.y) - expected_y) <= 1.0;
}
#endif
// Opens one passive and one focus-taking popup from the main window, and
// closes them.
void open_native_popups(RuntimeWindowsProbe& state, Window& window) {
    const Control::Ptr owner = window.root();
    const Rect bounds{24.0, 32.0, 120.0, 80.0};
    NativePopupToken passive = window.open_native_popup(
        owner, make_control<Control>(StableId("native.popup.passive")), bounds, NativePopupOptions{});
    state.passive_surface = passive.surface();
    state.passive_refusal = passive.refusal();
#if defined(_WIN32)
    if (passive.surface() == NativePopupSurface::native) {
        const HWND main_window = FindWindowW(nullptr, L"GUI.Forms runtime windows fixture");
        state.native_owner = main_window != nullptr;
        if (state.native_owner) check_native_popup(state, main_window, bounds);
    }
#endif
    passive.disconnect();
    state.passive_closed = !passive.connected();
    NativePopupOptions focus{};
    focus.focus = HostPopupFocus::takes_focus;
    NativePopupToken taking = window.open_native_popup(
        owner, make_control<Control>(StableId("native.popup.focus")), bounds, focus);
    state.focus_surface = taking.surface();
    taking.disconnect();
}
struct FloatingReady final {
    RuntimeWindowsProbe* probe{};
    void operator()(Window&, ApplicationWindowHandle handle) const {
        (*probe).floating = handle;
        ++(*probe).floating_ready;
    }
};
struct FloatingClosed final {
    RuntimeWindowsProbe* probe{};
    void operator()() const { ++(*probe).floating_closed; }
};
struct FloatingFrame final {
    RuntimeWindowsProbe* probe{};
    void operator()(const HostWindowFrameEvent&) const { ++(*probe).frames; }
};
struct RuntimeMainReady final {
    RuntimeWindowsProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        const ApplicationWindows windows = handle.windows();
        RuntimeWindowsProbe& state = *probe;
        state.supported = windows.capabilities().supports(HostCapability::runtime_windows) &&
            windows.capabilities().supports(HostCapability::window_frames);
        if (state.supported) {
            ApplicationWindow entry{};
            entry.stable_id = "native.runtime.tool";
            entry.owner_id = "main";
            entry.tool_window = true;
            entry.model = make_window();
            entry.options.title = "GUI.Forms runtime window fixture";
            entry.options.initial_size = {200.0, 150.0};
            entry.options.minimum_size = {100.0, 80.0};
            entry.options.initially_visible = false;
            entry.options.print_metrics_on_close = false;
            entry.options.ready = FloatingReady{probe};
            entry.options.closed = FloatingClosed{probe};
            entry.options.frame_changed = FloatingFrame{probe};
            const ApplicationWindowOpenResult opened = windows.open_window(std::move(entry));
            state.open = opened.status;
            state.ready_before_return = state.floating_ready == 1U;
            if (opened.status.accepted()) {
                state.request = opened.window.request_frame({120.0, 140.0, 220.0, 160.0});
                state.frame = opened.window.frame();
                state.pointer = windows.pointer_position().status;
                state.at_point = windows.window_at({1.0, 1.0}).status;
                state.raise = opened.window.bring_to_front();
                state.destroy = windows.destroy_window(opened.window);
                state.closed_before_return = state.floating_closed == 1U;
                state.handle_expired = !opened.window.active() &&
                    opened.window.show().error == HostServiceError::after_shutdown;
            }
            // A floating document (decision 1), opened hidden and destroyed:
            // its native style and the policy's creation path, without
            // putting a window on the desktop.
            ApplicationWindow floating{};
            floating.stable_id = "native.runtime.floating";
            floating.owner_id = "main";
            floating.floating = HostWindowFloating::document;
            floating.model = make_window();
            floating.options.title = "GUI.Forms floating window fixture";
            floating.options.initial_size = {220.0, 160.0};
            floating.options.minimum_size = {100.0, 80.0};
            floating.options.initially_visible = false;
            floating.options.print_metrics_on_close = false;
            const ApplicationWindowOpenResult floating_window = windows.open_window(std::move(floating));
            state.floating_open = floating_window.status;
            if (floating_window.status.accepted()) {
                state.floating_destroy = windows.destroy_window(floating_window.window);
            }
            open_native_popups(state, window);
        }
        require(handle.request_close().accepted(), "runtime fixture close failed");
    }
};
void check_runtime_windows() {
    RuntimeWindowsProbe probe{};
    ApplicationWindowOptions options{};
    options.title = "GUI.Forms runtime windows fixture";
    options.initial_size = {360, 240};
    options.print_metrics_on_close = false;
    options.ready = RuntimeMainReady{&probe};
    const ApplicationResult result = Application::run(make_window(), std::move(options));
    if (result.callback_exception) std::rethrow_exception(result.callback_exception);
    require(result.accepted(), "runtime windows fixture failed");
    if (!probe.supported) return;
    require(probe.open.accepted() && probe.ready_before_return, "runtime window not ready when open returned");
    require(probe.request.accepted(), "runtime window frame request refused");
    require(probe.frame.status.accepted(), "runtime window frame refused");
#if defined(_WIN32)
    // Win32 applies the request synchronously; X11 answers on a later event.
    require(std::fabs(probe.frame.frame.content.width - 220.0) < 1.0 &&
                std::fabs(probe.frame.frame.content.height - 160.0) < 1.0,
            "runtime window frame did not follow the request");
    require(probe.frames >= 1U, "runtime window frame change not reported");
#endif
#if defined(_WIN32)
    // Off the input desktop (a private test desktop) Windows refuses the
    // cursor position; the host reports that refusal as backend_failure.
    const bool pointer_answered = probe.pointer.accepted() ||
        (!test_support::on_input_desktop() &&
         probe.pointer.error == HostServiceError::backend_failure);
#else
    const bool pointer_answered = probe.pointer.accepted();
#endif
    require(pointer_answered, "pointer position refused");
    require(probe.at_point.accepted(), "window-at-point refused");
    require(probe.raise.accepted(), "ordering refused");
    require(probe.destroy.accepted() && probe.closed_before_return && probe.floating_closed == 1U,
            "runtime window destroy did not close it once before returning");
    require(probe.handle_expired, "destroyed runtime window handle still active");
    require(probe.floating_open.accepted() && probe.floating_destroy.accepted(),
            "floating document window not opened and destroyed");
#if defined(_WIN32)
    // Win32 opens popup windows (phase (b)); X11 falls back to an overlay
    // until its popup windows land.
    require(probe.passive_surface == NativePopupSurface::native && probe.passive_refusal == NativePopupRefusal::none,
            "a passive popup opens a native window");
    require(probe.native_owner, "the main window is found");
    require(probe.native_checked, "the main window owns exactly one popup window");
    require(probe.native_styles, "the popup window is an undecorated, non-activating tool");
    require(probe.native_placed, "the popup sits at the owner's client origin plus its bounds");
    require(probe.focus_surface == NativePopupSurface::native, "a focus-taking popup opens a native window");
#elif defined(__APPLE__)
    // AppKit opens popup windows as borderless, non-activating panels
    // (phase (d)); their native record is not readable from this C++ test.
    require(probe.passive_surface == NativePopupSurface::native && probe.passive_refusal == NativePopupRefusal::none,
            "a passive popup opens a native window");
    require(probe.focus_surface == NativePopupSurface::native, "a focus-taking popup opens a native window");
#else
    require(probe.passive_surface == NativePopupSurface::overlay &&
                probe.passive_refusal == NativePopupRefusal::unsupported,
            "a host without popup windows falls back to an overlay");
#endif
    require(probe.passive_closed, "disconnecting closes the popup");
}
#if defined(__APPLE__)
struct ForeignShutdown final {
    ApplicationResult* result{};
    void operator()() const { (*result) = Application::shutdown(); }
};
#endif
// Last in the process: after it no run can start, and stx_skia is ended.
void check_shutdown() {
#if defined(__APPLE__)
    ApplicationResult foreign{};
    std::thread worker(ForeignShutdown{&foreign});
    worker.join();
    require(foreign.error == ApplicationError::wrong_thread, "shutdown off the main thread accepted");
#endif
    const ApplicationResult ended = Application::shutdown();
    require(ended.accepted(), "shutdown after the last run refused");
    const ApplicationResult again = Application::shutdown();
    require(again.error == ApplicationError::after_shutdown, "second shutdown accepted");
    const ApplicationResult late = Application::run(make_window());
    require(late.error == ApplicationError::after_shutdown, "run after shutdown accepted");
}
}
int main() {
#if defined(_WIN32)
    check_direct_title_lifetime();
    check_character_normalization();
    Probe system_close{};
    system_close.system_close = true;
    const ApplicationResult system_result = run_probe(system_close);
    require(system_result.accepted() && system_close.closing == 2U && system_close.closed == 1U,
            "Alt+F4 must reach ordinary close cancellation and teardown");
    // The resize arrives as the host's resize event, after ready returns and
    // before the close it posted (X11 answers later; see linux_host_tests).
    require(system_close.closing_size == Size{420.0, 260.0}, "native client size did not reach the model");
#endif
    Probe normal{};
    const ApplicationResult result = run_probe(normal);
    require(result.accepted(), "native application run failed");
    require(normal.ready == 1U && normal.closing == 2U && normal.closed == 1U, "native close/cancel order wrong");
    require(normal.attached_services && normal.primary_hide_rejected && normal.recursive_rejected, "ready contract incomplete");
    require(normal.shutdown_rejected, "shutdown during a run accepted");
    const HostServiceStatus shown = normal.handle.show();
    require(!normal.closed_active && !normal.handle.active() && shown.error == HostServiceError::after_shutdown,
            "native window handle survived close");
    Probe failure{};
    failure.fail = true;
    const ApplicationResult failed = run_probe(failure);
    require(failed.error == ApplicationError::callback_failure && failed.callback_exception && failure.closed == 1U,
            "callback failure escaped or left native window open");
    bool observed = false;
    try { std::rethrow_exception(failed.callback_exception); }
    catch (const std::runtime_error& error) { observed = std::string(error.what()) == "intentional ready failure"; }
    require(observed, "callback exception identity lost");
    DispatchProbe dispatch{};
    const ApplicationResult dispatched = run_dispatch_probe(dispatch);
    require(dispatched.accepted() && dispatch.dispatched > 0U,
            "worker wake did not dispatch UI work");
    DispatchProbe dispatch_failure{};
    dispatch_failure.fail = true;
    const ApplicationResult failed_dispatch = run_dispatch_probe(dispatch_failure);
    require(failed_dispatch.error == ApplicationError::callback_failure && failed_dispatch.callback_exception,
            "dispatch exception escaped the portable application boundary");
    check_runtime_windows();
    check_shutdown();
    std::cout << "portable application API: native attach, cancel/retry, close, expiry, recursion, exception containment, runtime windows and shutdown passed\n";
}
