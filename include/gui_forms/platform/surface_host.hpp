#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/window.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::host {

// The windows of a single-surface host (owner decision, 2026-11-15): the
// browser now, a framebuffer appliance later, any host with one surface.
// GUI.Forms' own window manager (src/host/surface/) makes every window of
// the run a layer of that surface: the primary window fills it; owned,
// tool and floating windows have a frame it draws, with a title bar and
// minimise, maximise and close boxes, and can be moved, resized, minimised
// to a tab along the surface's bottom edge, maximised and restored; popups
// and menus are placed as the program puts them; modal windows block the
// windows beneath them. The host supplies only the surface, its input and
// a present call.
//
// The options mirror LinuxHostOptions and are published the same way, on the
// run's thread.
struct GUI_FORMS_API SurfaceHostOptions final {
  std::string title{"GUI.Forms Application"};
  // A primary window takes the surface's size; other windows open at this
  // client size, centred over their owner.
  Size initial_size{1120.0, 680.0};
  Size minimum_size{};
  bool print_metrics_on_close{true};
  bool initially_visible{true};
  bool hide_on_close{};
  // Whether a framed window shows a minimise box.
  bool minimizable{true};
  std::function<void(std::function<void()> show, std::function<void()> hide)>
      visibility_ready;
  // The primary window's full-screen toggle is the host's (a browser's
  // requestFullscreen); other windows ignore it.
  std::function<void(std::function<void()> toggle)> full_screen_ready;
  // The primary window's title is the host's (a document's title).
  std::function<void(std::function<HostServiceStatus(std::string_view)>)> title_ready;
  // The primary window's client size is the surface's, decided by the
  // host: its request is unsupported. Other windows are resized in the
  // surface.
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

struct GUI_FORMS_API SurfaceApplicationWindow final {
  std::string stable_id;
  std::string owner_id;
  std::unique_ptr<Window> model;
  SurfaceHostOptions options;
  bool tool_window{};
  // See ApplicationWindow::floating.
  HostWindowFloating floating{HostWindowFloating::none};
  // A popup layer (ApplicationWindow::popup); none for every other kind.
  std::optional<HostPopupPlacement> popup{};
};

// Opens one window while the application runs, as LinuxOpenWindow does.
// Callable on the run's thread while the run lives; afterwards it reports
// after_shutdown.
using SurfaceOpenWindow = std::function<HostWindowAdmission(SurfaceApplicationWindow)>;

struct GUI_FORMS_API SurfaceApplicationOptions final {
  // Published once on the run's thread before the first window is created.
  // `windowing` lives until windowing_ended has been called.
  std::function<void(HostWindowing &windowing, SurfaceOpenWindow open)> windowing_ready;
  // Published once when the event loop ends (or the run fails), before the
  // windowing is shut down and released.
  std::function<void()> windowing_ended;
};

} // namespace gui_forms::host
