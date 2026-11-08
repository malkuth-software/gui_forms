#pragma once

#include "gui_forms/host.hpp"
#include "gui_forms/platform/windows_compatibility_paint_endpoint/windows_compatibility_paint_endpoint.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::host {

struct WindowsHostOptions final {
    std::string title{"GUI.Forms Application"};
    Size initial_size{1120.0, 680.0};
    Size minimum_size{150.0, 150.0};
    // Portable frame capture (identical on every host). When capture_path is
    // non-empty, after the first presented frame that covers the whole window
    // the host encodes the presented framebuffer as PNG to that path, prints
    // one JSON line {"automation":"capture","saved":true|false,"path":"..."}
    // to stdout, and requests close. The process exit code is 0 when the file
    // was saved and non-zero otherwise. Only how the adapter obtains its
    // framebuffer differs between hosts.
    std::string capture_path{};
    // When set, the window is created at this logical size instead of
    // initial_size.
    std::optional<Size> capture_size{};
    // A fixed device scale (1.0 = 96 dpi) instead of the monitor's, for
    // captures and tests that need one scale whatever the display; 0 follows
    // the monitor and its DPI changes.
    double scale_override{};
    bool print_metrics_on_close{true};
    bool automation_enabled{};
    bool close_after_launch_for_testing{};
    bool quit_thread_on_close{true};
    bool popup_window{};
    bool initially_visible{true};
    bool hide_on_close{};
    bool minimizable{true};
    std::function<void(std::function<void()> show,
                       std::function<void()> hide)> visibility_ready;
    std::function<void(std::function<void()> toggle)> full_screen_ready;
    // Published on the host UI thread during initialization. The returned title
    // operation borrows its UTF-8 input only through the call; empty is allowed,
    // NUL/malformed UTF-8/>65536 bytes are invalid_argument. Invoke on the host
    // UI thread; wrong-thread calls are refused before native mutable access.
    // Copies do not own the host/window and may be discarded after run returns.
    // Calls on the UI thread after native lifetime ends return after_shutdown.
    // Adapter/conversion exceptions become backend_failure; accepted means the
    // native request was submitted, not that title pixels were presented.
    std::function<void(std::function<HostServiceStatus(std::string_view)>)> title_ready;
    // Published on the host UI thread during initialization, like title_ready.
    // The operation asks for a native client size in logical units; see
    // ApplicationWindowHandle::request_client_size. Sizes below minimum_size
    // are invalid_argument; full screen, maximized and minimized windows are
    // unsupported. The new size arrives through the host's resize event.
    std::function<void(std::function<HostServiceStatus(Size)>)> client_size_ready;
    Point initial_position{};
    std::function<std::shared_ptr<Control>(std::string_view)> automation_resolve;
    std::function<void(HostCloseRequest&)> close_request;
    std::function<void(std::function<void()> wake,
                       std::function<void()> request_close,
                       std::function<HostDialogResult(const HostDialogRequest&)> show_dialog,
                       std::function<HostServiceStatus(const HostTooltipRequest&)> show_tooltip,
                       std::function<void()> hide_tooltip,
                       std::function<HostClipboardTextResult()> read_clipboard_text,
                       std::function<HostServiceStatus(std::string_view)> write_clipboard_text)> host_ready;
    std::function<void()> dispatch_pending;
    std::function<void()> closed;
    std::function<void(std::string_view metrics_json,
                       std::string_view host_json)> final_snapshot;
};

struct WindowsApplicationWindow final {
    std::string stable_id;
    std::string owner_id;
    std::unique_ptr<Window> model;
    WindowsHostOptions options;
    bool tool_window{};
    // See ApplicationWindow::floating.
    HostWindowFloating floating{HostWindowFloating::none};
    // A native popup window (ApplicationWindow::popup); none for every
    // other kind.
    std::optional<HostPopupPlacement> popup{};
};

// Opens one window while the application runs: the same entry, ownership
// and readiness rules as an initial window, after HostWindowing::admit.
// Callable on the UI thread while the run lives; afterwards it reports
// after_shutdown. The window's host_ready and ready callbacks run before it
// returns.
using WindowsOpenWindow = std::function<HostWindowAdmission(WindowsApplicationWindow)>;

struct WindowsApplicationOptions final {
    // Published once on the UI thread before the first window is created.
    // `windowing` lives until windowing_ended has been called.
    std::function<void(HostWindowing& windowing, WindowsOpenWindow open)> windowing_ready;
    // Published once on the UI thread when the message loop ends, before
    // the remaining windows are destroyed and before the windowing is shut
    // down and released.
    std::function<void()> windowing_ended;
};

[[nodiscard]] HostCapabilities windows_capabilities();
int run_windows(std::unique_ptr<Window> window, WindowsHostOptions options = {});
int run_windows_application(std::vector<WindowsApplicationWindow> windows,
                            WindowsApplicationOptions application = {});

} // namespace gui_forms::host
