#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/brush/brush.hpp"

namespace gui_drawing {

struct GUI_FORMS_API PenSnapshot final {
    Color color;
    double width{1.0};
    DashStyle dash_style{DashStyle::solid};
    std::vector<double> dash_pattern;
    LineJoin join{LineJoin::miter};
    double miter_limit{4.0};
    LineCap cap{LineCap::flat};
    // The part of a drawn path or ellipse the pen keeps (set_trim): the
    // defaults keep it whole.
    double trim_start{0.0};
    double trim_end{1.0};
    double trim_offset{0.0};
};

class GUI_FORMS_API Pen final : public DrawingObject {
public:
    explicit Pen(Color color, double width = 1.0);
    explicit Pen(const Brush& brush, double width = 1.0);

    void set_width(double width);
    void set_dash_style(DashStyle style);
    void set_dash_pattern(std::span<const double> pattern);
    // How segments meet; `miter_limit` (finite, at least 1) bounds a miter's
    // length as a multiple of the pen width before it is bevelled. The
    // default, a miter limited to 4, is the stroker's own, so pens recorded
    // before joins were settable draw as they did.
    void set_line_join(LineJoin join, double miter_limit);
    void set_line_cap(LineCap cap);
    // Keeps only part of each path and ellipse the pen draws
    // (draw_path, draw_ellipse; owner, 2026-11-16): `start` and `end`
    // fractions of the figure's whole length, each in [0, 1] and in either
    // order, and `offset` (finite) moving the kept part along it, wrapping
    // across its start. The cut is the core's (gui_forms::trim_paint_path at
    // gui_forms::stroke_trim_tolerance), so the raster executor and a
    // Painter replay keep the same sub-path. Start 0 and end 1 keep the
    // whole figure, as before. Rectangles, lines and polylines draw whole.
    // Throws std::invalid_argument, changing nothing, for a value out of
    // range or not finite.
    void set_trim(double start, double end, double offset);
    [[nodiscard]] PenSnapshot snapshot() const;

private:
    Color color_;
    double width_{1.0};
    DashStyle dash_style_{DashStyle::solid};
    std::vector<double> dash_pattern_;
    LineJoin join_{LineJoin::miter};
    double miter_limit_{4.0};
    LineCap cap_{LineCap::flat};
    double trim_start_{0.0};
    double trim_end_{1.0};
    double trim_offset_{0.0};
};

} // namespace gui_drawing
