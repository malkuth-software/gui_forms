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

// Asks DWM for `window`'s corners (DWMWA_WINDOW_CORNER_PREFERENCE, build
// 22000 and later; owner, 2026-11-08: the window outer band) and reports
// them: Windows 11 rounds a top-level window by default at
// windows_corner_radius logical units, and squares it on request. Before
// build 22000 the attribute does not exist: the window is square and a
// request to round reports square with `complied` false.
[[nodiscard]] HostWindowCornersResult apply_windows_corners(HWND window, WindowCornerPreference preference) noexcept;

// The radius DWM rounds a top-level window's corners by (DWMWCP_ROUND), in
// logical units.
inline constexpr double windows_corner_radius = 8.0;

} // namespace gui_forms::host::detail
