#include "gui_forms/types.hpp"

#include <algorithm>

namespace gui_forms {

namespace {

// One axis of a rectangle: [start, start + extent).
struct AxisSpan final {
    double start{};
    double extent{};
};

// One axis of an intersection. When one side's whole span lies within the
// other's, that side's start and extent are returned as they are: an
// extent recomputed as (start + extent) - start can lose its last bit when
// start is not a dyadic value (a row of exactly 40 under a 22.6 header
// came back 39.999999999999993), so a rectangle clipped by a container
// keeps its own extent bit for bit. Otherwise the overlap, empty (extent
// 0) when the spans are disjoint.
AxisSpan intersect_axis(AxisSpan left, AxisSpan right) noexcept {
    const double left_end = left.start + left.extent;
    const double right_end = right.start + right.extent;
    const bool left_inside = left.extent >= 0.0 && right.start <= left.start && left_end <= right_end;
    if (left_inside) return left;
    const bool right_inside = right.extent >= 0.0 && left.start <= right.start && right_end <= left_end;
    if (right_inside) return right;
    const double start = std::max(left.start, right.start);
    const double end = std::min(left_end, right_end);
    const AxisSpan overlap{start, std::max(0.0, end - start)};
    return overlap;
}

} // namespace

Rect Rect::intersection(Rect left, Rect right) noexcept {
    const AxisSpan horizontal = intersect_axis(AxisSpan{left.x, left.width}, AxisSpan{right.x, right.width});
    const AxisSpan vertical = intersect_axis(AxisSpan{left.y, left.height}, AxisSpan{right.y, right.height});
    const Rect result{horizontal.start, vertical.start, horizontal.extent, vertical.extent};
    return result;
}

Rect Rect::united(Rect left, Rect right) noexcept {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    const double x0 = std::min(left.x, right.x);
    const double y0 = std::min(left.y, right.y);
    const double x1 = std::max(left.x + left.width, right.x + right.width);
    const double y1 = std::max(left.y + left.height, right.y + right.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

} // namespace gui_forms
