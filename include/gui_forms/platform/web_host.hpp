#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::host {

// The browser host (Emscripten; owner decisions, 2026-11-15). The program
// runs on a worker thread (the page is linked with PROXY_TO_PTHREAD), so its
// event loop, nested modal loops included, blocks there as on a desktop,
// while the page's main thread keeps the browser's events, the canvas and
// WebGL2. Every window of the run is a layer in one canvas (the element
// `#gui-forms-canvas`, unless the page sets Module.guiFormsCanvas): the
// primary window fills it; owned, tool and floating windows have a frame
// the host draws, with a title bar and a close box, and can be moved and
// resized; popups are placed as the program puts them. The host keeps their
// stacking, focus and modal blocking (src/host/web/desk/web_desk.hpp).
//
// What a page cannot do is reported, not imitated (web_capabilities): no
// accessibility publication yet, no window frames outside the canvas, no
// tray, no sound cues, no clipboard images, no frame capture, no folder
// dialog. The open-file dialog is the browser's file chooser (files are
// copied into the program's file system under /web/files/<request>/); the
// save dialog is the browser's save chooser where it has one and a download
// otherwise, made when the program closes the file it wrote at the path the
// dialog returned. Message boxes are drawn by the host.
//
// The options mirror LinuxHostOptions and are published the same way, on the
// program's thread.
struct GUI_FORMS_API WebHostOptions final {
  std::string title{"GUI.Forms Application"};
  // A primary window takes the canvas's size; other windows open at this
  // client size, centred over their owner.
  Size initial_size{1120.0, 680.0};
  Size minimum_size{};
  bool print_metrics_on_close{true};
  bool initially_visible{true};
  bool hide_on_close{};
  bool minimizable{true};
  std::function<void(std::function<void()> show, std::function<void()> hide)>
      visibility_ready;
  // The primary window's full-screen toggle is the page's (Element
  // requestFullscreen on the canvas's parent); other windows ignore it.
  std::function<void(std::function<void()> toggle)> full_screen_ready;
  // The primary window's title is the document's title.
  std::function<void(std::function<HostServiceStatus(std::string_view)>)> title_ready;
  // The primary window's client size is the canvas's, decided by the page:
  // its request is unsupported. Other windows are resized in the canvas.
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

struct GUI_FORMS_API WebApplicationWindow final {
  std::string stable_id;
  std::string owner_id;
  std::unique_ptr<Window> model;
  WebHostOptions options;
  bool tool_window{};
  // See ApplicationWindow::floating.
  HostWindowFloating floating{HostWindowFloating::none};
  // A popup layer (ApplicationWindow::popup); none for every other kind.
  std::optional<HostPopupPlacement> popup{};
};

// Opens one window while the application runs, as LinuxOpenWindow does.
// Callable on the program's thread while the run lives; afterwards it
// reports after_shutdown.
using WebOpenWindow = std::function<HostWindowAdmission(WebApplicationWindow)>;

struct GUI_FORMS_API WebApplicationOptions final {
  // Published once on the program's thread before the first window is
  // created. `windowing` lives until windowing_ended has been called.
  std::function<void(HostWindowing &windowing, WebOpenWindow open)> windowing_ready;
  // Published once when the event loop ends (or the run fails), before the
  // windowing is shut down and released.
  std::function<void()> windowing_ended;
};

[[nodiscard]] GUI_FORMS_API HostCapabilities web_capabilities();
// Runs the windows until the primary window closes and returns the exit
// code, as run_linux_application does: 0 after an ordinary close, 1 when the
// run failed, 2 when a window could not be opened. Call it on the program's
// thread (main under PROXY_TO_PTHREAD), never on the page's main thread,
// which must stay free for the browser (wrong_thread is reported as 3).
GUI_FORMS_API int run_web_application(std::vector<WebApplicationWindow> windows,
                                      WebApplicationOptions application = {});
GUI_FORMS_API int run_web(std::unique_ptr<Window> window, WebHostOptions options = {});

} // namespace gui_forms::host
