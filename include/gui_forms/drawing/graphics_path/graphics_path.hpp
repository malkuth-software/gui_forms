#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/matrix/matrix.hpp"
#include "gui_forms/drawing/object/drawing_object.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_drawing {

enum class PathVerb : std::uint8_t {
    start_figure = 0,
    line = 1,
    rectangle = 2,
    ellipse = 3,
    arc = 4,
    close_figure = 5,
    quadratic = 6,
    bezier = 7,
};

struct GUI_FORMS_API PathElement final {
    PathVerb verb{};
    PointF first{};
    PointF second{};
    PointF third{};
    PointF fourth{};
    RectF rect{};
    double start_angle{};
    double sweep_angle{};
};

struct GUI_FORMS_API PathSnapshot final {
    FillMode fill_mode{FillMode::alternate};
    std::vector<PathElement> elements;
};

// One figure of a flattened path (GraphicsPath::flatten): its points are
// those of the flattened point array from the previous figure's `end` (0
// for the first) to its own `end`, exclusive. A closed figure repeats its
// first point last, so every figure is a polyline as it stands.
struct GUI_FORMS_API FlattenedFigure final {
    std::size_t end{};
    bool closed{};
};

enum class PathFlatteningStatus : std::uint8_t {
    // Every figure was written (or, for flattened_size, counted).
    complete = 0,
    // The caller's arrays are smaller than the counts returned; nothing
    // was written.
    storage_too_small = 1,
    // The path at this tolerance needs more than
    // GraphicsPath::maximum_flattened_points points; nothing was written
    // and the counts are zero.
    limit_exceeded = 2,
};

// What a flattening needs or wrote: the points and figures, and the status.
struct GUI_FORMS_API PathFlattening final {
    std::size_t point_count{};
    std::size_t figure_count{};
    PathFlatteningStatus status{PathFlatteningStatus::complete};
};

class GUI_FORMS_API GraphicsPath final : public DrawingObject {
public:
    static constexpr std::size_t maximum_elements = 1'000'000;
    // The most points one flattening writes (2^24, 128 MiB of PointF): a
    // tolerance tiny against the path's size is refused, not truncated.
    static constexpr std::size_t maximum_flattened_points = 16'777'216;

    explicit GraphicsPath(FillMode fill_mode = FillMode::alternate);
    [[nodiscard]] FillMode fill_mode() const;
    void set_fill_mode(FillMode fill_mode);
    void reset();
    void start_figure();
    void close_figure();
    void add_line(PointF from, PointF to);
    void add_quadratic(PointF from, PointF control, PointF to);
    void add_bezier(PointF from, PointF control1, PointF control2, PointF to);
    void add_beziers(std::span<const PointF> points);
    void add_polygon(std::span<const PointF> points);
    void add_rectangle(RectF rectangle);
    void add_ellipse(RectF bounds);
    // One closed figure: `rectangle` with each corner rounded by its own
    // radius (held to half the shorter side), clockwise from the end of the
    // top-left corner. Each rounded corner is a cubic Bezier quarter (the
    // standard 0.5523 approximation, within 0.03 % of the radius of a
    // circle); a corner of radius 0 is square. Joined segments round only
    // a row's outer ends this way.
    void add_rounded_rectangle(RectF rectangle, CornerRadii radii);
    void add_arc(RectF bounds, double start_angle, double sweep_angle);
    void add_pie(RectF bounds, double start_angle, double sweep_angle);
    void add_path(const GraphicsPath& path, bool connect);
    void transform(const Matrix& matrix);
    [[nodiscard]] bool is_visible(PointF point) const;
    [[nodiscard]] std::vector<PointF> path_points() const;
    [[nodiscard]] std::unique_ptr<GraphicsPath> clone() const;
    [[nodiscard]] RectF bounds() const;
    [[nodiscard]] PathSnapshot snapshot() const;

    // Flattening: the path as polylines, every curve within `tolerance` of
    // the polyline that replaces it. Provisional, in the drawing ABI's 1.1
    // (GD_ABI_VERSION_1_1, outside the sealed 1.0); the C ABI's
    // graphics_path_flatten and C#'s GraphicsPath.Flatten are the same
    // operation.
    //
    // `tolerance` is a distance in the path's own coordinate units (the
    // units its points are stored in, after any transform()): no point of
    // a curve, arc or ellipse lies farther than `tolerance` from its chords
    // (up to rounding). A caller drawing the path at scale s passes the
    // device tolerance divided by s. It must be positive and finite, else
    // std::invalid_argument is thrown.
    //
    // Figures follow the raster's reading of the elements: a line, curve or
    // arc that does not start where the figure stands starts a new figure
    // (an arc joins it with a straight edge instead, as Windows Forms' AddArc does);
    // start_figure ends the figure open, close_figure ends it closed; a
    // rectangle (clockwise from its top-left corner) and an ellipse
    // (clockwise from its right-middle point) are closed figures of their
    // own. Arc angles are the ellipse's parameter angles in degrees, as the
    // raster draws them, with the sweep held to one turn either way. Lines
    // keep their end points; a curve's last point is its end point exactly.
    // A figure of fewer than two points is left out.
    //
    // flattened_size returns the counts flatten would write (status
    // complete or limit_exceeded). flatten fills `points` and `figures`,
    // which the caller owns and sizes, and returns what it wrote. When
    // either array is smaller than needed it writes nothing and returns
    // the counts needed with status storage_too_small. The chord counts are
    // a function of the tolerance and each element's geometry alone, so
    // the two calls agree.
    [[nodiscard]] PathFlattening flattened_size(double tolerance) const;
    [[nodiscard]] PathFlattening flatten(double tolerance, std::span<PointF> points,
                                         std::span<FlattenedFigure> figures) const;

private:
    void append(PathElement element);

    FillMode fill_mode_;
    std::vector<PathElement> elements_;
};

} // namespace gui_drawing
