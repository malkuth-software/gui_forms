#pragma once

#include "gui_forms/application/splashscreen/splashscreen.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <memory>

namespace gui_forms {

namespace detail {

// A host's splashscreen window (Splashscreen): opened hidden-free at its
// size, centred on the primary monitor, above other windows and taking no
// input. present paints `options` through the host's CPU raster with
// paint_splashscreen and shows the result before it returns (no run loop
// is running yet); false when it could not, leaving the last frame. The
// destructor closes the window. Main thread only.
class SplashscreenSurface {
public:
    virtual ~SplashscreenSurface() = default;
    [[nodiscard]] virtual bool present(const SplashscreenOptions& options) = 0;
};

// The splashscreen's minimal paint path: the face, a one-pixel border, the
// title (bundled control face, 20 units, bold), the status line (13 units)
// and, with a progress, a 4-unit bar along the bottom, in `size` logical
// units from the origin. Draws no images and measures no text.
void paint_splashscreen(Painter& painter, const SplashscreenOptions& options);

// The handover: closes the current splashscreen, if one shows. Called when
// the first window of a run is attached (attach_application_window).
void hand_over_splashscreen() noexcept;

} // namespace detail

namespace host {

// The platform host's splashscreen surface at `size` (logical units), or
// null when it has none or cannot open one. Defined by the platform host
// linked into GUIForms::Application (macOS, Windows, X11).
[[nodiscard]] std::unique_ptr<detail::SplashscreenSurface> open_platform_splashscreen(Size size);

} // namespace host

} // namespace gui_forms
