// The in-tree scale frame's map (stage VA): uniform scale and translation.

#include "gui_forms/types/scale_frame/scale_frame.hpp"

namespace gui_forms {

bool is_identity_frame(const ScaleFrame& frame) noexcept {
    const bool identity = frame.origin.x == 0.0 && frame.origin.y == 0.0 && frame.scale == 1.0;
    return identity;
}

Point frame_to_outer(const ScaleFrame& frame, Point inner) noexcept {
    const Point outer{frame.origin.x + frame.scale * inner.x, frame.origin.y + frame.scale * inner.y};
    return outer;
}

Rect frame_to_outer(const ScaleFrame& frame, Rect inner) noexcept {
    const Rect outer{frame.origin.x + frame.scale * inner.x, frame.origin.y + frame.scale * inner.y,
                     frame.scale * inner.width, frame.scale * inner.height};
    return outer;
}

Point frame_to_inner(const ScaleFrame& frame, Point outer) noexcept {
    const Point inner{(outer.x - frame.origin.x) / frame.scale, (outer.y - frame.origin.y) / frame.scale};
    return inner;
}

Rect frame_to_inner(const ScaleFrame& frame, Rect outer) noexcept {
    const Rect inner{(outer.x - frame.origin.x) / frame.scale, (outer.y - frame.origin.y) / frame.scale,
                     outer.width / frame.scale, outer.height / frame.scale};
    return inner;
}

ScaleFrame compose_frames(const ScaleFrame& outer, const ScaleFrame& inner) noexcept {
    // outer(inner(p)) = o_o + s_o * (o_i + s_i * p).
    const ScaleFrame composed{frame_to_outer(outer, inner.origin), outer.scale * inner.scale};
    return composed;
}

} // namespace gui_forms
