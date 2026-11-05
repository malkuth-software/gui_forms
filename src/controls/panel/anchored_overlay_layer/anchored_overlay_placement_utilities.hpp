#pragma once

#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_placement/anchored_overlay_placement.hpp"

#include <cmath>
#include <stdexcept>

namespace gui_forms::anchored_overlay_detail {

[[nodiscard]] inline bool finite_rect(Rect value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.width) && std::isfinite(value.height);
}

inline void validate_placement(const AnchoredOverlayPlacement& placement) {
    if (!std::isfinite(placement.preferred_size.width) ||
        !std::isfinite(placement.preferred_size.height) ||
        placement.preferred_size.width < 0.0 ||
        placement.preferred_size.height < 0.0 ||
        !std::isfinite(placement.gap) || placement.gap < 0.0 ||
        !std::isfinite(placement.viewport_margin) ||
        placement.viewport_margin < 0.0) {
        throw std::invalid_argument(
            "anchored overlay placement requires finite nonnegative geometry");
    }
}

} // namespace gui_forms::anchored_overlay_detail
