#include "windows_manipulation_rules.hpp"

#include <cmath>

namespace gui_forms::host::windows_detail {

namespace {

// A scale change smaller than this is a pan's rounding, not a pinch.
constexpr double pinch_threshold = 1e-4;

GestureEvent pinch_event(GesturePhase phase, Point focal, Modifier modifiers,
                         std::uint64_t timestamp_nanoseconds) noexcept {
  GestureEvent event{};
  event.kind = GestureKind::magnify;
  event.gesture_phase = phase;
  event.position = focal;
  event.modifiers = modifiers;
  event.timestamp_nanoseconds = timestamp_nanoseconds;
  return event;
}

PointerEvent wheel_event(WheelPhase phase, Point focal, Point delta, Modifier modifiers,
                         std::uint64_t timestamp_nanoseconds) noexcept {
  PointerEvent event{};
  event.action = PointerAction::wheel;
  event.position = focal;
  event.wheel_delta = delta;
  event.wheel_unit = WheelDeltaUnit::pixels;
  event.wheel_phase = phase;
  event.modifiers = modifiers;
  event.pointer_id = 1;
  event.timestamp_nanoseconds = timestamp_nanoseconds;
  return event;
}

// Closes the gesture in progress into `report`: a pinch's ended, a pan's
// ended or (after inertia) momentum_ended.
void close_gesture(ManipulationTracker &tracker, ManipulationReport &report, Point focal, Modifier modifiers,
                   std::uint64_t timestamp_nanoseconds) noexcept {
  if (tracker.pinching && !tracker.finished) {
    report.has_gesture = true;
    report.gesture = pinch_event(GesturePhase::ended, focal, modifiers, timestamp_nanoseconds);
  }
  if (tracker.panned) {
    report.has_wheel = true;
    report.wheel = wheel_event(tracker.inertia ? WheelPhase::momentum_ended : WheelPhase::ended, focal,
                               Point{}, modifiers, timestamp_nanoseconds);
  }
  tracker.running = false;
  tracker.pinching = false;
  tracker.finished = false;
  tracker.panned = false;
  tracker.inertia = false;
}

} // namespace

bool direct_manipulation_allowed(const char *variable) noexcept {
  const bool off = variable != nullptr && variable[0] == '0' && variable[1] == '\0';
  return !off;
}

bool direct_manipulation_wanted(bool allowed, bool requested, bool engaged, bool running) noexcept {
  if (!allowed)
    return false;
  const bool wanted = requested || (engaged && running);
  return wanted;
}

ManipulationReport manipulation_transform(ManipulationTracker &tracker, double scale, Point device_offset,
                                          Point focal, double window_scale, Modifier modifiers,
                                          std::uint64_t timestamp_nanoseconds) noexcept {
  ManipulationReport report{};
  const bool usable = std::isfinite(scale) && scale > 0.0 && std::isfinite(device_offset.x) &&
                      std::isfinite(device_offset.y);
  if (!usable)
    return report;
  const double previous_scale = tracker.scale > 0.0 ? tracker.scale : 1.0;
  const Point previous_offset = tracker.offset;
  tracker.scale = scale;
  tracker.offset = device_offset;
  // A report outside a gesture (the reset's own update) only moves the
  // baseline; so does one after a pinch's fingers left.
  if (!tracker.running || tracker.finished)
    return report;
  const double pixels = std::isfinite(window_scale) && window_scale > 0.0 ? window_scale : 1.0;
  const double change = scale / previous_scale;
  // The change in window space, y -> change * y + (offset - change *
  // previous offset), as a scale about the focal point plus the fingers'
  // travel: travel = offset - change * previous offset - (1 - change) * focal.
  const Point device_focal{focal.x * pixels, focal.y * pixels};
  const Point device_travel{device_offset.x - change * previous_offset.x - (1.0 - change) * device_focal.x,
                            device_offset.y - change * previous_offset.y - (1.0 - change) * device_focal.y};
  const Point travel{device_travel.x / pixels, device_travel.y / pixels};
  const bool starts_pinch = !tracker.pinching && !tracker.inertia && std::fabs(change - 1.0) > pinch_threshold;
  if (starts_pinch) {
    if (tracker.panned) {
      report.has_wheel = true;
      report.wheel = wheel_event(WheelPhase::ended, focal, Point{}, modifiers, timestamp_nanoseconds);
      tracker.panned = false;
    }
    tracker.pinching = true;
    report.has_gesture = true;
    report.gesture = pinch_event(GesturePhase::began, focal, modifiers, timestamp_nanoseconds);
    report.gesture.scale = change;
    report.gesture.translation = travel;
    return report;
  }
  if (tracker.pinching) {
    report.has_gesture = true;
    report.gesture = pinch_event(GesturePhase::changed, focal, modifiers, timestamp_nanoseconds);
    report.gesture.scale = change;
    report.gesture.translation = travel;
    return report;
  }
  if (travel.x == 0.0 && travel.y == 0.0)
    return report;
  const WheelPhase phase = tracker.inertia ? WheelPhase::momentum
                           : tracker.panned ? WheelPhase::changed
                                            : WheelPhase::began;
  tracker.panned = true;
  report.has_wheel = true;
  report.wheel = wheel_event(phase, focal, travel, modifiers, timestamp_nanoseconds);
  return report;
}

ManipulationReport manipulation_status(ManipulationTracker &tracker, ManipulationStatus status, Point focal,
                                       Modifier modifiers, std::uint64_t timestamp_nanoseconds) noexcept {
  ManipulationReport report{};
  switch (status) {
  case ManipulationStatus::running:
    // Fingers down again during a pan's inertia start a new gesture.
    if (tracker.running && tracker.inertia)
      close_gesture(tracker, report, focal, modifiers, timestamp_nanoseconds);
    if (!tracker.running) {
      tracker.running = true;
      tracker.pinching = false;
      tracker.finished = false;
      tracker.panned = false;
      tracker.inertia = false;
    }
    break;
  case ManipulationStatus::inertia:
    if (!tracker.running)
      break;
    tracker.inertia = true;
    if (tracker.pinching && !tracker.finished) {
      // A pinch has no inertia: the fingers left, the pinch ended.
      report.has_gesture = true;
      report.gesture = pinch_event(GesturePhase::ended, focal, modifiers, timestamp_nanoseconds);
      tracker.finished = true;
    }
    if (tracker.panned) {
      report.has_wheel = true;
      report.wheel = wheel_event(WheelPhase::ended, focal, Point{}, modifiers, timestamp_nanoseconds);
    }
    break;
  case ManipulationStatus::ready:
  case ManipulationStatus::other:
    if (tracker.running) {
      close_gesture(tracker, report, focal, modifiers, timestamp_nanoseconds);
      report.reset_content = true;
    }
    break;
  default:
    break;
  }
  return report;
}

} // namespace gui_forms::host::windows_detail
