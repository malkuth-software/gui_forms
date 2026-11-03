#pragma once

#include "gui_forms/controls/range_control/range_control.hpp"

namespace gui_forms {

namespace range_control_detail {

inline constexpr double track_inset = 10.0;

void require_finite(double value, const char* message);

} // namespace range_control_detail
} // namespace gui_forms
