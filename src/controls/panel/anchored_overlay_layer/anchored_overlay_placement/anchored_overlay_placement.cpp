#include "gui_forms/controls/panel/anchored_overlay_layer/anchored_overlay_placement/anchored_overlay_placement.hpp"

#include "../anchored_overlay_placement_utilities.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

using namespace anchored_overlay_detail;
AnchoredOverlayPlacementResult resolve_anchored_overlay(
    Rect anchor_bounds, Size client_size, AnchoredOverlayPlacement placement) {
    validate_placement(placement);
    if (!finite_rect(anchor_bounds) || anchor_bounds.width < 0.0 ||
        anchor_bounds.height < 0.0 || !std::isfinite(client_size.width) ||
        !std::isfinite(client_size.height) || client_size.width < 0.0 ||
        client_size.height < 0.0) {
        throw std::invalid_argument(
            "anchored overlay resolution requires finite nonnegative bounds");
    }

    const double margin_x = std::min(placement.viewport_margin,
                                     client_size.width * 0.5);
    const double margin_y = std::min(placement.viewport_margin,
                                     client_size.height * 0.5);
    const double available_width = std::max(0.0,
        client_size.width - margin_x * 2.0);
    const double available_height = std::max(0.0,
        client_size.height - margin_y * 2.0);
    const double width = std::min(placement.preferred_size.width,
                                  available_width);
    const double height = std::min(placement.preferred_size.height,
                                   available_height);

    double x = anchor_bounds.x;
    if (placement.horizontal_alignment == OverlayHorizontalAlignment::center) {
        x = anchor_bounds.x + (anchor_bounds.width - width) * 0.5;
    } else if (placement.horizontal_alignment == OverlayHorizontalAlignment::far) {
        x = anchor_bounds.x + anchor_bounds.width - width;
    }
    x = std::clamp(x, margin_x,
                   std::max(margin_x, client_size.width - margin_x - width));

    const double below_y = anchor_bounds.y + anchor_bounds.height + placement.gap;
    const double above_y = anchor_bounds.y - placement.gap - height;
    const bool fits_below = below_y + height <= client_size.height - margin_y;
    const bool fits_above = above_y >= margin_y;
    bool above = placement.vertical_preference == OverlayVerticalPreference::above;
    if (placement.allow_vertical_flip) {
        if (!above && !fits_below && fits_above) above = true;
        else if (above && !fits_above && fits_below) above = false;
    }
    double y = above ? above_y : below_y;
    y = std::clamp(y, margin_y,
                   std::max(margin_y, client_size.height - margin_y - height));

    return {{x, y, width, height}, above,
            width != placement.preferred_size.width,
            height != placement.preferred_size.height};
}

} // namespace gui_forms
