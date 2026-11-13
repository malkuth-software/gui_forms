#pragma once

// Graph space (world units): the coordinates of retained scenes that pan
// and zoom, such as the node graph a NodeCanvas draws (node engine study,
// section 2.2). World values are float64 so deep zoom keeps placement; a
// view converts them to logical units (Point, Rect) once per frame
// (SceneViewTransform). The node graph's records, the spatial index and the
// scene view all speak these two types.

#include "gui_forms/export/application_export.hpp"

#include <cmath>

namespace gui_forms {

// A point in graph space (world units, float64).
struct GUI_FORMS_API WorldPoint final {
    double x{};
    double y{};
    friend bool operator==(WorldPoint left, WorldPoint right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

// An axis-aligned rectangle in graph space: its top-left corner and size.
struct GUI_FORMS_API WorldRect final {
    double x{};
    double y{};
    double width{};
    double height{};
    [[nodiscard]] double right() const noexcept { return x + width; }
    [[nodiscard]] double bottom() const noexcept { return y + height; }
    // Whether `point` lies inside or on the edge.
    [[nodiscard]] bool contains(WorldPoint point) const noexcept {
        const bool inside = point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
        return inside;
    }
    friend bool operator==(const WorldRect& left, const WorldRect& right) noexcept {
        return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
    }
};

// Whether every field is finite and the size is not negative: the
// rectangles the spatial index and the scene view accept.
[[nodiscard]] inline bool valid_world_rect(const WorldRect& rect) noexcept {
    const bool finite = std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
                        std::isfinite(rect.height);
    const bool sized = rect.width >= 0.0 && rect.height >= 0.0;
    return finite && sized;
}

// Whether the two closed rectangles share a point (touching edges count).
[[nodiscard]] inline bool world_rects_intersect(const WorldRect& left, const WorldRect& right) noexcept {
    const bool apart = left.right() < right.x || right.right() < left.x || left.bottom() < right.y ||
                       right.bottom() < left.y;
    return !apart;
}

// Whether `inner` lies wholly inside `outer` (edges included).
[[nodiscard]] inline bool world_rect_contains(const WorldRect& outer, const WorldRect& inner) noexcept {
    const bool inside = inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
                        inner.bottom() <= outer.bottom();
    return inside;
}

// The smallest rectangle holding both.
[[nodiscard]] inline WorldRect world_rect_union(const WorldRect& left, const WorldRect& right) noexcept {
    const double x = std::fmin(left.x, right.x);
    const double y = std::fmin(left.y, right.y);
    const double far_x = std::fmax(left.right(), right.right());
    const double far_y = std::fmax(left.bottom(), right.bottom());
    const WorldRect joined{x, y, far_x - x, far_y - y};
    return joined;
}

// `rect` grown by `margin` on every side (a negative margin shrinks it,
// never below an empty rectangle at its centre).
[[nodiscard]] inline WorldRect world_rect_inflated(const WorldRect& rect, double margin) noexcept {
    double width = rect.width + 2.0 * margin;
    double height = rect.height + 2.0 * margin;
    double x = rect.x - margin;
    double y = rect.y - margin;
    if (width < 0.0) {
        x = rect.x + rect.width / 2.0;
        width = 0.0;
    }
    if (height < 0.0) {
        y = rect.y + rect.height / 2.0;
        height = 0.0;
    }
    const WorldRect grown{x, y, width, height};
    return grown;
}

} // namespace gui_forms
