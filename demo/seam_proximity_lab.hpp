#pragma once

#include "gui_forms/controls/split_container.hpp"
#include "gui_forms/window.hpp"

#include <memory>

namespace gui_forms::seam_proximity_lab {

[[nodiscard]] SplitSeamGeometry reference_seam_geometry();
[[nodiscard]] SplitSeamGeometry physical_hairline_geometry();
[[nodiscard]] bool invalid_geometry_is_rejected();
[[nodiscard]] const char* seam_state_name(SplitSeamState state) noexcept;
[[nodiscard]] std::unique_ptr<Window> make_seam_proximity_lab();

} // namespace gui_forms::seam_proximity_lab
