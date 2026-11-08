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

} // namespace gui_forms::host::linux_detail
