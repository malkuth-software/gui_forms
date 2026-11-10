#pragma once

#include "gui_forms/application/splashscreen/splashscreen.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <memory>
#include <optional>
#include <string_view>

namespace gui_forms {

namespace detail {

// One splashscreen frame, borrowed for one present: the window's size in
// logical units, the status, the progress, and the artwork in `images`
// (the splashscreen's own registry, live through the call).
struct SplashscreenFrame final {
    Size size{};
    std::string_view status{};
    std::optional<double> progress{};
    const ImageRegistry* images{};
    ImageId artwork{};
};

// A host's splashscreen window (Splashscreen): opened at its size, centred
// on the primary monitor, above other windows and taking no input. present
// synchronizes its CPU raster's images from `frame.images`, paints `frame`
// with paint_splashscreen and shows the result before it returns (no run
// loop is running yet); false when it could not, leaving the last frame.
// The destructor closes the window. Main thread only.
class SplashscreenSurface {
public:
    virtual ~SplashscreenSurface() = default;
    [[nodiscard]] virtual bool present(const SplashscreenFrame& frame) = 0;
};

// The splashscreen's minimal paint path: the artwork over the whole
// window, then, in fixed built-in styling with nothing behind them (owner,
// 2026-11-08: Q-S1 (a)), the status line near the bottom edge (bundled
// control face, 13 units, white over a one-unit dark offset copy so it
// reads on any artwork) and, with a progress, a 3-unit white bar on a
// translucent white track below it. Measures no text.
//
// It stays on this live path, not the Splash baker (Part S, phase 2): it
// presents before any run, so no baker is installed yet, and its artwork is
// an image in its own registry, which a display snapshot refuses
// (DisplaySnapshotRefusal::image). Baking it needs pixel-owning image
// commands in the snapshot, a later phase.
void paint_splashscreen(Painter& painter, const SplashscreenFrame& frame);

// The window size `options` asks for: its artwork's pixel size in logical
// units, or none when the artwork is not a valid PNG file with each extent
// in range. Reads the PNG's framing only.
[[nodiscard]] std::optional<Size> splashscreen_size(const SplashscreenOptions& options) noexcept;

// The handover: closes the current splashscreen, if one shows. Called when
// the first window of a run is attached (attach_application_window).
void hand_over_splashscreen() noexcept;

} // namespace detail

namespace host {

// The platform host's splashscreen surface at `size` (logical units), or
// null when it has none or cannot open one. Defined by the platform host
// linked into GUIForms::Application (macOS, Windows, X11).
[[nodiscard]] std::unique_ptr<gui_forms::detail::SplashscreenSurface> open_platform_splashscreen(Size size);

} // namespace host

} // namespace gui_forms
