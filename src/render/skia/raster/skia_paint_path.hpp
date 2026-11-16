#pragma once

// A PaintPath (paint_path.hpp) as a Skia path, shared by the Skia painter
// (SkiaRaster's fill_path, clip_path and stroke_path) and the GUI.Drawing
// executor (a pen's trimmed paths, cut in the core by trim_paint_path).
// Private to src/render/skia: no Skia type leaves it.

#include "gui_forms/types/paint_path/paint_path.hpp"

#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace gui_forms::skia_paint_path {

// The point count a path builder reserves, held within int.
[[nodiscard]] inline int reserved_path_points(std::size_t count) noexcept {
    constexpr std::size_t limit = std::size_t{1} << 24U;
    const std::size_t bounded = std::min(count, limit);
    return static_cast<int>(bounded);
}

[[nodiscard]] inline SkPoint path_point(Point point) noexcept {
    return SkPoint::Make(static_cast<SkScalar>(point.x), static_cast<SkScalar>(point.y));
}

// Appends an arc_to's arc (paint_path.hpp) to `builder`: a straight segment
// from the current point to its start unless `starts_contour`. A sweep
// beyond a half turn either way is drawn in two halves, so a full turn
// never reaches Skia as one arc whose ends coincide.
inline void append_arc(SkPathBuilder& builder, Point center, Point radii, Point angles, bool starts_contour) {
    const SkRect oval = SkRect::MakeLTRB(static_cast<SkScalar>(center.x - radii.x),
                                         static_cast<SkScalar>(center.y - radii.y),
                                         static_cast<SkScalar>(center.x + radii.x),
                                         static_cast<SkScalar>(center.y + radii.y));
    const double sweep = std::clamp(angles.y, -360.0, 360.0);
    if (std::fabs(sweep) <= 180.0) {
        builder.arcTo(oval, static_cast<SkScalar>(angles.x), static_cast<SkScalar>(sweep), starts_contour);
        return;
    }
    const double half = sweep * 0.5;
    builder.arcTo(oval, static_cast<SkScalar>(angles.x), static_cast<SkScalar>(half), starts_contour);
    builder.arcTo(oval, static_cast<SkScalar>(angles.x + half), static_cast<SkScalar>(half), false);
}

// `path` (validated by the caller) as a Skia path with `fill`.
[[nodiscard]] inline SkPath built_path(PaintPathView path, SkPathFillType fill) {
    SkPathBuilder builder(fill);
    builder.incReserve(reserved_path_points(path.points.size()));
    std::size_t consumed = 0U;
    bool have_current = false;
    for (const PaintPathVerb verb : path.verbs) {
        const std::span<const Point> operands = path.points.subspan(consumed, paint_path_points_for(verb));
        consumed = consumed + operands.size();
        switch (verb) {
        case PaintPathVerb::move_to: builder.moveTo(path_point(operands[0])); break;
        case PaintPathVerb::line_to: builder.lineTo(path_point(operands[0])); break;
        case PaintPathVerb::quad_to: builder.quadTo(path_point(operands[0]), path_point(operands[1])); break;
        case PaintPathVerb::cubic_to:
            builder.cubicTo(path_point(operands[0]), path_point(operands[1]), path_point(operands[2]));
            break;
        case PaintPathVerb::arc_to: append_arc(builder, operands[0], operands[1], operands[2], !have_current); break;
        case PaintPathVerb::close: builder.close(); break;
        default:
            // Unreachable for a validated path.
            break;
        }
        have_current = true;
    }
    return builder.detach();
}

} // namespace gui_forms::skia_paint_path
