#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/platform/surface_host.hpp"
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
// WebGL2. The canvas (the element `#gui-forms-canvas`, unless the page sets
// Module.guiFormsCanvas) is the one surface of GUI.Forms' window manager
// (surface_host.hpp): every window of the run is a layer the manager
// composes into it on the CPU, and the page shows the result as one WebGL2
// texture.
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
// The window types are the single-surface host's (surface_host.hpp).
using WebHostOptions = SurfaceHostOptions;
using WebApplicationWindow = SurfaceApplicationWindow;
using WebOpenWindow = SurfaceOpenWindow;
using WebApplicationOptions = SurfaceApplicationOptions;

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
