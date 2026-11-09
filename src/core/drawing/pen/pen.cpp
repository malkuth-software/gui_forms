#include "../support/drawing_support.hpp"

Pen::Pen(Color color, double width) : color_(color), width_(width) {
    require_pen_width(width);
}

Pen::Pen(const Brush& brush, double width)
    : color_(brush.snapshot().primary), width_(width) {
    require_pen_width(width);
}

void Pen::set_width(double width) {
    require_alive();
    require_pen_width(width);
    width_ = width;
}

void Pen::set_dash_style(DashStyle style) {
    require_alive();
    require_enum(style, 5U, "dash style");
    dash_style_ = style;
    if (style != DashStyle::custom) dash_pattern_.clear();
}

void Pen::set_dash_pattern(std::span<const double> pattern) {
    require_alive();
    if (pattern.empty() || pattern.size() > 256) {
        throw std::invalid_argument("dash pattern must contain 1 through 256 entries");
    }
    std::vector<double> copy;
    copy.reserve(pattern.size());
    for (const double entry : pattern) {
        require_finite(entry, "dash pattern entry");
        if (entry <= 0.0) {
            throw std::invalid_argument("dash pattern entries must be positive");
        }
        copy.push_back(entry);
    }
    dash_pattern_ = std::move(copy);
    dash_style_ = DashStyle::custom;
}

void Pen::set_line_join(LineJoin join, double miter_limit) {
    require_alive();
    require_enum(join, 2U, "line join");
    require_finite(miter_limit, "miter limit");
    if (miter_limit < 1.0) throw std::invalid_argument("miter limit must be at least 1");
    join_ = join;
    miter_limit_ = miter_limit;
}

void Pen::set_line_cap(LineCap cap) {
    require_alive();
    require_enum(cap, 2U, "line cap");
    cap_ = cap;
}

PenSnapshot Pen::snapshot() const {
    require_alive();
    PenSnapshot result;
    result.color = color_;
    result.width = width_;
    result.dash_style = dash_style_;
    result.dash_pattern = dash_pattern_;
    result.join = join_;
    result.miter_limit = miter_limit_;
    result.cap = cap_;
    return result;
}


} // namespace gui_drawing

