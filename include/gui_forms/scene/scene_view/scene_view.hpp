#pragma once

// The view onto a graph-space scene (node graph spec section 3; the node
// engine study, sections 2.2, 2.4 and 2.6): the transform between world
// units and logical view units with pan, zoom about a point and framing,
// the level of detail with hysteresis, and the soft link curve with the
// geometry its hit testing and knife need. Everything here is a plain
// function of its arguments: no state, no allocation, no callbacks.
//
// view = (world - origin) * zoom, where `origin` is the world point at the
// view's top-left corner. World values are float64; a frame converts each
// point it draws once.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scene/world_geometry/world_geometry.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

// -- the view transform ------------------------------------------------------------

// The zooms a view is held to (spec 3: 1/64 to 8).
struct GUI_FORMS_API SceneZoomRange final {
    double minimum{1.0 / 64.0};
    double maximum{8.0};
};

struct GUI_FORMS_API SceneViewTransform final {
    WorldPoint origin{};
    double zoom{1.0};

    [[nodiscard]] Point to_view(WorldPoint world) const noexcept {
        const Point view{(world.x - origin.x) * zoom, (world.y - origin.y) * zoom};
        return view;
    }
    [[nodiscard]] WorldPoint to_world(Point view) const noexcept {
        const WorldPoint world{origin.x + view.x / zoom, origin.y + view.y / zoom};
        return world;
    }
    [[nodiscard]] Rect rect_to_view(const WorldRect& world) const noexcept {
        const Point corner = to_view(WorldPoint{world.x, world.y});
        const Rect view{corner.x, corner.y, world.width * zoom, world.height * zoom};
        return view;
    }
    [[nodiscard]] WorldRect rect_to_world(const Rect& view) const noexcept {
        const WorldPoint corner = to_world(Point{view.x, view.y});
        const WorldRect world{corner.x, corner.y, view.width / zoom, view.height / zoom};
        return world;
    }
    friend bool operator==(const SceneViewTransform& left, const SceneViewTransform& right) noexcept {
        return left.origin == right.origin && left.zoom == right.zoom;
    }
};

// Whether the origin is finite and the zoom finite and positive.
[[nodiscard]] GUI_FORMS_API bool valid_scene_view(const SceneViewTransform& view) noexcept;

// `zoom` held to `range` (a NaN zoom becomes the minimum).
[[nodiscard]] GUI_FORMS_API double clamp_scene_zoom(double zoom, SceneZoomRange range) noexcept;

// `view` zoomed by `factor`, keeping the world point under `anchor` (view
// units) where it is; the zoom is held to `range`. A non-finite or
// non-positive factor returns `view` unchanged.
[[nodiscard]] GUI_FORMS_API SceneViewTransform scene_zoomed_about(const SceneViewTransform& view, Point anchor,
                                                                  double factor, SceneZoomRange range) noexcept;

// `view` with the content moved by `delta` view units (the content follows
// the pointer: a positive x moves it right).
[[nodiscard]] GUI_FORMS_API SceneViewTransform scene_panned(const SceneViewTransform& view, Point delta) noexcept;

// The view showing `world` centred in a viewport of `size`, with `margin`
// view units around it, at the largest zoom in `range` that fits. An empty
// world rectangle keeps the view's zoom and centres the rectangle; a
// viewport smaller than twice the margin, or a world rectangle that is not
// valid_world_rect, returns `view` unchanged.
[[nodiscard]] GUI_FORMS_API SceneViewTransform scene_framed(const SceneViewTransform& view, const WorldRect& world,
                                                            Size size, double margin, SceneZoomRange range) noexcept;

// -- level of detail -----------------------------------------------------------------

// What a scene draws of each record at a zoom (study 2.6): `overview` a
// filled silhouette, `silhouette` the outline, `compact` the caption and
// ports, `full` everything, the body included.
enum class SceneDetailLevel : std::uint8_t { overview, silhouette, compact, full };

// The zooms at which the level changes, and the hysteresis factor: a level
// is entered upward only at threshold * hysteresis or above, and left
// downward only below threshold / hysteresis, so a zoom resting near a
// threshold does not flicker between levels. The thresholds must ascend and
// the hysteresis be at least 1 (valid_scene_detail_thresholds).
struct GUI_FORMS_API SceneDetailThresholds final {
    double silhouette{0.1};
    double compact{0.3};
    double full{0.6};
    double hysteresis{1.1};
    friend bool operator==(const SceneDetailThresholds& left, const SceneDetailThresholds& right) noexcept {
        return left.silhouette == right.silhouette && left.compact == right.compact && left.full == right.full &&
               left.hysteresis == right.hysteresis;
    }
};

[[nodiscard]] GUI_FORMS_API bool valid_scene_detail_thresholds(const SceneDetailThresholds& thresholds) noexcept;

// The level for `zoom` without hysteresis (the level a fresh view starts
// at). Invalid thresholds or a non-finite zoom give `full`.
[[nodiscard]] GUI_FORMS_API SceneDetailLevel scene_detail_level_for(double zoom,
                                                                    const SceneDetailThresholds& thresholds) noexcept;

// The level after `current` for `zoom`, with hysteresis: it may move
// several levels at once (a frame-all from deep zoom), each crossing
// judged against its own threshold. Invalid thresholds or a non-finite zoom
// keep `current`.
[[nodiscard]] GUI_FORMS_API SceneDetailLevel next_scene_detail_level(SceneDetailLevel current, double zoom,
                                                                     const SceneDetailThresholds& thresholds) noexcept;

// The level's registered (snake_case) name.
[[nodiscard]] GUI_FORMS_API std::string_view scene_detail_level_name(SceneDetailLevel level) noexcept;

// -- links ----------------------------------------------------------------------------

// The edge of a record a link leaves or enters by; its outward normal
// orients the link's tangent there.
enum class SceneEdge : std::uint8_t { left, right, top, bottom };

// A cubic Bezier in world units: ends p0 and p3, control points p1 and p2.
struct GUI_FORMS_API SceneCubic final {
    WorldPoint p0{};
    WorldPoint p1{};
    WorldPoint p2{};
    WorldPoint p3{};
    friend bool operator==(const SceneCubic& left, const SceneCubic& right) noexcept {
        return left.p0 == right.p0 && left.p1 == right.p1 && left.p2 == right.p2 && left.p3 == right.p3;
    }
};

// The soft link curve's tangent lengths (world units): half the distance
// along each end's normal, held to [shortest, longest], plus `back_link`
// times the sideways distance when the far end lies behind the edge (a
// back-link loops round instead of cutting through the record).
struct GUI_FORMS_API SoftLinkShape final {
    double shortest{24.0};
    double longest{160.0};
    double back_link{0.25};
};

// The soft link from `from` (leaving by `from_edge`) to `to` (entering by
// `to_edge`): each control point lies along its end's outward normal.
[[nodiscard]] GUI_FORMS_API SceneCubic soft_link_cubic(WorldPoint from, SceneEdge from_edge, WorldPoint to,
                                                       SceneEdge to_edge, const SoftLinkShape& shape) noexcept;

// The control points' bounding box, which holds the whole curve.
[[nodiscard]] GUI_FORMS_API WorldRect scene_cubic_bounds(const SceneCubic& cubic) noexcept;

// Writes `segments + 1` points of `cubic` (uniform in its parameter, ends
// included), converted to view units by `view`, into `out`, and returns how
// many it wrote: fewer when `out` is shorter. Zero segments count as one.
[[nodiscard]] GUI_FORMS_API std::size_t flatten_scene_cubic(const SceneCubic& cubic, std::size_t segments,
                                                            const SceneViewTransform& view,
                                                            std::span<Point> out) noexcept;

// The shortest distance from `point` to the polyline (infinity for no
// points; the distance to the point for one).
[[nodiscard]] GUI_FORMS_API double scene_distance_to_polyline(std::span<const Point> points, Point point) noexcept;

// Whether segment a-b and segment c-d share a point (touching and collinear
// overlap count).
[[nodiscard]] GUI_FORMS_API bool scene_segments_cross(Point a, Point b, Point c, Point d) noexcept;

// Whether any segment of the polyline shares a point with segment c-d.
[[nodiscard]] GUI_FORMS_API bool scene_polyline_crosses_segment(std::span<const Point> points, Point c,
                                                                Point d) noexcept;

} // namespace gui_forms
