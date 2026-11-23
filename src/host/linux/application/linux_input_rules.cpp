#include "linux_input_rules.hpp"

#include <cmath>

namespace gui_forms::host::linux_detail {

std::uint32_t next_click_count(LinuxClickHistory &history, unsigned button, std::uint32_t time,
                               Point position, std::uint32_t interval_milliseconds,
                               double distance) noexcept {
  const std::uint32_t elapsed = time - history.time;
  const bool same_button = history.count != 0U && history.button == button;
  const bool in_time = elapsed < interval_milliseconds;
  const bool near = std::fabs(position.x - history.position.x) <= distance &&
                    std::fabs(position.y - history.position.y) <= distance;
  const std::uint32_t count = same_button && in_time && near ? history.count % 3U + 1U : 1U;
  history.count = count;
  history.button = button;
  history.time = time;
  history.position = position;
  return count;
}

bool requests_keyboard_context(std::uint32_t physical_key, Modifier modifiers) noexcept {
  const bool menu_key = physical_key == PhysicalKey::context_menu && modifiers == Modifier::none;
  const bool shift_f10 = physical_key == PhysicalKey::f10 && modifiers == Modifier::shift;
  return menu_key || shift_f10;
}

GestureEvent next_pinch_event(LinuxPinchState &state, LinuxPinchStage stage, bool cancelled,
                              Point position, double absolute_scale,
                              double delta_angle_clockwise, Point delta, Modifier modifiers,
                              std::uint64_t timestamp_nanoseconds) noexcept {
  if (stage == LinuxPinchStage::begin)
    state.scale = 1.0;
  const bool usable_scale = std::isfinite(absolute_scale) && absolute_scale > 0.0;
  const double previous = state.scale > 0.0 && std::isfinite(state.scale) ? state.scale : 1.0;
  GestureEvent event{};
  event.kind = GestureKind::magnify;
  switch (stage) {
  case LinuxPinchStage::begin:
    event.gesture_phase = GesturePhase::began;
    break;
  case LinuxPinchStage::update:
    event.gesture_phase = GesturePhase::changed;
    break;
  case LinuxPinchStage::end:
    event.gesture_phase = cancelled ? GesturePhase::cancelled : GesturePhase::ended;
    break;
  default:
    event.gesture_phase = GesturePhase::changed;
    break;
  }
  event.position = position;
  event.scale = usable_scale ? absolute_scale / previous : 1.0;
  event.rotation_degrees = std::isfinite(delta_angle_clockwise) ? -delta_angle_clockwise : 0.0;
  event.translation = Point{std::isfinite(delta.x) ? delta.x : 0.0, std::isfinite(delta.y) ? delta.y : 0.0};
  event.modifiers = modifiers;
  event.timestamp_nanoseconds = timestamp_nanoseconds;
  if (usable_scale)
    state.scale = absolute_scale;
  if (stage == LinuxPinchStage::end)
    state.scale = 1.0;
  return event;
}

} // namespace gui_forms::host::linux_detail
