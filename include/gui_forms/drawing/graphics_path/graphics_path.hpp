#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/matrix/matrix.hpp"
#include "gui_forms/drawing/object/drawing_object.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

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

class GUI_FORMS_API GraphicsPath final : public DrawingObject {
public:
    static constexpr std::size_t maximum_elements = 1'000'000;

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

private:
    void append(PathElement element);

    FillMode fill_mode_;
    std::vector<PathElement> elements_;
};

} // namespace gui_drawing
