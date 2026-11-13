#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host.hpp"
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

struct GUI_FORMS_API LinuxHostOptions final {
  std::string title{"GUI.Forms Application"};
  Size initial_size{1120.0, 680.0};
  // The smallest client size; a side of 0 (the default) sets no minimum
  // there beyond the host's own (ApplicationWindowOptions::minimum_size).
  Size minimum_size{};
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
  bool print_metrics_on_close{true};
  bool initially_visible{true};
  bool hide_on_close{};
  bool minimizable{true};
  std::function<void(std::function<void()> show, std::function<void()> hide)>
      visibility_ready;
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
  std::function<void(HostCloseRequest &)> close_request;
  std::function<void(
      std::function<void()> wake, std::function<void()> request_close,
      std::function<HostDialogResult(const HostDialogRequest &)> show_dialog,
      std::function<HostClipboardTextResult()> read_clipboard_text,
      std::function<HostServiceStatus(std::string_view)> write_clipboard_text)>
      host_ready;
  std::function<void()> dispatch_pending;
  std::function<void()> closed;
};

struct GUI_FORMS_API LinuxApplicationWindow final {
  std::string stable_id;
  std::string owner_id;
  std::unique_ptr<Window> model;
  LinuxHostOptions options;
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
using LinuxOpenWindow = std::function<HostWindowAdmission(LinuxApplicationWindow)>;

struct GUI_FORMS_API LinuxApplicationOptions final {
  // Published once on the UI thread before the first window is created.
  // `windowing` lives until windowing_ended has been called.
  std::function<void(HostWindowing &windowing, LinuxOpenWindow open)> windowing_ready;
  // Published once on the UI thread when the event loop ends (or the run
  // fails), before the windowing is shut down and released.
  std::function<void()> windowing_ended;
};

[[nodiscard]] GUI_FORMS_API HostCapabilities linux_capabilities();
GUI_FORMS_API int run_linux(std::unique_ptr<Window> window, LinuxHostOptions options = {});
GUI_FORMS_API int run_linux_application(std::vector<LinuxApplicationWindow> linux,
                          LinuxApplicationOptions application = {});

} // namespace gui_forms::host
