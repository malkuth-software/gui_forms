#pragma once
// The input settings a host reports (HostInputSettingsEvent) and the window
// keeps in PresentationSettings. Kept apart so that the host protocol's
// types can name them without the window's presentation types.
#include <cstdint>

namespace gui_forms {

// Where a setting the host reports came from.
enum class SettingOrigin : std::uint8_t {
    // GUI.Forms' stated default: the host has no such setting (a desktop
    // session that publishes none), or no host has reported yet.
    default_value,
    // The platform's own setting, read by the host.
    host,
};

// The double-click interval GUI.Forms states where the host has none, and
// the longest one admitted (Windows' own limit). Perceptual durations, in
// milliseconds.
inline constexpr std::uint32_t default_double_click_milliseconds = 400U;
inline constexpr std::uint32_t maximum_double_click_milliseconds = 5000U;

// The gesture thresholds' library defaults (APP_WINDOW_RULES_DESIGN C.2,
// FD-6 section 5), used where the host reports none. They are GUI.Forms'
// own choices, kept here and nowhere else; no control keeps thresholds of
// its own. Distances are in logical pixels along each axis from the press
// point (a square, not a circle); durations are perceptual, in
// milliseconds.
inline constexpr double default_gesture_slop = 2.0;
inline constexpr double default_drag_threshold = 4.0;
inline constexpr std::uint32_t default_hold_milliseconds = 500U;
// The largest distances and hold time a window takes.
inline constexpr double maximum_gesture_distance = 64.0;
inline constexpr std::uint32_t maximum_hold_milliseconds = 10000U;

// When the secondary button asks for a context menu: on its press (AppKit,
// most X11 toolkits) or on its release over the same control (Win32). The
// host reports its platform's choice; the library default is release.
enum class ContextRequestEdge : std::uint8_t {
    release,
    press,
};

// The thresholds the window's gesture recognizer reads (C.2). Each value
// carries where it came from: the host's own setting, or the library
// default above.
// - slop: how far the pointer may move from the press point and still
//   count as a second click of a double-click, or as a hold.
// - drag_threshold: how far the pointer must move, pressed, before a
//   control that takes drag gestures starts a drag.
// - hold_milliseconds: how long a press must rest within the slop to be a
//   hold.
// - context_request: the secondary button's edge that raises
//   context_request.
struct GestureSettings final {
    double slop{default_gesture_slop};
    SettingOrigin slop_origin{SettingOrigin::default_value};
    double drag_threshold{default_drag_threshold};
    SettingOrigin drag_threshold_origin{SettingOrigin::default_value};
    std::uint32_t hold_milliseconds{default_hold_milliseconds};
    SettingOrigin hold_origin{SettingOrigin::default_value};
    ContextRequestEdge context_request{ContextRequestEdge::release};
    SettingOrigin context_request_origin{SettingOrigin::default_value};

    friend constexpr bool operator==(const GestureSettings& left,
                                     const GestureSettings& right) noexcept {
        return left.slop == right.slop && left.slop_origin == right.slop_origin &&
               left.drag_threshold == right.drag_threshold &&
               left.drag_threshold_origin == right.drag_threshold_origin &&
               left.hold_milliseconds == right.hold_milliseconds &&
               left.hold_origin == right.hold_origin &&
               left.context_request == right.context_request &&
               left.context_request_origin == right.context_request_origin;
    }
};

// Whether every value lies in its range (each distance finite and between
// 0 and maximum_gesture_distance, the hold time between 1 and
// maximum_hold_milliseconds) and every origin and edge is in its enum.
[[nodiscard]] bool valid_gesture_settings(const GestureSettings& settings) noexcept;

} // namespace gui_forms
