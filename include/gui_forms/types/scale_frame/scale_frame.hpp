#pragma once

// The in-tree scale frame's map (stage VA, the visible-area guarantee;
// notes planning/stages/VA-visible-area/STUDY.md, sections 5 and 9).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

namespace gui_forms {

// A uniform scale and a translation: the map of the in-tree scale frame
// (stage VA). A point p of the inner coordinates lies at origin + scale * p
// in the outer ones, so rectangles map to rectangles and nothing turns.
// The default is the identity. scale is positive and finite wherever the
// window makes a frame.
//
// A control's window frame (Control::window_frame) maps its local
// coordinates into its window's; its content frame (Control::
// content_frame) maps its children's coordinates into its own, and is the
// identity unless the window carries out a contain or cover of its
// viewport.
struct GUI_FORMS_API ScaleFrame final {
    Point origin{};
    double scale{1.0};

    friend bool operator==(const ScaleFrame& left, const ScaleFrame& right) noexcept {
        const bool equal = left.origin == right.origin && left.scale == right.scale;
        return equal;
    }
    friend bool operator!=(const ScaleFrame& left, const ScaleFrame& right) noexcept {
        const bool different = !(left == right);
        return different;
    }
};

// Whether `frame` is the identity: no translation and scale 1.
[[nodiscard]] GUI_FORMS_API bool is_identity_frame(const ScaleFrame& frame) noexcept;
// An inner point or rectangle in the outer coordinates.
[[nodiscard]] GUI_FORMS_API Point frame_to_outer(const ScaleFrame& frame, Point inner) noexcept;
[[nodiscard]] GUI_FORMS_API Rect frame_to_outer(const ScaleFrame& frame, Rect inner) noexcept;
// An outer point or rectangle in the inner coordinates: the inverse map.
// `frame`'s scale must be positive.
[[nodiscard]] GUI_FORMS_API Point frame_to_inner(const ScaleFrame& frame, Point outer) noexcept;
[[nodiscard]] GUI_FORMS_API Rect frame_to_inner(const ScaleFrame& frame, Rect outer) noexcept;
// The frame that maps as `inner` and then `outer`:
// frame_to_outer(compose_frames(outer, inner), p) is
// frame_to_outer(outer, frame_to_outer(inner, p)).
[[nodiscard]] GUI_FORMS_API ScaleFrame compose_frames(const ScaleFrame& outer, const ScaleFrame& inner) noexcept;

} // namespace gui_forms
