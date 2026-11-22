#pragma once
// The Windows host's precision-touchpad gestures (DirectManipulation,
// src/surface/windows/input/windows_direct_manipulation.hpp) as portable
// events. DirectManipulation reports one content transform (scale and
// offset, device pixels, since the gesture's viewport was last reset) and
// the viewport's status; these rules turn each report into a GestureEvent
// (a pinch: magnify, with the fingers' travel as its translation) or a
// precise wheel event (a two-finger pan, with its phases and the system's
// inertia as momentum), since a window that takes the touchpad contact from
// DirectManipulation no longer receives the pan as WM_MOUSEWHEEL. Plain
// values in, no windows.h, so the rules are tested on every platform
// (windows_manipulation_rules_tests).
#include "gui_forms/events/gesture_events/gesture_events.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>

namespace gui_forms::host::windows_detail {

// A DirectManipulation viewport status (DIRECTMANIPULATION_STATUS) as the
// rules read it: ready (no contact, no inertia), running (the fingers are
// down), inertia (the fingers left and the system continues the pan), or
// any other status (building, enabled, disabled, suspended, autoscrolling),
// which ends a gesture in progress as ready does.
enum class ManipulationStatus : std::uint8_t { ready, running, inertia, other };

// One window's gesture in progress.
struct ManipulationTracker final {
  // The content transform last reported (device pixels).
  double scale{1.0};
  Point offset{};
  // Between a running status and the next ready.
  bool running{};
  // The gesture scaled: it is a pinch (magnify) to its end.
  bool pinching{};
  // A pinch ended (the fingers left): later transforms of the gesture are
  // dropped.
  bool finished{};
  // A pan's wheel began was dispatched.
  bool panned{};
  // The viewport reported inertia.
  bool inertia{};
};

// What one report dispatches, in order: the wheel event (when has_wheel),
// then the gesture (when has_gesture). reset_content asks the host to
// reset the viewport's transform to identity, so the next gesture starts
// from scale 1 and no offset.
struct ManipulationReport final {
  bool has_wheel{};
  PointerEvent wheel{};
  bool has_gesture{};
  GestureEvent gesture{};
  bool reset_content{};
};

// A content transform report: `scale` and `device_offset` are the
// content's transform (DirectManipulation's matrix: m11 and m31, m32).
// `focal` is the pointer's position in logical units (on a touchpad the
// pointer is where the fingers work), `window_scale` the window's device
// pixels per logical unit. The change since the previous report, as a
// transform about the focal point, is a pinch once its scale moves (and
// stays one to its end), otherwise a pan (wheel pixels; the portable wheel
// delta is positive to scroll up and left, which is where the content
// moves with the fingers).
[[nodiscard]] ManipulationReport manipulation_transform(ManipulationTracker &tracker, double scale,
                                                        Point device_offset, Point focal,
                                                        double window_scale, Modifier modifiers,
                                                        std::uint64_t timestamp_nanoseconds) noexcept;

// A viewport status report, the gesture's events closed or begun as the
// status says.
[[nodiscard]] ManipulationReport manipulation_status(ManipulationTracker &tracker, ManipulationStatus status,
                                                     Point focal, Modifier modifiers,
                                                     std::uint64_t timestamp_nanoseconds) noexcept;

} // namespace gui_forms::host::windows_detail
