#pragma once

#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/host/services/host_services.hpp"

#include <windows.h>

// The Windows compositor adapter (stage T5, phase C2): what DWM offers a
// window, and the grant applied to it. The routes are the ones measured in
// notes:planning/stages/T5-compositor/MEASUREMENTS.md (C2):
// - every non-opaque backdrop enables the client's premultiplied alpha
//   with DwmEnableBlurBehindWindow and an empty blur region, which shows
//   what lies behind the window and keeps the native caption;
// - a material adds DWMWA_SYSTEMBACKDROP_TYPE (main window 2, transient
//   3), offered where the attribute reads back (build 22621 and later);
// - opaque withdraws both, with DWMSBT_AUTO, which a window that never set
//   the attribute has.
// The adapter reports and applies; the window decides (resolve_window_backdrop).
// dwmapi.dll is loaded at run time, as elsewhere in the host. UI thread only.
namespace gui_forms::host::detail {

// The live state for `window`: composition present from Windows 8 on
// (DWM cannot be off there), else from DwmIsCompositionEnabled; support
// for each backdrop, the transparency-effects setting
// (EnableTransparency, which also stands for reduced transparency on
// Windows) and high contrast (SPI_GETHIGHCONTRAST). `generation` is the
// caller's. Without dwmapi.dll only opaque is supported.
[[nodiscard]] CompositorState read_windows_compositor_state(HWND window, CompositorGeneration generation) noexcept;

// Whether `message` may change what read_windows_compositor_state reads:
// WM_SETTINGCHANGE for "ImmersiveColorSet" (transparency effects) or
// SPI_SETHIGHCONTRAST, WM_THEMECHANGED, and WM_DWMCOMPOSITIONCHANGED.
[[nodiscard]] bool windows_compositor_message(UINT message, WPARAM wparam, LPARAM lparam) noexcept;

// Applies a granted backdrop to `window`. A failure returns backend_failure
// and may leave part of the route applied (blur behind on without the
// material); the window then grants opaque, which withdraws everything.
[[nodiscard]] HostServiceStatus apply_windows_backdrop(HWND window, WindowBackdrop backdrop) noexcept;

// Frames a popup-kind window (a Popup or Splash, APP_WINDOW_RULES_DESIGN
// Part S) for its own alpha surface: DWM neither rounds its corners nor
// draws its one-pixel border (DWMWA_WINDOW_CORNER_PREFERENCE square,
// DWMWA_BORDER_COLOR none, both build 22000 and later), so that the clear
// pixels around the content's shape and shadow show only what lies
// beneath. Before build 22000 DWM draws neither and the refusals are
// ignored. Nothing without dwmapi.dll.
void apply_windows_popup_frame(HWND window) noexcept;

// Asks DWM for `window`'s corners (DWMWA_WINDOW_CORNER_PREFERENCE, build
// 22000 and later; owner, 2026-11-08: the window outer band) and reports
// them: Windows 11 rounds a top-level window by default at
// windows_corner_radius logical units, and squares it on request. Before
// build 22000 the attribute does not exist: the window is square and a
// request to round reports square with `complied` false. While the window
// is maximized, snapped or full screen (read_windows_window_state) DWM
// does not round it, whatever the preference: the report is square, with
// `complied` false only for a request to round. The host asks again on
// each state change (HostWindowStateEvent).
[[nodiscard]] HostWindowCornersResult apply_windows_corners(HWND window, WindowCornerPreference preference) noexcept;

// The radius DWM rounds a top-level window's corners by (DWMWCP_ROUND), in
// logical units.
inline constexpr double windows_corner_radius = 8.0;

// The native window's state as it bears on the window outer band: full
// screen (a captionless window covering its monitor; Win32 has no
// full-screen state of its own), maximized, and snapped. DWM leaves the
// corners of a maximized, snapped or full-screen window square.
struct WindowsWindowState final {
    bool full_screen{};
    bool maximized{};
    bool snapped{};

    [[nodiscard]] bool squares_corners() const noexcept { return full_screen || maximized || snapped; }

    friend bool operator==(const WindowsWindowState& left, const WindowsWindowState& right) noexcept {
        return left.full_screen == right.full_screen && left.maximized == right.maximized &&
               left.snapped == right.snapped;
    }
};

// Reads `window`'s state. A minimized window, or a null one, reads as the
// default (none of the three). Snapped is inferred: a window that is
// neither maximized nor full screen and whose size differs from its
// restore rectangle's (GetWindowPlacement) was placed by a snap, which
// keeps the restore rectangle. A size changed by the program through
// SetWindowPos updates the restore rectangle too, so it does not read as a
// snap.
[[nodiscard]] WindowsWindowState read_windows_window_state(HWND window) noexcept;

} // namespace gui_forms::host::detail
