// Part shapes (T2 plan, phase E8): signed distances, inside tests, pixel
// coverage and hit regions.

#include "gui_forms/engine/part_shape/part_shape.hpp"

#include <algorithm>
#include <cmath>

// The same inputs give the same distances and coverage on every host: no
// fused multiply-add (T2 plan, "Numerics"); sqrt is exact in IEEE 754.
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms {

PartCornerRadii part_corner_radii(PartShape shape, Rect box) noexcept {
    const double half = std::min(box.width, box.height) * 0.5;
    const bool across = box.width >= box.height;
    // Square corners: the count sentinel's and the fallback's radii.
    const PartCornerRadii square{};
    switch (shape.kind) {
    case PartShapeKind::rounded_rectangle: {
        if (shape.per_corner) {
            const PartCornerRadii each{std::clamp(shape.corners.top_left, 0.0, half),
                                       std::clamp(shape.corners.top_right, 0.0, half),
                                       std::clamp(shape.corners.bottom_right, 0.0, half),
                                       std::clamp(shape.corners.bottom_left, 0.0, half)};
            return each;
        }
        const double radius = std::clamp(shape.corner_radius, 0.0, half);
        const PartCornerRadii rounded{radius, radius, radius, radius};
        return rounded;
    }
    case PartShapeKind::pill:
    case PartShapeKind::circle: {
        const PartCornerRadii round_ends{half, half, half, half};
        return round_ends;
    }
    case PartShapeKind::cap_start: {
        // The start is the left of a box lying across, the top of one
        // standing upright.
        const PartCornerRadii start =
            across ? PartCornerRadii{half, 0.0, 0.0, half} : PartCornerRadii{half, half, 0.0, 0.0};
        return start;
    }
    case PartShapeKind::cap_end: {
        const PartCornerRadii end =
            across ? PartCornerRadii{0.0, half, half, 0.0} : PartCornerRadii{0.0, 0.0, half, half};
        return end;
    }
    case PartShapeKind::count:
        // The count sentinel is no shape.
        return square;
    }
    // A kind outside the enumeration has square corners.
    return square;
}

double shape_signed_distance(PartShape shape, Rect box, Point point) noexcept {
    const double centre_x = box.x + box.width * 0.5;
    const double centre_y = box.y + box.height * 0.5;
    const double dx = point.x - centre_x;
    const double dy = point.y - centre_y;
    if (shape.kind == PartShapeKind::circle) {
        const double radius = std::min(box.width, box.height) * 0.5;
        const double distance = std::sqrt(dx * dx + dy * dy) - radius;
        return distance;
    }
    // The radius of the corner whose quadrant holds the point.
    const PartCornerRadii radii = part_corner_radii(shape, box);
    double radius = 0.0;
    if (dx < 0.0) {
        radius = dy < 0.0 ? radii.top_left : radii.bottom_left;
    } else {
        radius = dy < 0.0 ? radii.top_right : radii.bottom_right;
    }
    // A rounded box's distance: the point's offset beyond the inner
    // rectangle whose corners are the arcs' centres.
    const double qx = std::fabs(dx) - (box.width * 0.5 - radius);
    const double qy = std::fabs(dy) - (box.height * 0.5 - radius);
    const double outside_x = std::max(qx, 0.0);
    const double outside_y = std::max(qy, 0.0);
    const double outside = std::sqrt(outside_x * outside_x + outside_y * outside_y);
    const double inside = std::min(std::max(qx, qy), 0.0);
    const double distance = outside + inside - radius;
    return distance;
}

bool shape_contains(PartShape shape, Rect box, Point point) noexcept {
    if (!(point.x >= box.x && point.x < box.x + box.width && point.y >= box.y && point.y < box.y + box.height)) {
        return false;
    }
    const bool inside = shape_signed_distance(shape, box, point) <= 0.0;
    return inside;
}

double shape_coverage(PartShape shape, Rect box, double scale, std::int64_t x, std::int64_t y) noexcept {
    const Point centre{(static_cast<double>(x) + 0.5) / scale, (static_cast<double>(y) + 0.5) / scale};
    const double device_distance = shape_signed_distance(shape, box, centre) * scale;
    const double coverage = std::clamp(0.5 - device_distance, 0.0, 1.0);
    return coverage;
}

bool part_hit(const PartHitRegion& region, Point point) noexcept {
    const Rect bounds = region.bounds;
    if (!(point.x >= bounds.x && point.x < bounds.x + bounds.width && point.y >= bounds.y &&
          point.y < bounds.y + bounds.height)) {
        return false;
    }
    if (shape_contains(region.shape, bounds, point)) return true;
    // The minimum target, centred and clipped to the bounds.
    const double width = std::min(minimum_hit_target, bounds.width);
    const double height = std::min(minimum_hit_target, bounds.height);
    const double left = bounds.x + (bounds.width - width) * 0.5;
    const double top = bounds.y + (bounds.height - height) * 0.5;
    const bool in_minimum = point.x >= left && point.x < left + width && point.y >= top && point.y < top + height;
    return in_minimum;
}

} // namespace gui_forms
