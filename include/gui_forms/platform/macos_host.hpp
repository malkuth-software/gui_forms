#pragma once

#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::host {

enum class MacTitlebarPresentation : std::uint8_t {
    standard,
    transparent_full_size_content,
};

struct MacHostOptions {
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
    // Full-size content retains the native titled window, traffic-light
    // controls, resize behavior, system title, and accessibility identity. It
    // only makes the title material transparent and lets retained content
    // occupy that region.
    MacTitlebarPresentation titlebar_presentation{
        MacTitlebarPresentation::standard};
    // Optional stable ID of a retained backdrop that may begin a native window
    // drag. A primary click must hit this exact control; a descendant wins its
    // own input, so interactive content is never converted into a drag target.
    std::string window_drag_region_id;
    // Additional exact retained backdrops. This portable-ID list allows a
    // composed title surface to have several disconnected drag islands without
    // converting an interactive descendant into non-client input. The singular
    // spelling above remains source-compatible and is evaluated first.
    std::vector<std::string> window_drag_region_ids;
    // Application-mode secondary windows may be constructed and attached
    // before the run loop while remaining hidden until their owner invokes
    // them. hide_on_close keeps a reusable owned surface alive instead of
    // turning its native close button into a one-shot lifetime boundary.
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
    bool print_metrics_on_close{true};
    bool close_after_launch_for_testing{};
    std::uint32_t close_attempts_for_testing{1};
    std::function<void(HostCloseRequest&)> close_request;
    std::function<void(std::function<void()> wake,
                       std::function<void()> request_close,
                       std::function<HostDialogResult(const HostDialogRequest&)> show_dialog,
                       std::function<HostClipboardTextResult()> read_clipboard_text,
                       std::function<HostServiceStatus(std::string_view)> write_clipboard_text)> host_ready;
    std::function<void()> dispatch_pending;
    std::function<void()> closed;
    std::function<void(std::string_view metrics_json,
                       std::string_view host_json)> final_snapshot;
};

struct MacHostServiceOptions {
    std::string clipboard_name;
    bool cancel_dialogs_for_testing{};
};

// A native application may present several independent retained roots without
// merging their models or starting a second process. Ownership is expressed by
// stable host IDs, never native handles; adapters map it to their platform's
// owned/tool-window relationship.
struct MacApplicationWindow final {
    std::string stable_id;
    std::string owner_id;
    std::unique_ptr<Window> model;
    MacHostOptions options;
    bool tool_window{};
    // See ApplicationWindow::floating.
    HostWindowFloating floating{HostWindowFloating::none};
    // A native popup window (ApplicationWindow::popup); none for every
    // other kind.
    std::optional<HostPopupPlacement> popup{};
};

[[nodiscard]] HostCapabilities macos_capabilities();
[[nodiscard]] std::unique_ptr<HostServices> make_macos_host_services(
    MacHostServiceOptions options = {});
int run_macos(std::unique_ptr<Window> window, MacHostOptions options = {});
// Opens one window while the application runs: the same entry, ownership
// and readiness rules as an initial window, after HostWindowing::admit.
// Callable on the main thread while the run lives; afterwards it reports
// after_shutdown. The window's host_ready and ready callbacks run before it
// returns.
using MacOpenWindow = std::function<HostWindowAdmission(MacApplicationWindow)>;

struct MacApplicationOptions final {
    // Published once on the main thread before the first window is created.
    // `windowing` lives until windowing_ended has been called.
    std::function<void(HostWindowing& windowing, MacOpenWindow open)> windowing_ready;
    // Published once on the main thread when the application run ends,
    // before the remaining windows close and before the windowing is shut
    // down and released.
    std::function<void()> windowing_ended;
};

int run_macos_application(std::vector<MacApplicationWindow> windows,
                          MacApplicationOptions application = {});

} // namespace gui_forms::host
