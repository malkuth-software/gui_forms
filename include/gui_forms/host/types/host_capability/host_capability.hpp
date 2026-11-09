#pragma once

#include <cstdint>

namespace gui_forms {

enum class HostCapability : std::uint64_t {
    none = 0,
    lifecycle = 1ULL << 0U,
    scale_notifications = 1ULL << 1U,
    monitor_geometry = 1ULL << 2U,
    occlusion = 1ULL << 3U,
    scheduled_wake = 1ULL << 4U,
    pointer_input = 1ULL << 5U,
    keyboard_input = 1ULL << 6U,
    text_composition = 1ULL << 7U,
    pointer_capture = 1ULL << 8U,
    cursor = 1ULL << 9U,
    clipboard = 1ULL << 10U,
    typed_drag_destination = 1ULL << 11U,
    dialogs = 1ULL << 12U,
    menus = 1ULL << 13U,
    font_discovery = 1ULL << 14U,
    accessibility = 1ULL << 15U,
    typed_drag_source = 1ULL << 16U,
    sound_cues = 1ULL << 17U,
    clipboard_images = 1ULL << 18U,
    // The adapter can encode its presented framebuffer to a PNG file on
    // request (see the capture_path host option). Additive flag; no protocol
    // version change.
    frame_capture = 1ULL << 19U,
    // Notification-area (system tray) icons with click, double-click and
    // context events. Reported only where a tray is present (on X11, where a
    // system tray owns the _NET_SYSTEM_TRAY_Sn selection at startup).
    // Additive flags; no protocol version change.
    notification_area = 1ULL << 20U,
    // Notification text (a balloon or banner) shown from a tray icon.
    notification_area_messages = 1ULL << 21U,
    // A native menu shown from a tray icon (show_tray_menu).
    notification_area_menus = 1ULL << 22U,
    // Application-scope windowing (HostWindowing, stage DK decisions 6 and 7).
    // Each is reported separately so a host that can do one and not another
    // says so. Additive flags; no protocol version change.
    // Owned windows opened and destroyed while the application runs.
    runtime_windows = 1ULL << 23U,
    // Window frames read and requested in desktop logical units, with a
    // frame-changed report for every native move or resize.
    window_frames = 1ULL << 24U,
    // The pointer's position in desktop logical units, outside any window.
    screen_pointer = 1ULL << 25U,
    // Which of the application's windows is topmost at a desktop point.
    window_at_point = 1ULL << 26U,
    // Bringing one window to the front of the application's own windows.
    window_ordering = 1ULL << 27U,
    // Compositor cooperation (stage T5, notes:planning/stages/T5-compositor/
    // PLAN.md §2.1). Each is true only where the adapter implements it; what
    // the compositor allows now is the live CompositorState.
    // A window presented with per-pixel premultiplied alpha, whenever the
    // compositor state allows it (WindowBackdrop::transparent).
    transparent_windows = 1ULL << 28U,
    // A system material behind a transparent window (window_material,
    // transient_material).
    window_backdrops = 1ULL << 29U,
    // Compositor and preference changes reported at run time
    // (HostCompositorEvent after the first report).
    compositor_notifications = 1ULL << 30U,
    // The host's high-contrast and reduced-transparency settings reported
    // and fed to each window's PresentationSettings (owner decision T5-Q7).
    presentation_preferences = 1ULL << 31U,
    // Transient surfaces (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part
    // S): Splash windows (tooltips, the splashscreen) and Popup windows
    // (menus, drop-downs, pickers) opened during the run, translucent with
    // the core's shadow and fade where the compositor allows it
    // (HostWindowing::supports_popup_windows, supports_splash_windows,
    // transient_windows_translucent). Additive flags; no protocol version
    // change.
    transient_surfaces = 1ULL << 32U,
    // Transient surfaces may be opaque, with no shadow or fade, as the
    // compositor decides at run time (X11 without a compositing manager;
    // owner, 2026-11-08: Q-S2 (b)). Reported with transient_surfaces.
    opaque_transient_surfaces = 1ULL << 33U,
    // The window's corner shape requested and reported
    // (HostServices::apply_window_corners; owner, 2026-11-08: the window
    // outer band follows the host's corners only where the host reports
    // them). Windows 11 DWM corner preference today; macOS and X11 report
    // nothing and the band draws square. Additive flag; no protocol version
    // change.
    window_corners = 1ULL << 34U,
    // The host reports the native window's state (HostWindowStateEvent:
    // full screen, and the changes that can reshape its corners, such as
    // maximize, snap and restore) after attach and whenever it changes
    // (owner, 2026-11-08: the window outer band hides in full screen when
    // declared so). Windows, macOS and X11. Host protocol version 12.
    window_state_reports = 1ULL << 35U,
};

[[nodiscard]] constexpr HostCapability operator|(HostCapability left,
                                                  HostCapability right) noexcept {
    return static_cast<HostCapability>(static_cast<std::uint64_t>(left) |
                                       static_cast<std::uint64_t>(right));
}

[[nodiscard]] constexpr bool has_capability(HostCapability available,
                                            HostCapability requested) noexcept {
    return (static_cast<std::uint64_t>(available) &
            static_cast<std::uint64_t>(requested)) ==
           static_cast<std::uint64_t>(requested);
}

} // namespace gui_forms
