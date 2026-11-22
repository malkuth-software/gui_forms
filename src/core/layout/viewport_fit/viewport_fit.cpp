// The visible-area guarantee's values (stage VA): names, validation and the
// one placement computation every contain and cover uses.

#include "gui_forms/layout/viewport_fit/viewport_fit.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {
namespace {

bool valid_alignment(ContentAlignment alignment) noexcept {
    const std::uint8_t value = static_cast<std::uint8_t>(alignment);
    const bool valid = value <= static_cast<std::uint8_t>(ContentAlignment::bottom_right);
    return valid;
}

// The fraction of the leftover space before the content on each axis: 0 at
// the start, one half in the middle, 1 at the end.
struct AlignmentFractions final {
    double horizontal{};
    double vertical{};
};

AlignmentFractions alignment_fractions(ContentAlignment alignment) noexcept {
    AlignmentFractions fractions{};
    switch (alignment) {
    case ContentAlignment::top_left: fractions = AlignmentFractions{0.0, 0.0}; break;
    case ContentAlignment::top_center: fractions = AlignmentFractions{0.5, 0.0}; break;
    case ContentAlignment::top_right: fractions = AlignmentFractions{1.0, 0.0}; break;
    case ContentAlignment::middle_left: fractions = AlignmentFractions{0.0, 0.5}; break;
    case ContentAlignment::middle_center: fractions = AlignmentFractions{0.5, 0.5}; break;
    case ContentAlignment::middle_right: fractions = AlignmentFractions{1.0, 0.5}; break;
    case ContentAlignment::bottom_left: fractions = AlignmentFractions{0.0, 1.0}; break;
    case ContentAlignment::bottom_center: fractions = AlignmentFractions{0.5, 1.0}; break;
    case ContentAlignment::bottom_right: fractions = AlignmentFractions{1.0, 1.0}; break;
    default:
        // Unreachable for a validated fit; centred, as the default fit is.
        fractions = AlignmentFractions{0.5, 0.5};
        break;
    }
    return fractions;
}

bool usable_extent(Size extent) noexcept {
    const bool usable = std::isfinite(extent.width) && std::isfinite(extent.height) && extent.width > 0.0 &&
                        extent.height > 0.0;
    return usable;
}

// Within the viewport, allowing for rounding in the scaled extent.
constexpr double placement_tolerance = 1.0e-9;

} // namespace

const char* overflow_answer_name(OverflowAnswer answer) noexcept {
    switch (answer) {
    case OverflowAnswer::scroll: return "scroll";
    case OverflowAnswer::contain: return "contain";
    case OverflowAnswer::cover: return "cover";
    case OverflowAnswer::clip: return "clip";
    }
    return "unknown";
}

const char* overflow_cause_name(OverflowCause cause) noexcept {
    switch (cause) {
    case OverflowCause::minimum_content_exceeds_viewport: return "minimum_content_exceeds_viewport";
    case OverflowCause::design_size_exceeds_viewport: return "design_size_exceeds_viewport";
    case OverflowCause::placed_child_outside_viewport: return "placed_child_outside_viewport";
    case OverflowCause::minimum_size_exceeds_work_area: return "minimum_size_exceeds_work_area";
    }
    return "unknown";
}

bool valid_viewport_fit(const ViewportFit& fit) noexcept {
    const bool answers = is_valid_overflow_answer(fit.answer) &&
                         (fit.below_minimum == OverflowAnswer::scroll || fit.below_minimum == OverflowAnswer::clip);
    const bool minimum = std::isfinite(fit.minimum_scale) && fit.minimum_scale >= 0.0;
    const bool maximum = std::isfinite(fit.maximum_scale) && fit.maximum_scale > 0.0 &&
                         fit.maximum_scale >= fit.minimum_scale && fit.maximum_scale <= maximum_viewport_scale;
    const bool valid = answers && minimum && maximum && valid_alignment(fit.alignment);
    return valid;
}

ViewportPlacement place_viewport_content(Size content, Rect viewport, const ViewportFit& requested) noexcept {
    const ViewportFit fit = valid_viewport_fit(requested) ? requested : ViewportFit{};
    ViewportPlacement placement{};
    placement.applied = fit.answer;
    const Size viewport_extent{viewport.width, viewport.height};
    if (!usable_extent(content) || !usable_extent(viewport_extent)) {
        placement.scale = 1.0;
        placement.destination = Rect{viewport.x, viewport.y, 0.0, 0.0};
        placement.fits = true;
        return placement;
    }
    const double ratio_x = viewport.width / content.width;
    const double ratio_y = viewport.height / content.height;
    double scale = 1.0;
    bool aligned = false;
    switch (fit.answer) {
    case OverflowAnswer::contain: {
        const double ratio = std::min(ratio_x, ratio_y);
        if (ratio < fit.minimum_scale) {
            placement.applied = fit.below_minimum;
            scale = fit.minimum_scale;
        } else {
            scale = std::min(ratio, fit.maximum_scale);
            aligned = true;
        }
        break;
    }
    case OverflowAnswer::cover: {
        const double ratio = std::max(ratio_x, ratio_y);
        scale = std::clamp(ratio, fit.minimum_scale, fit.maximum_scale);
        aligned = true;
        break;
    }
    case OverflowAnswer::scroll:
    case OverflowAnswer::clip:
        scale = 1.0;
        break;
    default:
        // A validated fit has one of the four answers; placed as scroll.
        placement.applied = OverflowAnswer::scroll;
        scale = 1.0;
        break;
    }
    // Every scale here is positive: the ratios are (both extents are), and
    // below_minimum is taken only when minimum_scale exceeds a ratio.
    const double width = content.width * scale;
    const double height = content.height * scale;
    double x = viewport.x;
    double y = viewport.y;
    if (aligned) {
        const AlignmentFractions fractions = alignment_fractions(fit.alignment);
        x = viewport.x + (viewport.width - width) * fractions.horizontal;
        y = viewport.y + (viewport.height - height) * fractions.vertical;
    }
    placement.scale = scale;
    placement.destination = Rect{x, y, width, height};
    const bool inside_x = x >= viewport.x - placement_tolerance &&
                          x + width <= viewport.x + viewport.width + placement_tolerance;
    const bool inside_y = y >= viewport.y - placement_tolerance &&
                          y + height <= viewport.y + viewport.height + placement_tolerance;
    placement.fits = inside_x && inside_y;
    return placement;
}

bool valid_safe_insets(const SafeInsets& insets) noexcept {
    const bool finite = std::isfinite(insets.left) && std::isfinite(insets.top) && std::isfinite(insets.right) &&
                        std::isfinite(insets.bottom);
    const bool not_negative = insets.left >= 0.0 && insets.top >= 0.0 && insets.right >= 0.0 && insets.bottom >= 0.0;
    const bool valid = finite && not_negative;
    return valid;
}

ScaleFrame placement_frame(Rect content, const ViewportPlacement& placement) noexcept {
    const double scale = placement.scale;
    const Point origin{placement.destination.x - scale * content.x, placement.destination.y - scale * content.y};
    const ScaleFrame frame{origin, scale};
    return frame;
}

ViewportFit fit_within_outer_scale(const ViewportFit& fit, double outer_scale) noexcept {
    ViewportFit adjusted = fit;
    // Both kept within [0, maximum_viewport_scale], so the result is valid.
    adjusted.minimum_scale = std::min(fit.minimum_scale / outer_scale, maximum_viewport_scale);
    const double maximum = std::min(fit.maximum_scale / outer_scale, maximum_viewport_scale);
    adjusted.maximum_scale = std::max(maximum, adjusted.minimum_scale);
    return adjusted;
}

} // namespace gui_forms
