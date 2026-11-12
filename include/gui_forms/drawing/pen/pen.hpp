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
    [[nodiscard]] PenSnapshot snapshot() const;

private:
    Color color_;
    double width_{1.0};
    DashStyle dash_style_{DashStyle::solid};
    std::vector<double> dash_pattern_;
    LineJoin join_{LineJoin::miter};
    double miter_limit_{4.0};
    LineCap cap_{LineCap::flat};
};

} // namespace gui_drawing
